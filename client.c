#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#include "utils.h"

#define LENGTH 100
#define BUFFER_SZ 100000

volatile sig_atomic_t flag = 0;
int sockfd = -1; // descritor de arquivo do socket iniciado com valor invalido

// Funcao para criar e configurar socket
void cria_e_configura_socket(char ip[11], int port, struct sockaddr_in *addr)
{
    // Cria socket de dominio IPv4 e tipo e protocolo de comunicacao TCP
	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(sockfd < 0)
    { // se contem erros
    	perror("[-]Socket error");
    	exit(1);
  	} // senao
  	printf("[+]TCP server socket created.\n");

    // Monta estruturas de enderecos a serem vinculadas ao socket na conexao
	(*addr).sin_family = AF_INET;
	(*addr).sin_addr.s_addr = inet_addr(ip);
	(*addr).sin_port = port;
}

// Funcao para esperar comando de cliente para se conectar ao servidor
void aguarda_solicitacao_de_conexao()
{
    char conectar[10];
	while(1)
    {
		printf("Digite '/connect' para conectar-se ao servidor > ");

		fgets(conectar, 10, stdin);
		str_trim_lf(conectar, strlen(conectar));

		if(strcmp(conectar, "/connect") == 0) break;
		
	}	
}

// Funcao para conectar ao servidor
void estabelece_conexao(struct sockaddr_in addr)
{
    int err = connect(sockfd, (struct sockaddr *) &addr, sizeof(addr));
	if(err == -1)
	{
		printf("Não foi possível se conectar ao servidor.\n");
		exit(1);
	}
}

// Funcao para esperar cliente inserir comando com apelido valido
void aguarda_insercao_de_apelido()
{
	char apelido[100];
	while(1)
	{
		printf("Digite '/nickname' e informe o seu apelido > ");

		fgets(apelido, 100, stdin); // le token com apelido do fluxo de entrada
		str_trim_lf(apelido, strlen(apelido)); // remove '\n'

		char aux[10];
		char nick[51];
 
		sendCom(apelido, aux, nick); // separa o comando inserido e o apelido

		if(strcmp(aux, "/nickname") != 0) continue; // se token invalido, continua
		else if(strcmp(nick, "(null)") == 0 || strlen(nick) < 2 || strlen(nick) > 50)
        { // senao, se comparacao de apelido invalida 
			printf("Apelido muito grande ou muito pequeno!\n");
			continue;
		}

		send(sockfd, nick, strlen(nick), 0); // envia apelido escolhido pro servidor

		char resp[6];
		recv(sockfd, resp, 6, 0); // recebe resposta

		if(strcmp(resp, "error") == 0) printf("Apelido inválido! \n");
		else break; // se apelido aceito, sai do loop

        memset(&resp, '\0', 6);
		memset(&aux, '\0', 10);
		memset(&nick, '\0', 51);
	}
}

// Funcao responsavel por lidar com o envio de mensagens
void send_msg_handler()
{
  	char message[LENGTH];
	char buffer[BUFFER_SZ];

	while(1)
	{
		str_overwrite_stdout();

		// Verificar se o cliente pressionou ctrl_d
		// para sair do chat
		if(fgets(message, LENGTH, stdin) == NULL)
		{
			flag = 1;
			break;
		}
		str_trim_lf(message, LENGTH);

		if(strcmp(message, "/quit") == 0)
		{
			flag = 1;
			break;
		} else
		{
			sprintf(buffer, "%s", message);
			send(sockfd, buffer, strlen(buffer), 0);
		}

		bzero(message, LENGTH);
		bzero(buffer, BUFFER_SZ);
	}
}

// Funcao responsavel por lidar com o recebimento de mensagens
void recv_msg_handler()
{
	char message[BUFFER_SZ];
	while(1)
	{
		int receive = recv(sockfd, message, BUFFER_SZ, 0);
		if(receive > 0)
		{
			printf("%s", message);
			str_overwrite_stdout();
		} else
		{
			break;
		} 
		memset(message, 0, sizeof(message));
	}
}

int main(int argc, char **argv)
{
	char ip[11] = "0.0.0.0"; // atribui endereco IP do servidor
	int port = 4000; // atribui numero de porta do servidor

	signal(SIGINT, sigintHandler); // trata sinal de interrupção de execução do programa
    
    aguarda_solicitacao_de_conexao(); // aguarda cliente solicitar conexao ao servidor

	struct sockaddr_in addr; // declara estrutura que contera endereco IP do servidor

    cria_e_configura_socket(ip, port, &addr);
    estabelece_conexao(addr);

    char con[6];
    recv(sockfd, con, 6, 0); // recebe resposta do servidor sobre estado de conexao

	if(strcmp(con, "error") == 0)
    { // se a resposta for "error", a conexao eh encerrada
		printf("Maximo de clientes conectados. Conexao Rejeitada.\n");
	} else
    {
        aguarda_insercao_de_apelido(); // aguarda cliente informar apelido que vai usar no servidor
    }

    char name[51];
	recv(sockfd, name, 51, 0); // recebe resposta com apelido

	printf("\n\n>>> SALA DE BATE PAPO <<<\n\n");
	printf("- Para sair do chat, digite: /quit ou pressione Ctrl + D\n");
	printf("- Para entrar em um canal, digite: /join nomeCanal\n");
	printf("- Digite /ping para receber resposta do servidor\n\n");

	pthread_t send_msg_thread;
  	if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0)
	{
		printf("ERROR: pthread.\n");
    	return EXIT_FAILURE;
	}

	pthread_t recv_msg_thread;
  	if(pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0)
	{
		printf("ERROR: pthread.\n");
		return EXIT_FAILURE;
	}

	while (1)
	{
		if(flag)
		{
			printf("\nAté logo!\n");
			break;
    	}
	}

	close(sockfd);

	return EXIT_SUCCESS;
}