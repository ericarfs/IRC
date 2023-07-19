#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include "utils.c" 


#define MAX_CLIENTS 4 //quantidade maxima de clientes
#define MAX_CANAIS 5 //quantidade maxima de canais
#define BUFFER_SZ 100000 //buffer auxiliar para mensagens maiores
#define BUFFER_MAX 100 //tamanho max da mensagem


static _Atomic unsigned int cli_count = 0;
static _Atomic unsigned int chan_count = 0;
static int uid = 0;

//Estrutura do cliente
typedef struct{
	struct sockaddr_in address;
	char name[51];
	int sockfd;
	int uid;
	int onChannel;
	int channelId;
	int isAdmin;
	int isMuted;
} client_t;

client_t *clients[MAX_CLIENTS];

// Estrutura do canal
typedef struct{
	client_t *clients_ch[MAX_CLIENTS];
	char chan_name[200];
	char admin_name[51];
	char mode[2];
	int admin_uid;
	int num_users;	
} channel_t;


channel_t *canais[MAX_CANAIS];


pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

//Função que printa o endereço IP do cliente informado como parametro
void print_client_addr(struct sockaddr_in addr, char *endereco){
	sprintf(endereco,"%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);

    printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}


//Adicionar cliente na fila
void queue_add(client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = cl;
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}


//Remover cliente da fila
void queue_remove(int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i = 0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}


	pthread_mutex_unlock(&clients_mutex);
}


/*	Enviar mensagens para todos os clientes, menos ao que escreveu a mensagem
	As mensagens são só enviadas para quem está no mesmo canal que quem enviou
*/
void send_message(char *s, client_t *cli){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != cli->uid && clients[i]->channelId == cli->channelId  && clients[i]->isMuted == 0){
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){
					perror("ERRO: Falha ao enviar mensagem!");
					break;
				}				
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}



/*	Enviar mensagem de entrada no chat para todos os clientes
*/
void entered_chat(char *s, client_t *cli){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != cli->uid){
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){
					perror("ERRO: Falha ao enviar mensagem!");
					break;
				}				
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}


//Enviar mensagens para um cliente específico
void send_private_message(char *s, char *nome){
	pthread_mutex_lock(&clients_mutex);


	char buff_aux[BUFFER_SZ+54];
	char msg_aux[BUFFER_SZ+54];
	char apelido[52];

	//Separar o apelido do destinatario da mensagem e a mensagem enviada
	char *token;

	token = strtok(s, " "); 
	sprintf(apelido, "%s", token); //apelido
	token = strtok(NULL, "");
	sprintf(buff_aux, "%s", token); //mensagem

	//Se a mensagem estiver vazia, substitui '(null)' por vazio
	if (strcmp(buff_aux, "(null)") ==0){
		bzero(buff_aux, BUFFER_SZ);
	}
	

	sprintf(msg_aux, "%s: %s\n", nome, buff_aux);


	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			//colocar '@' no inicio do apelido do cliente para fazer a comparação
			char apelido_aux[52];
			apelido_aux[0] = '@';
			sprintf(&apelido_aux[1], "%s", clients[i]->name);

			if(strcmp(apelido_aux, apelido) == 0){
				if(write(clients[i]->sockfd, msg_aux, strlen(msg_aux)) < 0){
					perror("ERRO: Falha ao enviar mensagem!");
					break;
				}
				break;	
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}


/*	Função que verifica se o apelido escolhido está disponível
	Retorna 0, caso apelido ja esteja em uso; caso contrario, retorna 1
*/
int checkApelido(char *s){
	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(strcmp(clients[i]->name,s)==0){
				return 0;				
			}
		}
	}
	return 1;
}


