#include <arpa/inet.h>
#include <asm-generic/errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include "socket.h"
#include "common.h"

#define BUFF_SIZE 1024
#define CONN_BACKLOG_SIZE 5
#define MAX_CONNECTIONS 20

const char MAX_CONN_REACHED_MSG[] = "The server reached its maximum number of connections, please try again later.\n";

sem_t available_connections;
pthread_mutex_t clients_lock;
struct client *clients;

void print_usage(char *progName) {
    printf("Usage : %s [port]\n", progName);
}

struct client {
    int id;
    char *name;
    Socket sock;
};

Socket init_socket(int port, int backlog_size) {

    char port_str[6];
    if (port < 0 || port > 65536) {
        fprintf(stderr, "Invalid port %d\n", port);
        exit(EXIT_FAILURE);
    }
    sprintf(port_str, "%d", port);

    struct addrinfo *res = NULL;

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0; /* Any protocol*/
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int err = getaddrinfo(NULL, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "Error while getting address info : %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    struct addrinfo *rp;
    Socket s = -1;

    for(rp = res; rp != NULL; rp = rp->ai_next) {

        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (s == -1) continue;

        if (bind(s, rp->ai_addr, rp->ai_addrlen) == 0) break;  /* Success */
        
        close(s);

    }

    freeaddrinfo(res);
    if (rp == NULL) {
        fprintf(stderr, "Could not connect to host\n");
        exit(EXIT_FAILURE);
    }

    err = listen(s, backlog_size);

    if (err != 0) {
        if (errno == EADDRINUSE) {
            fprintf(stderr, "Error while listening to new connections : port already in use\n");
        } else {
            fprintf(stderr, "Error while listening to new connections");
        }
        close(s);
        exit(errno);
    }

    return s;

}

void broadcast_msg(const char *msg, int from) {

    pthread_mutex_lock(&clients_lock);

    char buff[MAX_MSG_LENGTH + 1];

    if (from >= 0) {
        sprintf(buff, "%.*s : %.*s", MAX_USERNAME_LENGTH, clients[from].name, MAX_MSG_LENGTH - (int)strlen(clients[from].name) - 3, msg);
        printf("[CHAT:%.*s] : %s\n", MAX_USERNAME_LENGTH, clients[from].name, buff);
    } else {
        sprintf(buff, "%.*s", MAX_MSG_LENGTH, msg);
        printf("[CHAT#SYSTEM] : %s\n", buff);
    }

    

    int msg_length = strlen(buff);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].sock < 0 || i == from) continue;
        write(clients[i].sock, buff, msg_length + 1);
    }

    pthread_mutex_unlock(&clients_lock);

}

char *read_username(Socket s) {

    int nread;
    char buff[BUFF_SIZE] = {0};

    nread = read(s, buff, 1);
    if (nread != 1) {
        fprintf(stderr, "Error while reading username length\n");
        return NULL;
    }
    char username_length = (unsigned char) buff[0];

    if (username_length > MAX_USERNAME_LENGTH) {
        username_length = MAX_USERNAME_LENGTH;
    }

    nread = read(s, buff + 1 , sizeof(char) * (username_length + 1));

    if (nread != username_length + 1) {
        fprintf(stderr, "Error while reading username (%d read, username length = %d)\n", nread, username_length);
        return NULL;
    }

    buff[MAX_USERNAME_LENGTH + 1] = '\0';
    char *username = malloc(sizeof(char) * username_length + 1);
    strcpy(username, buff + 1);
    return username;
}

