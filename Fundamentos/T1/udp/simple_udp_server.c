/*
    UDP server
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUFLEN 512
#define MAX_CLIENTS 5
#define REG_PREFIX "/reg "
#define FILE_PREFIX "/file "
#define DISCONNECT "DISCONNECT"

void die(char *s)
{
    perror(s);
    exit(1);
}

typedef struct {
    struct sockaddr_in addr;
    int addr_len;
    char name[50];
} Client;

Client clients[MAX_CLIENTS];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_client(struct sockaddr_in *client_addr, int addr_len, const char *name){

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; i++) { //registra um novo nome para cliente
        if (clients[i].addr.sin_addr.s_addr == client_addr->sin_addr.s_addr && clients[i].addr.sin_port == client_addr->sin_port) {
            strncpy(clients[i].name, name, sizeof(clients[i].name) - 1);
            clients[i].name[sizeof(clients[i].name) - 1] = '\0';
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }
    
    if (num_clients < MAX_CLIENTS) { //registra informações do novo cliente
        clients[num_clients].addr = *client_addr;
		clients[num_clients].addr_len = addr_len;
        strncpy(clients[num_clients].name, name, sizeof(clients[num_clients].name) - 1);
        clients[num_clients].name[sizeof(clients[num_clients].name) - 1] = '\0';
        num_clients++;
        printf("Cliente conectado: %s:%d\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
    } else {
        printf("Maximo de clientes. Conexão recusada de %s:%d\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(struct sockaddr_in *client_addr){
	
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].addr.sin_addr.s_addr == client_addr->sin_addr.s_addr && clients[i].addr.sin_port == client_addr->sin_port) {
            printf("Cliente desconectado: %s:%d\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
            clients[i] = clients[num_clients - 1];  
            num_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_message(int sock, char *message, int msg_len, struct sockaddr_in *sender_addr){
	
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].addr.sin_addr.s_addr != sender_addr->sin_addr.s_addr || clients[i].addr.sin_port != sender_addr->sin_port) {
            if (sendto(sock, message, msg_len, 0, (struct sockaddr *)&clients[i].addr, clients[i].addr_len) == -1) {
                perror("sendto() error");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

int main(int argc, char **argv){
    struct sockaddr_in si_me, si_other;
    int s, slen = sizeof(si_other), recv_len;
    char buf[BUFLEN], msg[BUFLEN + 50], sender_name[50] = "Stranger";

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return -1;
    }

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        die("socket");

    memset((char *)&si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(atoi(argv[1]));
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
        die("bind");

    printf("Servidor UDP rodando e esperando mensagens...\n");

    while (1) {
        memset(buf, 0, BUFLEN);

        if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *)&si_other, (socklen_t *)&slen)) == -1)
            die("recvfrom()");

        if (strncmp(buf, DISCONNECT, strlen(DISCONNECT)) == 0) {
            remove_client(&si_other);
        } 
        if (strncmp(buf, REG_PREFIX, strlen(REG_PREFIX)) == 0) {
            char *name = buf + strlen(REG_PREFIX);
            add_client(&si_other, slen, name);
            
            pthread_mutex_lock(&clients_mutex);
             for(int i=0; i < num_clients; i++){	
				if(clients[i].addr.sin_addr.s_addr == si_other.sin_addr.s_addr && clients[i].addr.sin_port == si_other.sin_port){
					strncpy(sender_name, clients[i].name, sizeof(sender_name) - 1);
					sender_name[sizeof(sender_name) - 1] = '\0';
					break;
				}
			}
            pthread_mutex_unlock(&clients_mutex);
            continue;
        } else {
            snprintf(msg, sizeof(msg), "%s: %s", sender_name, buf);
            send_message(s, msg, strlen(msg), &si_other);
        }
    }

    close(s);
    return 0;
}
