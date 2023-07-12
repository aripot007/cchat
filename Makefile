CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic
LDFLAGS = 

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

all: client server