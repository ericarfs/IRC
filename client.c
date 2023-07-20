#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "utils.c" 

#define LENGTH 4096
#define BUFFER_SZ 100000

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[51];


//Função que atualiza a tela
void str_overwrite_stdout() {
	printf("\r%s", "> ");
	fflush(stdout);
}

 
//Função para pegar o ctrl_c
void sigintHandler(int sig_num){ 
    signal(SIGINT, sigintHandler); 
	fflush(stdout); 
}


//Função responsável por lidar com o envio de mensagens
void send_msg_handler() {
  	char message[LENGTH];
	char buffer[BUFFER_SZ];

	while(1) {
		str_overwrite_stdout();

		//Verificar se o cliente pressionou ctrl_d para sair do chat
		if(fgets(message, LENGTH, stdin) == NULL){
			flag = 1;
			break;
		}
		str_trim_lf(message, LENGTH);

		if (strcmp(message, "/quit") == 0) {
			flag = 1;
			break;
		}
		else{
			sprintf(buffer, "%s", message);
			send(sockfd, buffer, strlen(buffer), 0);
		}

		bzero(message, LENGTH);
		bzero(buffer, BUFFER_SZ);
	}
}


//Função responsável por lidar com o recebimento de mensagens
void recv_msg_handler() {
	char message[BUFFER_SZ];
	while (1) {
		int receive = recv(sockfd, message, BUFFER_SZ, 0);
		if (receive > 0) {
			printf("%s", message);
			str_overwrite_stdout();
		} else if (receive == 0) {
			break;
		} 
		memset(message, 0, sizeof(message));
		
	}
}


int main(int argc, char **argv){

	char ip[11] = "0.0.0.0";
	int port = 4000;

	signal(SIGINT, sigintHandler);

	//Esperar cliente se conectar ao servidor
	char conectar[50];
	while(1){
		printf("Digite '/connect' para conectar-se ao servidor > ");

		fgets(conectar, 50, stdin);
		str_trim_lf(conectar, strlen(conectar));

		if (strcmp(conectar, "/connect") == 0){
			break;
		}
		
	}
	

	struct sockaddr_in addr;

	/* Configurar Socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0){
    	perror("[-]Socket error");
    	exit(1);
  	}
  	printf("[+]TCP server socket created.\n");

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);
	addr.sin_port = port;


	// Conectar ao servidor
	int err = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
	if (err == -1) {
			printf("Não foi possível se concectar ao servidor\n");
			return EXIT_FAILURE;
	}


	char con[6];
	// Receber resposta do servidor para estabeleciemento de conexao
	recv(sockfd, con, 6, 0); 

	// Se a resposta for "error", a conexão é encerrada
	if (strcmp(con, "error") == 0){
		printf("Maximo de clientes conectados. Conexao Rejeitada.\n");
	}
	else{
		// Informar apelido que vai usar no servidor
		char apelido[100];
		while(1){
			printf("Digite '/nickname' e informe o seu apelido > ");

			fgets(apelido, 100, stdin);
			str_trim_lf(apelido, strlen(apelido));

			char aux[10];
			char nick[51];

			//Separar o comando inserido e o apelido 
			sendCom(apelido, aux, nick);


			if (strcmp(aux, "/nickname") != 0){
				continue;
			}
			else if (strcmp(nick, "(null)") ==0||strlen(nick) < 2 || strlen(nick)>50){
				printf("Apelido muito grande ou muito pequeno! \n");
				continue;
			}

			// Enviar apelido escolhido pro servidor
			send(sockfd, nick, strlen(nick), 0);

			char resp[6];
			// Receber resposta 
			recv(sockfd, resp, 6, 0);

			//Verificar se o apelido foi aceito
			if (strcmp(resp, "error") == 0){
				printf("Apelido inválido! \n");
			}
			else{
				break;
			}

			memset(&resp, '\0', 6);
			memset(&aux, '\0', 10);
			memset(&nick, '\0', 51);

			
		}

		// Receber apelido 
		recv(sockfd, name, 51, 0);
		

		printf("\n\n>>> SALA DE BATE PAPO <<<\n\n");
		printf("- Para sair do chat, digite: /quit ou pressione Ctrl + D\n");
		printf("- Para entrar em um canal, digite: /join nomeCanal\n");
		printf("- Digite /ping para receber resposta do servidor\n\n");

		pthread_t send_msg_thread;
		if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0){
			printf("ERROR: pthread\n");
			return EXIT_FAILURE;
		}

		pthread_t recv_msg_thread;
		if(pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0){
			printf("ERROR: pthread\n");
			return EXIT_FAILURE;
		}

		while (1){
			if(flag){
				printf("\nAté logo!\n");
				break;
			}
		}



	}
	
	close(sockfd);

	return EXIT_SUCCESS;
}
