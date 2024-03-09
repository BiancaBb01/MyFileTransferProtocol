#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include "sha256_hash.h"

#define MAX_LEN 1024

int errno;
int sockd; // descriptor socket

void send_to_server(char* buffer);
void read_from_server(char* buffer);

int main (int argc, char *argv[]) {
  struct sockaddr_in server_address;
	bzero(&server_address, sizeof(server_address));

  if(argc != 3) {
		printf("Sintaxa: %s <server> <port>\n", argv[0]);
		exit(-1);
	}

  int port = atoi(argv[2]);

  if((sockd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
		perror ("Eroare la creare socket-ului.");
		return errno;
	}

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = inet_addr(argv[1]);
  server_address.sin_port = htons (port);
  
  if(connect(sockd, (struct sockaddr *) &server_address, sizeof(struct sockaddr)) == -1) {
		perror ("Eroare la conectarea la server.");
		return errno;
	}

  char buffer[MAX_LEN];
	bzero(buffer, MAX_LEN);
	printf("Conectat la server.\n> ");
	while(fgets(buffer, MAX_LEN, stdin) != NULL) {
		if(strlen(buffer) == 1) {
			printf("> ");
			continue;
		}
		send_to_server(buffer); //comanda
		read_from_server(buffer); //raspuns

		//verificare raspunsuri speciale server
		char* tmp;
		tmp = strtok(buffer, "|");

		if(!strcmp(tmp, "await_password")){
			tmp = strtok(NULL, "|");
			printf("Server: %s\n", tmp);
			do {
				printf("Parola: ");
				bzero(buffer, MAX_LEN);
				fgets(buffer, MAX_LEN, stdin);
				buffer[strlen(buffer) - 1] = '\0'; //eliminam \n
				send_to_server(SHA256(buffer)); //trimitem parola criptata
				read_from_server(buffer);
				tmp = strtok(buffer, "|");
				if(!strcmp(tmp, "await_password")) { //parola gresita
					tmp = strtok(NULL, "|");
					printf("Server: %s\n", tmp);
					continue;
				} else if(!strcmp(tmp, "quitting")){ //serverul inchide conexiunea
					tmp = strtok(NULL, "|");
					printf("Server: %s\n", tmp);
					exit(0);
				}
				break;
			} while(1);
		} else if(!strcmp(tmp, "quit")){ //comanda quit => serverul inchide conexiunea
			tmp = strtok(NULL, "|");
			printf("Server: %s\n", tmp);
			break;
		} else if(!strcmp(tmp, "await_upload")){ //serverul asteapta un fisier
			tmp = strtok(NULL, "|"); //primim numele fisierului (validat de server)
			//verificam daca exista fisierul
			FILE* file = fopen(tmp, "r");
			if(file == NULL) {
				printf("Fisierul %s nu exista.\n> ", tmp);
				send_to_server("cancel"); //trimitem cancel la server
				read_from_server(buffer);
				continue;
			}
			//trimitem dimensiunea fisierului
			fseek(file, 0L, SEEK_END);
			int file_size = ftell(file);
			fseek(file, 0L, SEEK_SET);
			sprintf(buffer, "%d", file_size);
			send_to_server(buffer);

			//asteptam validare pentru inceperea transferului
			read_from_server(buffer);
			if(strncmp(buffer, "ready", 5)){ //orice altceva decat "ready" => eroare pe server
				printf("Upload anulat.\n> ");
				continue;
			}

			//trimitem continutul fisierului
			bzero(buffer, MAX_LEN);
			int bytes_read;
			while((bytes_read = fread(buffer, 1, MAX_LEN, file)) > 0) { //citim din fisier
				write(sockd, buffer, bytes_read); //trimitem la server
				bzero(buffer, MAX_LEN);
			}
			fclose(file);

			//asteptam mesaj de succes / eroare
			read_from_server(buffer);
		}
		printf("Server: %s\n> ", buffer);
		bzero(buffer, MAX_LEN);
	}
  close(sockd);
	return 0;
}

//functia de trimitere a comenzilor la server
void send_to_server(char* buffer) {
	if(write(sockd, buffer, strlen(buffer)) <= 0) {
		perror("Eroare la scrierea in socket");
		exit(errno);
	}
}

//functia de citire a raspunsurilor de la server
void read_from_server(char* buffer) {
	bzero(buffer, MAX_LEN);
	if(read(sockd, buffer, MAX_LEN) < 0) {
		perror("Eroare la citirea din socket");
		exit(errno);
	}
}
