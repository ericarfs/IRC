#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Funcao que substitui o ultimo caracter de uma string de '\n' por '\0'
void str_trim_lf (char* arr, int length) {
	int i;
	for (i = 0; i < length; i++) { // trim \n
		if (arr[i] == '\n') {
			arr[i] = '\0';
			break;
		}
	}
}

//Função responsável por separar o comando inserido e o resto da mensagem
void sendCom(char *entrada, char *comando, char *mensagem){
	char *token;
	token = strtok(entrada, " ");
	sprintf(comando, "%s", token); //comando

	token = strtok(NULL, " ");
	sprintf(mensagem, "%s", token); //mensagem
	str_trim_lf(mensagem, strlen(mensagem));
} 