void *handle_client(void *args) {

    struct client client = *(struct client *)(args);

    char buff[MAX_MSG_LENGTH + 1];
    bool overflow = false;  // true if the client sent a message too long to fit into the buffer
    char overflowed_char = '\0';

    int nread = 0;

    while (true) {

        if (overflow) {
            nread = read(client.sock, buff + 1, MAX_MSG_LENGTH);
        } else {
            nread = read(client.sock, buff, MAX_MSG_LENGTH + 1);
        }

        // Stop communicating with the client
        if (nread <= 0) break;

        if (nread == (overflow ? MAX_MSG_LENGTH : MAX_MSG_LENGTH + 1) && buff[MAX_MSG_LENGTH] != '\0') {
            overflow = true;
            overflowed_char = buff[MAX_MSG_LENGTH];
            buff[MAX_MSG_LENGTH] = '\0';
        } else {
            overflow = false;
        }

        broadcast_msg(buff, client.id);

        if (overflow) {
            buff[0] = overflowed_char;
        }
    }

    // Disconnect client
    pthread_mutex_lock(&clients_lock);
    close(client.sock);
    clients[client.id].sock = -1;
    pthread_mutex_unlock(&clients_lock);

    sprintf(buff, "[SYSTEM] %.*s left the chat !", MAX_USERNAME_LENGTH, client.name);
    broadcast_msg(buff, -1);

    sem_post(&available_connections);

    return NULL;

}

int main(int argc, char* argv[]) {

    int err;

    long port = DEFAULT_PORT;
    if (argc >= 2) {
        char *endptr;
        port = strtol(argv[1], &endptr, 10);
        if (endptr == argv[1] || port < 0 || port > 65536) {
            printf("Invalid port number\n");
            return EXIT_FAILURE;
        }
    }

    sem_init(&available_connections, 0, MAX_CONNECTIONS);
    pthread_mutex_init(&clients_lock, NULL);

    Socket s = init_socket(port, CONN_BACKLOG_SIZE);

    err = listen(s, CONN_BACKLOG_SIZE);

    pthread_t *client_threads = malloc(sizeof(pthread_t) * MAX_CONNECTIONS);
    clients = malloc(sizeof(struct client) * MAX_CONNECTIONS);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        clients[i].id = i;
        clients[i].sock = -1;
        clients[i].name = NULL;
    }

    char msg_buff[MAX_MSG_LENGTH + 1];

    while(true) {

        struct sockaddr_in client_addr = {0};
        socklen_t addr_length = sizeof(struct sockaddr_in);

        Socket client_sock = accept(s, (struct sockaddr*)&client_addr, &addr_length);

        if (client_sock == -1) {
            fprintf(stderr, "Error while accepting client connection");
            switch (errno) {
            
                case ECONNABORTED:
                    fprintf(stderr, " : connection aborted\n");
                    break;

                case EMFILE:
                case ENFILE:
                    fprintf(stderr, " : open file descriptors limit reached\n");
                    break;

                case ENOBUFS:
                case ENOMEM:
                    fprintf(stderr, " : not enough memory\n");
                    break;

                case EPERM:
                    fprintf(stderr, " : firewall rules forbid connection\n");
                    break;

                case ETIMEDOUT:
                    fprintf(stderr, " : timed out\n");
                    break;

                default:
                    fprintf(stderr, " : ERRNO %d\n", errno);
                    break;
            }
            continue;
        }

        char addr_buff[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), addr_buff, INET_ADDRSTRLEN);
        printf("New connection from %s : ", addr_buff);

        // Test if new connections are available
        err = sem_trywait(&available_connections);
        if (err != 0) {
            // No new connections available
            write(client_sock, MAX_CONN_REACHED_MSG, strlen(MAX_CONN_REACHED_MSG));
            close(client_sock);
            continue;
        }

        char *username = read_username(client_sock);

        printf("%s\n", username);

        if (username == NULL) {
            sem_post(&available_connections);
            close(client_sock);
            continue;
        }

        pthread_mutex_lock(&clients_lock);

        // Get next available connection id
        int conn_id;
        for (conn_id = 0; conn_id < MAX_CONNECTIONS; conn_id++) {
            if (clients[conn_id].sock == -1) break;
        }

        free(clients[conn_id].name);
        clients[conn_id].name = username;
        clients[conn_id].sock = client_sock;

        pthread_mutex_unlock(&clients_lock);

        sprintf(msg_buff, "[SYSTEM] %.*s joined the chat !", MAX_USERNAME_LENGTH, username);
        broadcast_msg(msg_buff, -1);

        pthread_create(&client_threads[conn_id], NULL, handle_client, &clients[conn_id]);

    }

}