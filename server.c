#include <arpa/inet.h>
#include <asm-generic/errno.h>
#include <bits/pthreadtypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include "socket.h"
#include "common.h"
#include "packets.h"

#define BUFF_SIZE 1024
#define CONN_BACKLOG_SIZE 5
#define MAX_CONNECTIONS 20

const char MAX_CONN_REACHED_MSG[] = "The server reached its maximum number of connections, please try again later.\n";

sem_t available_connections;
int nb_clients = 0;
pthread_mutex_t clients_lock;
struct client *clients;
pthread_t *client_threads;

SSL_CTX *ssl_ctx = NULL;

void print_usage(char *progName) {
    printf("Usage : %s [port]\n", progName);
}

struct client {
    int id;
    char *name;
    Socket sock;
    int sock_fd;
};

int init_socket(int port, int backlog_size) {

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
    int s = -1;

    // Create new SSL context    
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (ssl_ctx == NULL) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Set the key and cert used to authenticate
    if (SSL_CTX_use_certificate_chain_file(ssl_ctx, "ssl/server-cert.pem") <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, "ssl/server-key.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Use scantor CA for certificate verification
    if (!SSL_CTX_load_verify_locations(ssl_ctx, "ssl/ca-cert.pem", NULL)) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Abort connection if handshake fails
    // SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);

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

void close_socket(int sock_fd, SSL *ssl) {
    if (ssl != NULL) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (sock_fd >= 0) {
        close(sock_fd);
    }
}

// Broadcast a message to all connected users
void broadcast_msg(const char *buff, int from) {

    int len = strlen(buff) + 1;

    uint32_t packet[3] = {htonl(PA_MSG), htonl(from), htonl(len)};

    pthread_mutex_lock(&clients_lock);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].sock == NULL || i == from) continue;
        SSL_write(clients[i].sock, packet, sizeof(packet));
        SSL_write(clients[i].sock, buff, len);
    }

    pthread_mutex_unlock(&clients_lock);

}

void broadcast_join_message(int client_id) {

    int username_len = strlen(clients[client_id].name) + 1;
    uint32_t packet[3] = {htonl(PA_USRJOIN), htonl(client_id), htonl(username_len)};

    pthread_mutex_lock(&clients_lock);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (i == client_id || clients[i].sock == NULL) continue;
        SSL_write(clients[i].sock, &packet, sizeof(packet));
        SSL_write(clients[i].sock, clients[client_id].name, username_len);
    }

    pthread_mutex_unlock(&clients_lock);

}

void broadcast_leave_message(uint32_t client_id) {

    uint32_t packet[2] = {htonl(PA_USRLEAVE), htonl(client_id)};

    pthread_mutex_lock(&clients_lock);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].sock == NULL) continue;
        SSL_write(clients[i].sock, &packet, sizeof(packet));
    }

    pthread_mutex_unlock(&clients_lock);

}

char *read_username(Socket s) {

    int nread;
    char buff[BUFF_SIZE] = {0};
    uint32_t pa_num;
    uint32_t username_len;

    nread = SSL_read(s, &pa_num, sizeof(uint32_t));
    pa_num = ntohl(pa_num);

    if (nread != sizeof(uint32_t) || pa_num != PA_USERNAME) {
        fprintf(stderr, "Error while SSL_reading username packet\n");
        return NULL;
    }

    SSL_read(s, &username_len, sizeof(uint32_t));
    username_len = ntohl(username_len);

    if (username_len > MAX_USERNAME_LENGTH) {
        username_len = MAX_USERNAME_LENGTH;
    }

    nread = SSL_read(s, buff , sizeof(char) * username_len);

    if (nread != (int) username_len) {
        fprintf(stderr, "Error while SSL_reading username (%d SSL_read, username length = %d)\n", nread, username_len);
        return NULL;
    }

    buff[MAX_USERNAME_LENGTH + 1] = '\0';
    char *username = malloc(sizeof(char) * username_len);
    strcpy(username, buff);
    return username;
}

/*
 * Remove a client from the list of clients. If `cancel_thread` is true, cancel the thread used to handle the client.
 * If this is called from the client handler thread, `cancel_thread` should be `false`.
 */
void remove_client(struct client client, bool cancel_thread) {

    pthread_mutex_lock(&clients_lock);

    close_socket(clients[client.id].sock_fd, clients[client.id].sock);
    clients[client.id].sock_fd = -1;
    clients[client.id].sock = NULL;

    if (cancel_thread) {
        pthread_cancel(client_threads[client.id]);
    }

    nb_clients--;

    pthread_mutex_unlock(&clients_lock);

    broadcast_leave_message(client.id);

    sem_post(&available_connections);
}

