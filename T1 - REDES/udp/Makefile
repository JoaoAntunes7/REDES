CC      = gcc
CFLAGS  = -O2 -Wall

all:
	$(CC) $(CFLAGS) simple_udp_server.c -o simple_udp_server
	$(CC) $(CFLAGS) simple_udp_client.c -o simple_udp_client
clean:
	-rm -f *.o *~ simple_udp_server simple_udp_client 
