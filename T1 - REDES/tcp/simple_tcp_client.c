/* 
	simple TCP client
	usage: tcpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#define BUFSIZE 512
#define FILE_PREFIX "/file "
#define REG_PREFIX "/reg "
#define QUIT_PREFIX "/quit"
#define DISCONNECT "DISCONNECT"

void die(char *s)
{
    perror(s);
    exit(1);
}

void *recieve_message(void *sockfd_ptr){ 
    int socket = *(int *)sockfd_ptr;
    char buf[BUFSIZE];
    int n;
    
    while(1){
		memset(buf, 0, BUFSIZE);
		n = read(socket, buf, BUFSIZE);
		if (n <= 0){
			close(socket);
			exit(0);
		}
		printf("%s", buf);	
	}
	return 0;
}

void send_file(int sockfd, const char *filename){
    char buf[BUFSIZE];
    FILE *file = fopen(filename, "rb");
    size_t n;
    
    if (file == NULL) {
        printf("Ocorreu um erro ao enviar o arquivo '%s'\n", filename);
        return;
    }

    while ((n = fread(buf, 1, BUFSIZE, file)) > 0) {
        if (write(sockfd, buf, n) < 0) {
            die("write");
        }
    }

    fclose(file);
    printf("Arquivo '%s' enviado.\n", filename);
}

int main(int argc, char **argv){
    int sockfd, portno;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];
    pthread_t recieve_thread;

    /* check command line arguments */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <hostname> <port>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        die("socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL)
        die("gethostbyname");

    /* build the server's Internet address */
    memset((char *)&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    if (inet_aton(hostname, &serveraddr.sin_addr) == 0)
        die("inet_aton");

    /* connect: create a connection with the server */
    if (connect(sockfd, (const struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
        die("connect");

    printf("CONECTADO AO CHAT\nComandos do chat:\n/reg para registrar um nome\n/file <nome do arquivo.txt> para enviar um arquivo de texto\n/quit para desconectar-se\n\n");
   
    if(pthread_create(&recieve_thread, NULL, recieve_message, &sockfd) != 0) //cria thread para receber as mensagem do server
		die("pthread_create");

    while (1) {
        fflush(stdout);
        
        memset(buf, 0, BUFSIZE);
        fgets(buf, BUFSIZE - 1, stdin);
        buf[strcspn(buf, "\n")] = 0;

		if (strncmp(buf, QUIT_PREFIX, strlen(QUIT_PREFIX)) == 0) {
		    if (write(sockfd, DISCONNECT, strlen(DISCONNECT)) < 0) 
			    die("write");
			break;
        }
		if(strncmp(buf, FILE_PREFIX, strlen(FILE_PREFIX)) == 0){
            char *filename = buf + strlen(FILE_PREFIX);
			if (strlen(filename) > 0) {
                send_file(sockfd, filename);
			} 
		} else {
			if (write(sockfd, buf, strlen(buf)) < 0) 
				die("write");
        } 
    }
    close(sockfd);
    return 0;
}