/*	Função responsável pela criação e inserção de clientes em um canal
	Recebe o nome do canal e o cliente como parametros e retorna valores
	que serão verificados por quem chamou a função
*/
int joinChannel(char *nomeCanal, client_t *cli){
	int tam = strlen(nomeCanal);

	//Verifica se o nome é válido
	if ((nomeCanal[0] != '&' && nomeCanal[0] != '#' )
	|| (strlen(nomeCanal) <2 || strlen(nomeCanal) >200)){
        return 9; //indica que o nome é inválido
    }


    //Verificar se o canal existe
    int canalExiste = 0;
    int i;
    for(i = 0; i < MAX_CANAIS; ++i){
		if(canais[i]){
			if(strcmp(canais[i]->chan_name, nomeCanal)==0){
				canalExiste = 1;
				break;
			}
		}
	}

    //Se canal não existe, criar ele
    if (canalExiste == 0){

		//verificar se ja foi preenchido o maximo de canais
		if (chan_count == MAX_CANAIS){
			return 8;
		}

        //Configurar canal
        channel_t *chan = (channel_t *)malloc(sizeof(channel_t));
        sprintf(chan->chan_name, "%s", nomeCanal); 
        chan->admin_uid = cli->uid;
        sprintf(chan->admin_name, "%s", cli->name); 
        chan->num_users = 1;
        sprintf(chan->mode, "+p"); 
        chan->clients_ch[0] = cli;

		//Outros cliente apontam como nulo
		for (int k = 1;k<MAX_CLIENTS;k++){
        	chan->clients_ch[k] = NULL;
		}
			

		//Adicionar o canal criado
		for(int j=0; j < MAX_CANAIS; ++j){
			if(!canais[j]){
				canais[j] = chan;
				cli->channelId = j;
				break;
			}
		}
		chan_count++;
		return 7;
    }
	//Se canal existe, colocar cliente
	else{
		//Verificar o modo do canal
		//Se não for invite only, pode entrar
		if (canais[i]->mode[1] != 'i'){
			cli->channelId = i;
			for(int k=0; k < MAX_CLIENTS; ++k){
				if(!canais[i]->clients_ch[k]){
					canais[i]->clients_ch[k] = cli;
					break;
				}
			}
			
			canais[i]->num_users++;
			return 5;
		}
		
		// Se for invite only
		return 3;
	}
}


/*	Função responsável pela saida de clientes de um canal
	Recebe o cliente e o indice do canal como parametros e 
	retorna a posição do cliente removido no vetor de clientes do canal
*/
int leaveChannel(client_t *cli, int i){
	//retirar cliente do canal
	int k;
	for(k=0; k < MAX_CLIENTS; ++k){
		if(canais[i]->clients_ch[k] == cli){
			canais[i]->clients_ch[k] = NULL;
			break;
		}
	}

	canais[i]->num_users--;


	//posição do cliente removido
	return k;

}


/*	Função responsável por procurar nome de clientes em um canal
	Recebe como paremtros o vetor de clientes e o nome procurado e
	retorna -1, se não encontrar; caso contrario, retorna posição
	do cliente no vetor
*/
int findClient(client_t **cl, char *nomeKick){
	int clientEx = 0;
	int k;
	//procurar apelido informado no canal
	for(k=0; k < MAX_CLIENTS; ++k){
		if (cl[k]){
			if(strcmp(cl[k]->name, nomeKick) == 0){
				clientEx = 1;
				break;
			}
		}
								
	}

	// Cliente não existe
	if (clientEx == 0){
		return -1;
	}

	// Cliente existe
	return k; 

}


