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


#define MAX_CLIENTS 3 //quantidade maxima de cliente
#define MAX_CANAIS 3 //quantidade maxima de canais
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
} client_t;

client_t *clients[MAX_CLIENTS];

// Estrutura do canal
typedef struct{
	client_t *clients_chan[MAX_CLIENTS];
	char chan_name[200];
	char admin_name[51];
	char mode[2];
	int admin_uid;
	int num_users;	
} channel_t;


channel_t *canais[MAX_CANAIS];


pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;


void print_client_addr(struct sockaddr_in addr){
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


//Enviar mensagens para todos os clientes, menos ao que escreveu a mensagem
// Se cliente estiver em um canal, mensagem só é enviada pra quem tá no mesmo canal
void send_message(char *s, client_t *cli){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != cli->uid && clients[i]->channelId == cli->channelId){
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


//Função que verifica se o apelido escolhido está disponível
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


//Função responsável pela criação e inserção de clientes em um canal
int joinChannel(char *nomeCanal, client_t *cli){
	//Verifica se o nome é válido
	if (nomeCanal[0] != '&' && nomeCanal[0] != '#'){
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
        //Configurar canal
        channel_t *chan = (channel_t *)malloc(sizeof(channel_t));
        sprintf(chan->chan_name, "%s", nomeCanal); 
        chan->admin_uid = cli->uid;
        sprintf(chan->admin_name, "%s", cli->name); 
        chan->num_users = 1;
        sprintf(chan->mode, "+p"); 
        chan->clients_chan[0] = cli;

		//Outros cliente apontam como nulo
		for (int k = 1;k<MAX_CLIENTS;k++){
        	chan->clients_chan[k] = NULL;
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
				if(!canais[i]->clients_chan[k]){
					canais[i]->clients_chan[k] = cli;
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


//Função responsável pela saida de clientes de um canal
int leaveChannel(client_t *cli, int i){
	//retirar cliente do canal
	int k;
	for(k=0; k < MAX_CLIENTS; ++k){
		if(canais[i]->clients_chan[k] == cli){
			canais[i]->clients_chan[k] = NULL;
			break;
		}
	}

	canais[i]->num_users--;


	//retorna a posição do cliente removido no vetor de clientes do canal
	return k;

}


//Função responsável por procurar nome de clientes em um canal
int findClient(char *nomeKick, int i){
	int clientEx = 0;
	int k;
	//procurar apelido informado no canal
	for(k=1; k < MAX_CLIENTS; ++k){
		if (canais[i]->clients_chan[k]){
			if(strcmp(canais[i]->clients_chan[k]->name, nomeKick) == 0){
				clientEx = 1;
				break;
			}
		}
								
	}

	// Cliente não existe
	if (clientEx == 0){
		return -1;
	}

	// Se cliente existe, retorna a posição no vetor de clientes do canal
	return k; 

}


//Função que lida com a comunicação com cada cliente
void *handle_client(void *arg){
	char buff_aux[BUFFER_SZ+54];
	char buff_out[BUFFER_SZ];
	int leave_flag = 0;

	cli_count++;
	printf("%d\n",cli_count);
	client_t *cli = (client_t *)arg;

	send(cli->sockfd, cli->name, strlen(cli->name), 0);
	sprintf(buff_out, "\n** %s entrou no chat! **\n", cli->name);
	printf("%s", buff_out);
	send_message(buff_out, cli);


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
							sprintf(buff_aux, "\n- Você já está em um canal!\n");
						}
						else{
							int join = joinChannel(msg_aux, cli);
							printf("join? %d\n", join);
							//Verificar nome do canal
							if (join == 9){
								sprintf(buff_aux, "\n- Nome de canal inválido!\n");

							}
							//Verificar se foi o primeiro a entrar no canal
							else if (join == 7){
								cli->onChannel = 1;
								cli->isAdmin = 1;
								sprintf(buff_aux, "\n- Você agora administra o canal %s!\n", msg_aux);

							}
							//Verificar se entrou no canal
							else if (join == 5){
								//mandar mensagem avisando que entrou no canal
								sprintf(buff_aux, "\n** %s entrou no canal! **\n", cli->name);
								send_message(buff_aux, cli);
								bzero(buff_aux, BUFFER_SZ);

								cli->onChannel = 1;
								sprintf(buff_aux, "\n- Você agora faz parte do canal %s!\n- Para sair, digite: /leave!\n", msg_aux);

							}
							//Verificar se entrou no canal
							else if (join == 3){
								sprintf(buff_aux, "\n- Você não pode entrar nesse canal!\n");

							}
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					}
					else if (strcmp(comando, "/kick") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n");
						}
						else {	
							int i = cli->channelId;
							if (strcmp(canais[i]->admin_name, msg_aux) == 0){
								sprintf(buff_aux, "\n- Você não pode se remover do próprio canal. Para sair, digite: /leave!\n");
							}
							else{
								int kick = findClient(msg_aux, i); // procurar cliente no canal
								//Cliente não encontrado
								if (kick == -1){
									sprintf(buff_aux, "\n- %s não está no canal!\n", msg_aux);
								}
								//Cliente encontrado
								else{
									//Informar cliente que ele foi removido
									sprintf(buff_aux, "\n- Você foi removido do canal!\n");
									send(canais[i]->clients_chan[kick]->sockfd, buff_aux, strlen(buff_aux),0);
									bzero(buff_aux, BUFFER_SZ);

									//Remover cliente e informar aos outros do canal
									sprintf(buff_aux, "\n- %s foi removido do canal!\n", msg_aux);
									
									canais[i]->clients_chan[kick]->onChannel = 0;
									canais[i]->clients_chan[kick]->channelId = -1;
									canais[i]->clients_chan[kick] = NULL; // remover cliente do canal
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
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n");
						}
					
						else{
							int i = cli->channelId;
							int kick = findClient(msg_aux, i); // procurar cliente no canal
							//Cliente não encontrado
							if (kick == -1){
								sprintf(buff_aux, "\n- %s não está no canal!\n", msg_aux);
							}
							//Cliente encontrado
							else{
								//muteClient(msg_aux, cli->uid);
							}
						}

						send(cli->sockfd, buff_aux, strlen(buff_aux),0);
					} 
					else if (strcmp(comando, "/unmute") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n");
						}
						
						else{
							int i = cli->channelId;
							int kick = findClient(msg_aux, i); // procurar cliente no canal
							//Cliente não encontrado
							if (kick == -1){
								sprintf(buff_aux, "\n- %s não está no canal!\n", msg_aux);
							}
							//Cliente encontrado
							else{
								//muteClient(msg_aux, cli->uid);
							}
						}
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);

					} 
					else if (strcmp(comando, "/leave") == 0){
						//Verificar se realmente está em um canal
						if (cli->onChannel == 0){
							sprintf(buff_aux, "\n- Você não está em nenhum canal!\n");
						}
						else{
							int i = cli->channelId;
							int leave = leaveChannel(cli, i);

							//Se o administrador saiu do canal, ele é encerrado
							if (leave == 0){
								//informar encerramento aos outros clientes do canal
								sprintf(buff_aux, "\n** @%s encerrou o canal! **\n", cli->name);
								send_message(buff_aux, cli);
								bzero(buff_aux, BUFFER_SZ);

								//atualizar informações
								cli->onChannel = 0;
								cli->channelId = -1;
								sprintf(buff_aux, "\n- Você encerrou o canal!\n");

								//Atualizar informações dos outros clientes
								for (int k = 1;k<MAX_CLIENTS;k++){
									if (canais[i]->clients_chan[k]){
										canais[i]->clients_chan[k]->onChannel = 0;
										canais[i]->clients_chan[k]->channelId = -1;
										canais[i]->clients_chan[k] = NULL;
									}
								}

								//encerrar canal
								canais[i] = NULL;
							}
							else{
								//informar saida aos outros clientes do canal
								sprintf(buff_aux, "\n** %s saiu do canal! **\n", cli->name);
								send_message(buff_aux, cli);
								bzero(buff_aux, BUFFER_SZ);

								//atualizar informações
								cli->onChannel = 0;
								cli->channelId = -1;
								sprintf(buff_aux, "\n- Você saiu do canal!\n");
							}
						}
						// se o cliente era admin, deixa de ser quando sai
						cli->isAdmin = 0; 
						send(cli->sockfd, buff_aux, strlen(buff_aux),0);		
					} 
					else if (strcmp(comando, "/mode") == 0){
						//Verificar se é admin de algum canal
						if (cli->isAdmin == 0){
							sprintf(buff_aux, "\n- Você não é admin de nenhum canal!\n");
						}
						else{
							int indice = cli->channelId;
							//modo invite only
							if (strcmp(msg_aux,"+i")==0){
								if (canais[indice]->mode[1] != 'i'){
									canais[indice]->mode[1] = 'i';
									sprintf(buff_aux, "\n- O seu canal agora é invite only!\n");
								}
								else{
									sprintf(buff_aux, "\n- O seu canal já é invite only!\n");
								}
							}
							//modo privado (padrão)
							else if (strcmp(msg_aux,"+p")==0){
								if (canais[indice]->mode[1] != 'p'){
									canais[indice]->mode[1] = 'p';
									sprintf(buff_aux, "\n- O seu canal agora é privado!\n");
								}
								else{
									sprintf(buff_aux, "\n- O seu canal já é privado!\n");
								}
							}
							
							
						}
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
			sprintf(buff_out, "\n** %s saiu do chat! **\n", cli->name);
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
		sprintf(cli->name, "%s", recv_apelido);

		memset(&recv_apelido, '\0', 51);


		/* Adicionar cliente na fila */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		
		sleep(1);
	}

	return EXIT_SUCCESS;
}
