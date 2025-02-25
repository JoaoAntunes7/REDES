/*
	Simple tcp server (connect to multiple clients)
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include <signal.h>
#include <sys/wait.h>
 
#define BUFLEN	1024	//Max length of buffer
#define MAX_CLIENTS 5
#define REG_PREFIX "/reg "
#define DISCONNECT "DISCONNECT"

void die(char *s)
{
	perror(s);
	exit(1);
}

typedef struct{ //struct pro client
	struct sockaddr_in addr; 
	int sock;
	char name[50];
} Client;


Client clients[MAX_CLIENTS];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER; 


void send_message(int socket, char *msg){
	
	pthread_mutex_lock(&clients_mutex);
	for(int i=0; i<num_clients; i++){
		if(clients[i].sock != socket){
			if(write(clients[i].sock, msg, strlen(msg)) < 0){ 
				perror("write");
				close(clients[i].sock);
				clients[i].sock = -1; //desconecta cliente
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

void register_client(int socket, const char *name){
	
	pthread_mutex_lock(&clients_mutex);
	for(int i=0; i<num_clients; i++){
		if(clients[i].sock == socket){
			strncpy(clients[i].name, name, sizeof(clients[i].name) - 1);
			clients[i].name[sizeof(clients[i].name) - 1] = '\0'; //determina o fim do char
			break;
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int socket){ //remove cliente especifico
	
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].sock == socket) {
            printf("Cliente desconectado: %s:%d\n", inet_ntoa(clients[i].addr.sin_addr), ntohs(clients[i].addr.sin_port));
            clients[i] = clients[num_clients - 1];  // Substitui cliente removido pelo último da lista
            num_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *client_main(void *socket_alloc){
	int socket = *(int *)socket_alloc;
	char buf[BUFLEN];
	int recv_len;
	char sender_name[50] = "Stranger"; //nome padrão do cliente
		
	while(1){
		memset(buf, 0, sizeof(buf));
		
		recv_len = read(socket, buf, BUFLEN);
		if(recv_len <= 0){
			close(socket);
			break;
		}

		if (strncmp(buf, DISCONNECT, strlen(DISCONNECT)) == 0) { //Se comando "/quit" desconecta cliente
            remove_client(socket);
            break;
		}
		if (strncmp(buf, REG_PREFIX, strlen(REG_PREFIX)) == 0) {  //Se comando "/reg" registra o nome do cliente
            char *name = buf + strlen(REG_PREFIX);
            register_client(socket, name);
            
			pthread_mutex_lock(&clients_mutex);
            for(int i=0; i < num_clients; i++){	
				if(clients[i].sock == socket){
					strncpy(sender_name, clients[i].name, sizeof(sender_name) - 1);
					sender_name[sizeof(sender_name) - 1] = '\0';
					break;
				}
			}
			pthread_mutex_unlock(&clients_mutex);
            continue; 
		}
		
		char full_msg[BUFLEN + 50];
		snprintf(full_msg, sizeof(full_msg), "%s: %s\n", sender_name, buf); //mensagem inteira (nome + mensagem)
		send_message(socket, full_msg);
	}
	
	free(socket_alloc);	//desaloca o socket
	return 0;
}


int main(int argc, char **argv){
	
	struct sockaddr_in si_me, si_other;
	int s, slen = sizeof(si_other), conn, portno;
	pthread_t thread_id;
	
	/* check command line arguments */
	if (argc != 2) {
		fprintf(stderr,"usage: %s <port>\n", argv[0]);
		exit(0);
	}
     
	/* create a TCP socket */
	if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
		die("socket");
    
	/* zero out the structure */
	memset((char *) &si_me, 0, sizeof(si_me));
	portno = atoi(argv[1]);
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(portno);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
     
	/* bind socket to port */
	if (bind(s, (struct sockaddr*)&si_me, sizeof(si_me)) == -1)
		die("bind");
	
	/* allow 5 requests to queue up */ 
	if (listen(s, 5) == -1)
		die("listen");
		
	/* keep listening for data */
	while (1) {
		conn = accept(s, (struct sockaddr *) &si_other, (socklen_t *)&slen);
		if (conn < 0)
			die("accept");
			
		pthread_mutex_lock(&clients_mutex);
		if(num_clients < MAX_CLIENTS){
			clients[num_clients].sock = conn;
			clients[num_clients].addr.sin_addr.s_addr = si_other.sin_addr.s_addr;
			clients[num_clients++].addr.sin_port = si_other.sin_port;					//salva valores do ip e do socket na struct cliente
			
			printf("Cliente conectado: %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
			pthread_mutex_unlock(&clients_mutex);
	
			int *socket_alloc = malloc(sizeof(int)); //alocação do socket
			*socket_alloc = conn;
			
			if(pthread_create(&thread_id, NULL, client_main, (void *)socket_alloc) < 0){ //cria uma thread para cuidar de cada cliente
				die("pthread_create");
			}
		} else {
			printf("Maximo de clientes. Conexão recusada de %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
			close(conn);
			pthread_mutex_unlock(&clients_mutex);
		}
		pthread_detach(thread_id);
	}
	close(s);
	return 0;
}

	