/*	Função que lida com a comunicação com cada cliente
	Todas as mensagens recebidas pelo servidor são verificadas para
	poder ser executada a ação correta antes de enviar para os clientes
*/
void *handle_client(void *arg){
	char buff_aux[BUFFER_SZ+54];
	char buff_out[BUFFER_SZ];
	int leave_flag = 0;

	cli_count++;
	client_t *cli = (client_t *)arg;

	send(cli->sockfd, cli->name, strlen(cli->name), 0);
	sprintf(buff_out, "\n** %s entrou no chat! **\n\n", cli->name);
	printf("%s", buff_out);
	entered_chat(buff_out, cli);


	bzero(buff_out, BUFFER_SZ);

	while(1){
		if (leave_flag) {
			break;
		}

		int receive = recv(cli->sockfd, buff_out, BUFFER_MAX, 0);
		if (receive > 0){
			if(strcmp(buff_out, "/ping") == 0){
				sprintf(buff_out, "pong\n");
				send(cli->sockfd, buff_out, 5, 0);
			}
			else if(strlen(buff_out) > 0){
				if (buff_out[0]=='@'){
					send_private_message(buff_out, cli->name);
				}
				else{
					char message2[BUFFER_SZ];
					char comando[BUFFER_SZ];
					char msg_aux[BUFFER_SZ];

					sprintf(message2, "%s", buff_out);

					sendCom(message2, comando, msg_aux);

					printf("%s: %s\n", cli->name, buff_out);

					if (strcmp(comando, "/join") == 0){
						//Verificar se ja está em um canal
						if (cli->onChannel == 1){
							sprintf(buff_aux, "\n- Você já está em um canal!\n\n");
						}
						else{
							int join = joinChannel(msg_aux, cli);
							//Verificar nome do canal
							if (join == 9){
								sprintf(buff_aux, "\n- Nome de canal inválido!\n\n");
							}
							else if (join == 8){
								sprintf(buff_aux, "\n- Número máximo de canais excedido!\n\n");
							}
							//Verificar se foi o primeiro a entrar no canal
							else if (join == 7){
								cli->onChannel = 1;
								cli->isAdmin = 1;
								sprintf(buff_aux, "\n- Você agora administra o canal %s!\n- Comandos:\n * /kick nomeUsuario - Fecha conexao de um usuario especifico\n * /mute nomeUsuario - Faz com que usuario nao possa mandar mensagens\n * /unmute nomeUsuario - Retira mute do usuario\n * /whois nomeUsuario - Retorna endereco IP do usuario\n * /mode modo - Altera o modo do canal (+i: invite only / +p: privado)\n * /invite nomeUsuario - Convida usuario para o canal\n * /list - Lista os comandos\n * /leave - Fecha conexao e encerra canal\n\n", msg_aux);

							}
							//Verificar se entrou no canal
							else if (join == 5){
								//mandar mensagem avisando que entrou no canal
								sprintf(buff_aux, "\n** %s entrou no canal! **\n\n", cli->name);
								send_message(buff_aux, cli);
								bzero(buff_aux, BUFFER_SZ);

								cli->onChannel = 1;
								sprintf(buff_aux, "\n- Você agora faz parte do canal %s!\n- Para sair, digite: /leave!\n\n", msg_aux);

							}
							//Verificar se entrou no canal
							else if (join == 3){
								sprintf(buff_aux, "\n- Você não pode entrar nesse canal!\n\n");

							}
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					}
					else if (strcmp(comando, "/kick") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
						else {	
							int i = cli->channelId;
							//Verificar se o admin escreveu o proprio apelido
							if (strcmp(canais[i]->admin_name, msg_aux) == 0){
								sprintf(buff_aux, "\n- Você é admin do canal. Para sair, digite: /leave!\n\n");
							}
							else{
								int kick = findClient(canais[i]->clients_ch, msg_aux); // procurar cliente no canal
								//Cliente não encontrado
								if (kick == -1){
									sprintf(buff_aux, "\n- %s não está no canal!\n\n", msg_aux);
								}
								//Cliente encontrado
								else{
									//Informar cliente que ele foi removido
									sprintf(buff_aux, "\n- Você foi removido do canal!\n\n");
									send(canais[i]->clients_ch[kick]->sockfd, buff_aux, strlen(buff_aux),0);
									bzero(buff_aux, BUFFER_SZ);

									//Remover cliente e informar aos outros do canal
									sprintf(buff_aux, "\n- %s foi removido do canal!\n\n", msg_aux);
									
									canais[i]->clients_ch[kick]->onChannel = 0;
									canais[i]->clients_ch[kick]->channelId = -1;
									canais[i]->clients_ch[kick] = NULL; // remover cliente do canal
									canais[i]->num_users--;

									send_message(buff_aux, cli); //informar outros clientes do canal
								}
							}
						}
						
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);

					}
					else if (strcmp(comando, "/mute") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
					
						else{
							int i = cli->channelId;
							//Verificar se o admin escreveu o proprio apelido
							if (strcmp(canais[i]->admin_name, msg_aux) == 0){
								sprintf(buff_aux, "\n- Você é admin do canal, não pode ser mutado!\n\n");
							}
							else{
								int mute = findClient(canais[i]->clients_ch, msg_aux); // procurar cliente no canal
								//Cliente não encontrado
								if (mute == -1){
									sprintf(buff_aux, "\n- %s não está no canal!\n\n", msg_aux);
								}
								
								//Cliente encontrado
								else{
									//Verifica se cliente está mutado
									int isMuted = canais[i]->clients_ch[mute]->isMuted;
									if (isMuted == 1){
										sprintf(buff_aux, "\n- %s já está mutado!\n\n", msg_aux);
									}
									//Mutar cliente
									else{
										canais[i]->clients_ch[mute]->isMuted = 1;
										sprintf(buff_aux, "\n- Você foi mutado!\n\n");

										send(canais[i]->clients_ch[mute]->sockfd, buff_aux, strlen(buff_aux),0);

										bzero(buff_aux, BUFFER_SZ);
										sprintf(buff_aux, "\n- %s foi mutado!\n\n", msg_aux);
									}
								}
							}
						}

						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					} 
					else if (strcmp(comando, "/unmute") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
						
						else{
							int i = cli->channelId;
							//Verificar se o admin escreveu o proprio apelido
							if (strcmp(canais[i]->admin_name, msg_aux) == 0){
								sprintf(buff_aux, "\n- Você é admin do canal, não pode ser mutado!\n\n");
							}
							else{
								int unmute = findClient(canais[i]->clients_ch, msg_aux); // procurar cliente no canal
								//Cliente não encontrado
								if (unmute == -1){
									sprintf(buff_aux, "\n- %s não está no canal!\n\n", msg_aux);
								}
								//Cliente encontrado
								else{
									//Verifica se cliente está mutado
									int isMuted = canais[i]->clients_ch[unmute]->isMuted;
									if (isMuted == 0){
										sprintf(buff_aux, "\n- %s não está mutado!\n\n", msg_aux);
									}
									//Desmutar cliente
									else{
										canais[i]->clients_ch[unmute]->isMuted = 0;
										sprintf(buff_aux, "\n- Você foi desmutado!\n\n");

										send(canais[i]->clients_ch[unmute]->sockfd, buff_aux, strlen(buff_aux),0);

										bzero(buff_aux, BUFFER_SZ);
										sprintf(buff_aux, "\n- %s foi desmutado!\n\n", msg_aux);
									}
								}
							}
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);

					} 
					else if (strcmp(comando, "/whois") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
						else{
							int i = cli->channelId;
							//Verificar se o admin escreveu o proprio apelido
							if (strcmp(canais[i]->admin_name, msg_aux) == 0){
								char endereco[16];
								print_client_addr(cli->address, endereco);
								sprintf(buff_aux, "\n- Seu IP é: %s!\n\n", endereco);
							}
							else{
								int end = findClient(canais[i]->clients_ch, msg_aux); // procurar cliente no canal
								//Cliente não encontrado
								if (end == -1){
									sprintf(buff_aux, "\n- %s não está no canal!\n\n", msg_aux);
								}
								//Cliente encontrado
								else{
									char endereco[16];
									print_client_addr(canais[i]->clients_ch[end]->address, endereco);
									sprintf(buff_aux, "\n- O IP de %s é: %s!\n\n", msg_aux, endereco);
								}
							}
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);

					}

					else if (strcmp(comando, "/mode") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
						else{
							int indice = cli->channelId;
							//modo invite only
							if (strcmp(msg_aux,"+i")==0){
								if (canais[indice]->mode[1] != 'i'){
									canais[indice]->mode[1] = 'i';
									sprintf(buff_aux, "\n- O seu canal agora é invite only!\n\n");
								}
								else{
									sprintf(buff_aux, "\n- O seu canal já é invite only!\n\n");
								}
							}
							//modo privado (padrão)
							else if (strcmp(msg_aux,"+p")==0){
								if (canais[indice]->mode[1] != 'p'){
									canais[indice]->mode[1] = 'p';
									sprintf(buff_aux, "\n- O seu canal agora é privado!\n\n");
								}
								else{
									sprintf(buff_aux, "\n- O seu canal já é privado!\n\n");
								}
							}
							
							
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					}

					else if (strcmp(comando, "/invite") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
					
						else{
							int i = cli->channelId;
							//Verificar se o admin escreveu o proprio apelido
							if (strcmp(canais[i]->admin_name, msg_aux) == 0){
								sprintf(buff_aux, "\n- Você é admin do canal, não pode se convidar!\n\n");
							}
							else{
								int find = findClient(clients, msg_aux); // procurar cliente no servidor
								//Cliente não encontrado
								if (find == -1){
									sprintf(buff_aux, "\n- %s não está no servidor!\n\n", msg_aux);
								}
								//Cliente encontrado
								else{
									//verificar se cliente ja esta em um canal
									if (clients[find]->onChannel == 1){
										sprintf(buff_aux, "\n- %s já está em um canal!\n\n", msg_aux);
									}
									else{
										//adicionar cliente no canal
										clients[find]->channelId = i;
										for(int k=0; k < MAX_CLIENTS; ++k){
											if(!canais[i]->clients_ch[k]){
												canais[i]->clients_ch[k] = clients[find];
												clients[find]->onChannel = 1;
												break;
											}
										}											
										canais[i]->num_users++;

										//mandar mensagem avisando que entrou no canal
										sprintf(buff_aux, "\n** %s entrou no canal! **\n\n", clients[find]->name);
										send_message(buff_aux, cli);
										bzero(buff_aux, BUFFER_SZ);

										sprintf(buff_aux, "\n- %s convidou você para o canal %s!\n- Para sair, digite: /leave!\n\n", cli->name, canais[i]->chan_name);

										send(clients[find]->sockfd, buff_aux, strlen(buff_aux),0);

										bzero(buff_aux, BUFFER_SZ);

										sprintf(buff_aux, "\n- Você convidou %s para o canal %s!\n\n", msg_aux, canais[i]->chan_name);
									}
								}
							}
						}

						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					} 
					else if (strcmp(comando, "/list") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n\n");
						}
						else{
							sprintf(buff_aux, "\n- Comandos:\n * /kick nomeUsuario - Fecha conexao de um usuario especifico\n * /mute nomeUsuario - Faz com que usuario nao possa mandar mensagens\n * /unmute nomeUsuario - Retira mute do usuario\n * /whois nomeUsuario - Retorna endereco IP do usuario\n * /mode modo - Altera o modo do canal (+i: invite only / +p: privado)\n * /invite nomeUsuario - Convida usuario para o canal\n * /list - Lista os comandos\n * /leave - Fecha conexao e encerra canal\n\n");
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					}
					else if (strcmp(comando, "/leave") == 0){
						//Verificar se realmente está em um canal
						if (cli->onChannel == 0){
							sprintf(buff_aux, "\n- Você não está em nenhum canal!\n\n");
						}
						else{
							int i = cli->channelId;
							int leave = leaveChannel(cli, i);

							//Se o administrador saiu do canal, ele é encerrado
							if (leave == 0){
								//informar encerramento aos outros clientes do canal
								sprintf(buff_aux, "\n** @%s encerrou o canal! **\n\n", cli->name);
								send_message(buff_aux, cli);
								bzero(buff_aux, BUFFER_SZ);

								//atualizar informações
								cli->onChannel = 0;
								cli->channelId = -1;
								sprintf(buff_aux, "\n- Você encerrou o canal!\n\n");

								//Atualizar informações dos outros clientes
								for (int k = 1;k<MAX_CLIENTS;k++){
									if (canais[i]->clients_ch[k]){
										canais[i]->clients_ch[k]->onChannel = 0;
										canais[i]->clients_ch[k]->channelId = -1;
										canais[i]->clients_ch[k] = NULL;
									}
								}

								//encerrar canal
								canais[i] = NULL;
							}
							else{
								//informar saida aos outros clientes do canal
								sprintf(buff_aux, "\n** %s saiu do canal! **\n\n", cli->name);
								send_message(buff_aux, cli);
								bzero(buff_aux, BUFFER_SZ);

								//atualizar informações
								cli->onChannel = 0;
								cli->channelId = -1;
								sprintf(buff_aux, "\n- Você saiu do canal!\n\n");
							}
						}
						// se o cliente era admin, deixa de ser quando sai
						cli->isAdmin = 0; 
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);		
					} 
					else{
						// Mensagens de admin começam com @ no nome dele
						if (cli->isAdmin == 1){
							sprintf(buff_aux, "@%s: %s\n", cli->name, buff_out);
						}
						else{
							sprintf(buff_aux, "%s: %s\n", cli->name, buff_out);
						}
						
						send_message(buff_aux, cli);
					}
					
				}
				
				bzero(buff_aux, BUFFER_SZ);

			}
			
		} else if (receive == 0){
			sprintf(buff_out, "\n** %s saiu do chat! **\n\n", cli->name);
			printf("%s", buff_out);
			send_message(buff_out, cli);
			leave_flag = 1;

		} else {
			printf("ERROR: -1\n");
			leave_flag = 1;
		}

		bzero(buff_out, BUFFER_SZ);
	}

  	//Tirar o cliente da fila 
	close(cli->sockfd);
	queue_remove(cli->uid);
	free(cli);
	cli_count--;
	printf("%d\n",cli_count);
	pthread_detach(pthread_self());

	return NULL;
}


