CC = gcc
CFLAGS = -g -Wall -Wextra -Wpedantic -fsanitize=address
LDFLAGS = -pthread

client: client.c gui.o packets.h socket.h common.h
	$(CC) $(CFLAGS) $(shell ncursesw5-config --cflags --libs) -o client gui.o client.c $(shell ncursesw5-config --libs) $(LDFLAGS)

server: server.c packets.h socket.h common.h
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

gui.o: gui.c gui.h
	$(CC) $(CFLAGS) $(shell ncursesw5-config --cflags) -c -o gui.o gui.c $(shell ncursesw5-config --libs) $(LDFLAGS)
	
all: client server