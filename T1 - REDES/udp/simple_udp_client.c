/*
    Simple UDP client 
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

#define BUFLEN 512
#define DISCONNECT "DISCONNECT"
#define FILE_PREFIX "/file "
#define REG_PREFIX "/reg "
#define QUIT_PREFIX "/quit"

void die(char *s)
{
    perror(s);
    exit(1);
}

int main(int argc, char **argv)
{
    struct sockaddr_in si_other;
    int s, slen = sizeof(si_other);
    char buf[BUFLEN], command[50];
    fd_set readfds;

    if (argc != 3) {
        printf("Usage: %s <server ip> <port>\n", argv[0]);
        return -1;
    }

    // Cria socket UDP
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        die("socket");

    memset((char *)&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(atoi(argv[2]));

    if (inet_aton(argv[1], &si_other.sin_addr) == 0)
        die("inet_aton() falhou");

    printf("Registre-se com /reg <nome>\n");

    fgets(command, sizeof(command), stdin);
    if (strncmp(command, REG_PREFIX, strlen(REG_PREFIX)) == 0) {
        sendto(s, command, strlen(command), 0, (struct sockaddr *)&si_other, slen);
    }

    printf("CONECTADO AO CHAT\nComandos do chat:\n/reg para registrar um nome\n/file <nome do arquivo.txt> para enviar um arquivo de texto\n/quit para desconectar-se\n\n");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(0, &readfds); // para envio
        FD_SET(s, &readfds); // e para receber
        
        if (select(s + 1, &readfds, NULL, NULL, NULL) == -1)
			die("select");	
		
        if (FD_ISSET(0, &readfds)) {
            memset(buf, 0, BUFLEN);
            fgets(buf, BUFLEN, stdin);
		
            if (strncmp(buf, QUIT_PREFIX, strlen(QUIT_PREFIX)) == 0) {
                sendto(s, DISCONNECT, strlen(DISCONNECT), 0, (struct sockaddr *)&si_other, slen);
                break;
            } else if (strncmp(buf, FILE_PREFIX, strlen(FILE_PREFIX)) == 0) {
                char *filename = buf + strlen(FILE_PREFIX);
                FILE *file = fopen(filename, "rb");
                ssize_t	n;
                printf("%s", filename);

                if (file == NULL) {
                    printf("Erro ao abrir o arquivo: %s\n", filename);
                } else {
                    while ((n = fread(buf, 1, BUFLEN, file)) > 0) {
                        sendto(s, buf, strlen(buf), 0, (struct sockaddr *)&si_other, slen);
					}
                    fclose(file);
                    printf("Arquvio '%s' enviado \n", filename);
                }
            } else {
                sendto(s, buf, strlen(buf), 0, (struct sockaddr *)&si_other, slen);
            }
        }

        if (FD_ISSET(s, &readfds)) {
            memset(buf, 0, BUFLEN);
            if (recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *)&si_other, (socklen_t *)&slen) == -1)
                die("recvfrom");
            printf("%s", buf);
        }
    }
    close(s);
    return 0;
}
