#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <time.h>
#include "sniffer.h"

#define MAX_PACKET_SIZE 1500

void get_timestamp(char *buffer, size_t size) { //Função para pegar o tempo atual
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%d/%m/%Y %H:%M", t);
}

// Função para escrever uma entrada de navegação no arquivo HTML
void write_html(FILE *file, const char *timestamp, const char *ip, const char *url) {
    fprintf(file, "<li>%s - %s - <a href=\"%s\">%s</a></li>\n", timestamp, ip, url, url); //Para cada pacote, ele escreve o horario, ip e o url
}

void dns_name(unsigned char *data) { //Pontua as áreas necessárias do endereço web(ex: wwwyoutubecom -> www.youtube.com)
    int read = 0;
    int write = 0;

    while (data[read] != 0) {
        int label_len = data[read++];

        if (write > 0) {
            data[write++] = '.';
        }

        for (int i = 0; i < label_len; i++) {
            data[write++] = data[read++];
        }
    }

    data[write] = '\0';
}

void http_name(unsigned char *data, int len, char *host, int max_len) { // Extrai o valor do campo "Host"
    char *host_prefix = "Host: ";
    char *ptr = (char *)data;

    for (int i = 0; i < len - strlen(host_prefix); i++) {
        if (strncmp(ptr + i, host_prefix, strlen(host_prefix)) == 0) {
            char *host_start = ptr + i + strlen(host_prefix);
            int j = 0;
            while (host_start[j] != '\r' && host_start[j] != '\n' && j < max_len - 1) {
                host[j] = host_start[j];
                j++;
            }
            host[j] = '\0';
            return;
        }
    }
    snprintf(host, max_len, "unknown");
}

void http_path(unsigned char *data, int len, char *path, int max_len) { // Extrai todo o caminho do pacote HTTP
    char *ptr = (char *)data;
    if (strncmp(ptr, "GET ", 4) == 0 || strncmp(ptr, "POST ", 5) == 0) {
        int i = (ptr[0] == 'G') ? 4 : 5;
        int j = 0;

        while (ptr[i] != ' ' && ptr[i] != '\r' && ptr[i] != '\n' && j < max_len - 1) {
            path[j++] = ptr[i++];
        }
        path[j] = '\0';
        return;
    }
    snprintf(path, max_len, "/");
}

int main(int argc, char *argv[]){
	//Receber o IP alvo
	if(argc != 2){
		printf("Usage: %s <Target IP>\n", argv[0]);
		return 1;
	}

	const char *target_ip= argv[1]; //Recebe IP alvo a ser monitorado
	

    int fd_tcp, fd_udp;
    struct sockaddr src_addr;
    socklen_t addr_len = sizeof(src_addr);
    char recv_buffer[MAX_PACKET_SIZE];
    FILE *file = fopen("historico.html", "w");
    
    if(!file){
		perror("file");
		exit(1);
	}
	
	/* Cria um descritor de socket do tipo RAW para o protocolo TCP */
    if((fd_tcp = socket(AF_INET, SOCK_RAW, IPPROTO_TCP)) < 0){ 
        perror("fd");
        exit(1);
    }
    /* Cria um descritor de socket do tipo RAW para o protocolo UDP */
    if((fd_udp = socket(AF_INET, SOCK_RAW, IPPROTO_UDP)) < 0){
        perror("fd");
        exit(1);
    }
	
	fprintf(file, "<html><header><title>Historico de Navegacao</title></header><body><ul>\n"); //Inicio do HTML

    while(1){
		char url[1024];
		char timestamp[32];
		get_timestamp(timestamp, sizeof(timestamp));
		
		/* Recvfrom para pacotes TCP */ 
        if(recvfrom(fd_tcp, (char *)&recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&src_addr, &addr_len) >= 0){ //Recebe pacotes da rede
            struct ip_header *recv_ip = (struct ip_header *)recv_buffer;
            
            if((inet_ntoa(*(struct in_addr *)&recv_ip->saddr)) == target_ip){ //Se o endereço de origem do pacote for do IP alvo
				struct tcp_header *recv_tcp = (struct  tcp_header *)(recv_buffer + sizeof(struct ip_header));
				 
				if(ntohs(recv_tcp->sport) == 80){ //Se porta == 80, protocolo de aplicação é HTTP
					//pega a data do pacote TCP e verifica se é um GET 	
					
					int payload_len = (ntohs(recv_ip->total) - sizeof(struct ip_header) + sizeof(struct tcp_header));
					
					if(payload_len > 0){
						unsigned char *payload = ((unsigned char *)(recv_buffer + sizeof(struct ip_header) + sizeof(struct tcp_header)));
						
						char host[256];
						char path[256];

						http_name(payload, payload_len, host, sizeof(host));
						http_path(payload, payload_len, path, sizeof(path));

						if (strlen(host) > 0) {
							snprintf(url, sizeof(url), "http://%s%s", host, path);
							printf("%s\n", url);
							
							write_html(file, timestamp, target_ip, url);
							//write_html(file, timestamp, inet_ntoa(*(struct in_addr *)&recv_ip->daddr), url);
						}
						
					}
				}
			}
        }
        
        /* Recvfrom para pacotes UDP */ 
        if(recvfrom(fd_udp, (char *)&recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&src_addr, &addr_len) >= 0){
			struct ip_header *recv_ip = (struct ip_header *)recv_buffer;
			struct udp_header *recv_udp = (struct udp_header *)(recv_buffer + sizeof(struct ip_header));

			if((inet_ntoa(*(struct in_addr *)&recv_ip->saddr)) == target_ip){ //Se o endereço de origem do pacote for do IP alvo

				if(ntohs(recv_udp->sport) == 53 || ntohs(recv_udp->dport) == 53){ //Se porta == 53, protocolo de aplicação é DNS
					//pega a data do pacote UDP e pega link apos "https://"
					struct dns_header *recv_dns = (struct dns_header *)(recv_buffer + sizeof(struct udp_header) + sizeof(struct ip_header));
					
					if(ntohs(recv_dns->numRR) >= 1){ //Se for uma respota
						dns_name((unsigned char *)&recv_dns->data); //Pontua a url
						sprintf(url, "http://%s", (unsigned char *)&recv_dns->data); //Coloca o prefixo "http://" antes do dado
						write_html(file, timestamp, target_ip, url); //Escreve no arquivo HTML
						//write_html(file, timestamp, inet_ntoa(*(struct in_addr *)&recv_ip->daddr), url); 
					}	
				}
			}
		}
    }
    
    fprintf(file, "</ul></body></html>\n"); //Fim do HTML
    fclose(file);
    close(fd_tcp);
    close(fd_udp);
    return 0;
}
