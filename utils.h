#ifndef UTILS_H
#define UTILS_H

void str_overwrite_stdout(); // Funcao que atualiza a tela
void str_trim_lf(char* arr, int length); // Funcao que substitui o ultimo caracter de uma string de '\n' por '\0'
void sigintHandler(int sig_num); // Funcao para pegar o ctrl_c
void sendCom(char *entrada, char *comando, char *mensagem); // Funcao responsavel por separar o comando inserido e o resto da mensagem

#endif