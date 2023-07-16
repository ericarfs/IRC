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

#define MAX_CLIENTS 100
#define BUFFER_SZ 100000
#define BUFFER_MAX 100 //tamanho max da mensagem


static _Atomic unsigned int cli_count = 0;
static int uid = 0;


typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char name[51];
} client_t;

client_t *clients[MAX_CLIENTS];

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

	for(int i=0; i < MAX_CLIENTS; ++i){
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
void send_message(char *s, int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != uid){
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


//Função que lida com a comunicação com cada cliente
void *handle_client(void *arg){
	char buff_aux[BUFFER_SZ+54];
	char buff_out[BUFFER_SZ];
	int leave_flag = 0;

	cli_count++;
	client_t *cli = (client_t *)arg;

	send(cli->sockfd, cli->name, strlen(cli->name), 0);
	sprintf(buff_out, "%s entrou no chat!\n", cli->name);
	printf("%s", buff_out);
	send_message(buff_out, cli->uid);


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
					printf("%s: %s\n", cli->name, buff_out);

					sprintf(buff_aux, "%s: %s\n", cli->name, buff_out);
					send_message(buff_aux, cli->uid);
				}
				

				bzero(buff_aux, BUFFER_SZ);

			}
			
		} else if (receive == 0){
			sprintf(buff_out, "%s saiu do chat!\n", cli->name);
			printf("%s", buff_out);
			send_message(buff_out, cli->uid);
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

		/* Verificar a quantidade de clientes conectados */
		if((cli_count + 1) == MAX_CLIENTS){
			printf("Maximo de clientes conectados. Coneccao Rejeitada. ");
			close(connfd);
			continue;
		}
		else{
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
		sprintf(cli->name, "%s", recv_apelido);

		memset(&recv_apelido, '\0', 51);


		/* Adicionar cliente na fila */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		
		sleep(1);
	}

	return EXIT_SUCCESS;
}
