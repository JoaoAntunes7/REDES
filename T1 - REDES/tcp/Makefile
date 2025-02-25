CC      = gcc
CFLAGS  = -O2 -Wall

all:
	$(CC) $(CFLAGS) simple_tcp_server_multiclient.c -o simple_tcp_server_multiclient
	$(CC) $(CFLAGS) simple_tcp_client.c -o simple_tcp_client
	
clean:
	-rm -f *.o *~ simple_tcp_server_multiclient simple_tcp_client 
