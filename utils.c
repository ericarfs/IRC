#include <stdio.h>
#include <string.h>
#include <signal.h>

// Funcao que atualiza a tela
void str_overwrite_stdout()
{
	printf("\r%s", "> ");
	fflush(stdout);
}

// Funcao que substitui o ultimo caracter
// de uma string, de '\n' por '\0'
void str_trim_lf(char* arr, int length)
{
	int i;
	for(i = 0; i < length; i++)
	{ // trim \n
		if(arr[i] == '\n')
		{
			arr[i] = '\0';
			break;
		}
	}
}

// Funcao para pegar o ctrl_c
void sigintHandler(int sig_num)
{ 
    signal(SIGINT, sigintHandler); 
	fflush(stdout);
}

// Função responsável por separar o comando inserido e o resto da mensagem
void sendCom(char *entrada, char *comando, char *mensagem){
	char *token;
	token = strtok(entrada, " "); // separa string ate final do token,
								  // delimitado por espaco
	sprintf(comando, "%s", token); // guarda token em comando

	token = strtok(NULL, " "); // separa string entrada a partir da delimitacao
							   // anterior ate final
	sprintf(mensagem, "%s", token); // guarda novo resultado em mensagem
	str_trim_lf(mensagem, strlen(mensagem)); // remove '/n' da mensagem
} 

