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

#define MAX_PACKET_SIZE	1500
#define IP_SOURCE "10.89.0.1" //Selecione o endereço de origem 

struct ip_header{
	uint8_t version;
	uint8_t tos;
	uint16_t total_len;
	uint16_t id;
	uint16_t frag_off;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t checksum;
	uint32_t saddr;
	uint32_t daddr;
};

struct icmp_header {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint16_t id;
	uint16_t sequence;
	uint32_t data;
};

uint16_t checksum_cal(void *buffer, int len){
	uint16_t *buf = buffer;
	uint32_t sum = 0;
	uint16_t result;
	
	for(sum = 0; len > 1; len -= 2){
		sum += *buf++;
	}
	if(len == 1){
		sum += *(uint8_t *)buf;
	}
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	result = ~sum;
	return result;
}

void cidr(const char *cidr, uint32_t *base_ip, uint32_t *mask){
	char ip[32];
	int network_num;
	
	sscanf(cidr, "%[^/]/%d", ip, &network_num); //Separa endereço IP e número de bits de rede
	
	inet_pton(AF_INET, ip, base_ip);
	*mask = htonl((0XFFFFFFFF << (32 - network_num)) & 0xFFFFFFFF); //Exemplo: 32-24 = 8bits 
																    //0XFFFFFFFF << 8 -> 0XFFFFFF00 & 0xFFFFFFFF = 255.255.255.0
	*base_ip &= *mask;  //Exemplo: 192.168.0.197 & 255.255.255.0 = 192.168.0.0								   
}


int main(int argc, char *argv[]){
	if(argc != 3){
		printf("Usage: %s <rede/máscara> <timeout_ms>\n", argv[0]);
		return 1;
	}
	
	const char *network_ip = argv[1];
	int timeout_ms = atoi(argv[2]);

	int fd, recv_fd;
	int active_hosts = 1;
	
	//Para o sendto
	struct sockaddr_in daddr;
	char packet[MAX_PACKET_SIZE];
	int total_len;//short
	
	//Para o recvfrom
	struct sockaddr_in src_addr;
	socklen_t addr_len = sizeof(src_addr);
	char recv_buffer[MAX_PACKET_SIZE];

	struct ip_header *ip = (struct ip_header *)packet;  
	struct icmp_header *icmp = (struct icmp_header *)(packet + sizeof(struct ip_header));

	uint32_t base_ip, mask;
	cidr(network_ip, &base_ip, &mask);
	uint32_t num_hosts = ~ntohl(mask) - 1; //número de hosts na rede = ~(255.255.255.0) - 2 = 0.0.0.255 - 2 = 255 - 2 = 253
	
	/* Cria um descritor de socket do tipo RAW para o protocolo IP */
	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		perror("socket");
		exit(1);
	}
	/* Cria um descritor de socket do tipo RAW para o protocolo ICMP */
	if ((recv_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
		perror("socket");
		exit(1);
	}

	/* Esperar ICMP reply - Suporte a timeout no kernel */
	struct timeval timeout; 
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000; //Pega unidade e dezena do timeout em segundos
	if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){ //Define tempo de timeout para o pacote
		perror("setsocket");
		exit(1);
	}
	if(setsockopt(recv_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){ //Define tempo de timeout para o pacote
		perror("setsocket");
		exit(1);
	}
	
	printf("Varredura na rede %s com timeout de %d ms...\n", network_ip, timeout_ms);

	/* Preenche struct do endereco de destino */
	for(uint32_t i = 1; i <= num_hosts; i++){ //Faz a varredura pelo número de hosts da rede
		
		uint32_t target_ip = htonl(ntohl(base_ip) + i); //Incrementa o endereço IP da rede

		daddr.sin_family = AF_INET;
		daddr.sin_addr.s_addr = target_ip;
		
		memset(packet, 0, sizeof(packet)); //Zera pacote toda iteração

		/* Preenche o campo de dados */
		total_len = sizeof(struct ip_header) + sizeof(struct icmp_header);

		/* Preenche o cabecalho IP */
		ip->version = 0x45;     /* IHL + Version (0x05 + 0x40) */
		ip->tos = 0;			/* Type of Service (Tos) */
		ip->total_len = htons(total_len);	/* Total Length */
		ip->id = htons(54321);   /* Identification */ //Aleatório
		ip->frag_off = 0;		/* Fragment Offset and Flags */
		ip->ttl = 64;			/* Time To Live (TTL) */
		ip->protocol = 1;		/* Protocol */ //ICMP = 1
		ip->checksum = 0;		/* Header Checksum */
		ip->checksum = checksum_cal(ip, sizeof(struct ip_header)); //Checksum calculado			
		ip->saddr = inet_addr(IP_SOURCE); /* Source IP Address */ 
		ip->daddr = daddr.sin_addr.s_addr;	/* Destination IP Address */

		/* Preenche o cabecalho ICMP */
		icmp->type = 8; //8 = request e 0 = reply
		icmp->code = 0; //code = 0 para tipo request e reply
		icmp->id = htons(1234); //id aleatório
		icmp->sequence = active_hosts;   //sequência sendo incrementado para cada request
		icmp->checksum = checksum_cal(icmp, sizeof(struct icmp_header));
		icmp->data = 0; //Usado para dados extras
		
		/* Envia o pacote */
		if (sendto(fd, (char *)packet, total_len, 0, 
			(struct sockaddr *)&daddr, (socklen_t)sizeof(daddr)) < 0) {
			perror("sendto");
			continue;
		}

		memset(recv_buffer, 0, sizeof(recv_buffer));
		
		/* Recebe ICMP reply */ 
		if(recvfrom(recv_fd, (char *)&recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&src_addr, &addr_len) >= 0) {
			struct icmp_header *recv_icmp = (struct icmp_header *)(recv_buffer + sizeof(struct ip_header));
			
			if(recv_icmp->type == 0 && recv_icmp->id == icmp->id){ //type para reply e mesmo id do src
				printf("Host ativo: %s\n", inet_ntoa(*(struct in_addr *)&ip->daddr));
				active_hosts++;
			} else { //Se type = 3 (DESTINATION UNREACHABLE) ou outros
				printf("Host inativo: %s\n", inet_ntoa(*(struct in_addr *)&ip->daddr));
			}

		} else{ 
			printf("Host inativo: %s\n", inet_ntoa(*(struct in_addr *)&ip->daddr));
		}
	}
	active_hosts--;
	printf("Total de hosts ativos: %d\n", active_hosts);
	close(fd);
	close(recv_fd);
	return 0;
}
