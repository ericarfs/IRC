compile:
	gcc -Wall -g3 -fsanitize=address -pthread -lm server.c -o server
	gcc -Wall -g3 -fsanitize=address -pthread client.c utils.c -o client
FLAGS    = -L /lib64
LIBS     = -lusb-1.0 -l pthread


serv:
	./server

cli:
	./client