void *handle_client(void *args) {

    struct client client = *(struct client *)(args);

    char buff[MAX_MSG_LENGTH + 1];

    int nread = 0;
    uint32_t pa_num;
    uint32_t msglen;

    while (true) {

        nread = SSL_read(client.sock, &pa_num, sizeof(uint32_t));

        // Stop communicating with the client
        if (nread <= 0) break;

        pa_num = ntohl(pa_num);

        if (nread != sizeof(uint32_t) || pa_num != PA_MSG) {
            printf("[ERROR] Invalid packet %d from '%s', closing connection.\n", pa_num, client.name);
            remove_client(client, true);
            return NULL;
        }

        // Ignore client_id
        SSL_read(client.sock, &pa_num, sizeof(uint32_t));

        nread = SSL_read(client.sock, &msglen, sizeof(uint32_t));
        msglen = ntohl(msglen);

        if (nread < (int) sizeof(uint32_t)) {
            printf("[ERROR] Invalid packet from '%s', closing connection.\n", client.name);
            remove_client(client, true);
            return NULL;
        }

        if (msglen > sizeof(buff)) {
            printf("[ERROR] Error SSL_reading packet from '%s' : packet too large\n", client.name);
            remove_client(client, true);
            return NULL;
        }

        nread = SSL_read(client.sock, buff, msglen);

        // Stop communicating with the client
        if (nread <= 0) break;

        broadcast_msg(buff, client.id);

    } 

    // Disconnect client
    remove_client(client, false);

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

    int s = init_socket(port, CONN_BACKLOG_SIZE);

    err = listen(s, CONN_BACKLOG_SIZE);

    client_threads = malloc(sizeof(pthread_t) * MAX_CONNECTIONS);
    clients = malloc(sizeof(struct client) * MAX_CONNECTIONS);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        clients[i].id = i;
        clients[i].sock_fd = -1;
        clients[i].sock = NULL;
        clients[i].name = NULL;
    }

    while(true) {

        struct sockaddr_in client_addr = {0};
        socklen_t addr_length = sizeof(struct sockaddr_in);

        int client_sock_fd = accept(s, (struct sockaddr*)&client_addr, &addr_length);

        if (client_sock_fd == -1) {
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

        /* Create server SSL structure using newly accepted client socket */
        SSL *client_sock = SSL_new(ssl_ctx);
        if (!SSL_set_fd(client_sock, client_sock_fd)) {
            ERR_print_errors_fp(stderr);
            exit(EXIT_FAILURE);
        }
        
        /* Wait for SSL connection from the client */
        if (SSL_accept(client_sock) <= 0) {
            ERR_print_errors_fp(stderr);
            close(client_sock_fd);
            continue;
        }

        char addr_buff[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), addr_buff, INET_ADDRSTRLEN);
        printf("New connection from %s : ", addr_buff);

        uint32_t pa_num;

        // Test if new connections are available
        err = sem_trywait(&available_connections);
        if (err != 0) {
            // No new connections available
            pa_num = htonl(PA_ERRMAXCONN);
            SSL_write(client_sock, &pa_num, sizeof(uint32_t));
            close_socket(client_sock_fd, client_sock);
            continue;
        }

        // Accept connection
        pa_num = htonl(PA_CONNACCEPT);
        SSL_write(client_sock, &pa_num, sizeof(uint32_t));

        char *username = read_username(client_sock);

        printf("%s\n", username);

        if (username == NULL) {
            sem_post(&available_connections);
            pa_num = htonl(PA_ERRNAME);
            SSL_write(client_sock, &pa_num, sizeof(uint32_t));
            close_socket(client_sock_fd, client_sock);
            continue;
        }

        pthread_mutex_lock(&clients_lock);

        // Get next available connection id and check if username is available
        int conn_id;
        for (conn_id = 0; conn_id < MAX_CONNECTIONS; conn_id++) {
            if (clients[conn_id].sock == NULL) break;
        }

        bool username_ok = true;
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (clients[i].sock != NULL && !strcmp(clients[i].name, username)) {
                username_ok = false;
                break;
            }
        }

        pthread_mutex_unlock(&clients_lock);

        if (!username_ok) {
            sem_post(&available_connections);
            pa_num = htonl(PA_ERRNAME);
            SSL_write(client_sock, &pa_num, sizeof(uint32_t));
            close_socket(client_sock_fd, client_sock);
            continue;
        }

        // Send client id
        uint32_t id_packet[2] = {htonl(PA_USERID), htonl(conn_id)};
        SSL_write(client_sock, id_packet, sizeof(id_packet));

        // Send client list

        pthread_mutex_lock(&clients_lock);

        pa_num = htonl(PA_USRLIST);
        uint32_t num_clients = htonl(nb_clients);
        SSL_write(client_sock, &pa_num, sizeof(uint32_t));
        SSL_write(client_sock, &num_clients, sizeof(uint32_t));

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            
            if (clients[i].sock == NULL) continue;

            uint32_t usr_packet[2] = {htonl(clients[i].id), htonl(strlen(clients[i].name) + 1)};
            SSL_write(client_sock, usr_packet, sizeof(uint32_t) * 2);
            SSL_write(client_sock, clients[i].name, strlen(clients[i].name) + 1);

        }

        // Add client to list;
        free(clients[conn_id].name);
        clients[conn_id].name = username;
        clients[conn_id].sock = client_sock;
        clients[conn_id].sock_fd = client_sock_fd;
        nb_clients++;

        pthread_mutex_unlock(&clients_lock);

        pthread_create(&client_threads[conn_id], NULL, handle_client, &clients[conn_id]);
        
        broadcast_join_message(conn_id);

    }

}