int main(int argc, char **argv){
	char ip[11] = "0.0.0.0";
	int port = 4000;
	int connfd = 0;
	struct sockaddr_in server_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0){
        perror("[-]Socket error");
        exit(1);
    }
    printf("[+]TCP server socket created.\n");

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = port;
    server_addr.sin_addr.s_addr = inet_addr(ip);


	signal(SIGPIPE, SIG_IGN);

	// Abrindo socket
    int yes = 1;
    if(setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,&yes, sizeof(int)) < 0) {
		printf("ERRO: Não foi possivel abrir o socket.\n");
		return EXIT_FAILURE;
	}

	//bind
	if(bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
		printf("ERRO: Porta está sendo usada por outro processo, ou há um problema com o IP definido.\n");
		return EXIT_FAILURE;
	}
    printf("[+]Bind na porta: %d\n", port);

	//listen
	if(listen(server_sock, 10) < 0){
		printf("ERRO: Não foi possivel colocar em modo listening.\n");
		return EXIT_FAILURE;
	}
    printf("Listening...\n");

	printf(">>> SALA DE BATE PAPO <<<\n");

	char recv_apelido[51];
	bzero(recv_apelido, 51);

	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(server_sock, (struct sockaddr*)&cli_addr, &clilen);
		
		
		char msg[6];
		/* Verificar a quantidade de clientes conectados */
		if((cli_count) == MAX_CLIENTS){
			sprintf(msg, "error");
			send(connfd, msg, strlen(msg), 0);
			close(connfd);
			continue;
		}
		else{
			sprintf(msg, "check");
			send(connfd, msg, strlen(msg), 0);
			int apCheck = 0;
			
			char resp[51];
			do{
				
				recv(connfd, recv_apelido, BUFFER_MAX, 0);
				apCheck = checkApelido(recv_apelido);
				if (apCheck == 0){
					sprintf(resp, "error");
					send(connfd, resp, strlen(resp), 0);
					memset(&recv_apelido, '\0', 51);
				}
				else{
					sprintf(resp, "check");
					send(connfd, resp, strlen(resp), 0);
					break;
				}
			}
			while (apCheck == 0);
		}

		/* Configurar cliente */
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->address = cli_addr;
		cli->sockfd = connfd;
		cli->uid = uid++;
		cli->onChannel = 0;
		cli->channelId = -1;
		cli->isAdmin = 0;
		cli->isMuted = 0;
		sprintf(cli->name, "%s", recv_apelido);

		memset(&recv_apelido, '\0', 51);


		/* Adicionar cliente na fila */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		
		sleep(1);
	}

	return EXIT_SUCCESS;
}
