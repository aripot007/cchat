#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include "socket.h"
#include "common.h"

void print_usage(char *progName) {
    printf("Usage : %s USERNAME HOST [port]\n", progName);
}

Socket sock;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void *handle_receive(void *args) {

#pragma GCC diagnostic pop

    char buff[MAX_MSG_LENGTH + 1];
    buff[MAX_MSG_LENGTH] = '\0';

    int nread = 0;

    while (true) {

        nread = read(sock, buff, MAX_MSG_LENGTH);

        // Stop communicating with the server
        if (nread <= 0) break;

        // If the message was too long to fit into the buffer, do not print a newline
        if (nread == MAX_MSG_LENGTH && buff[MAX_MSG_LENGTH - 1] != '\0') {
            printf("%s", buff);
        } else {
            printf("%s\n", buff);
        }
    }

    

    fprintf(stderr, "Connection lost\n");
    close(sock);

    exit(EXIT_SUCCESS);

    return NULL;
}

int main(int argc, char* argv[]) {

    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *username = argv[1];
    unsigned char username_length = strlen(username);
    if (username_length > 32 || username_length <= 0) {
        fprintf(stderr, "Username too long (max 32 characters)\n");
        return EXIT_FAILURE;
    }

    long port = -1;
    if (argc >= 4) {
        char *endptr;
        port = strtol(argv[3], &endptr, 10);
        if (endptr == argv[3] || port < 0 || port > 65536) {
            printf("Invalid port number\n");
            return EXIT_FAILURE;
        }
    }

    struct addrinfo *res = NULL;

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol*/

    int err = getaddrinfo(argv[2], (port == -1 ? DEFAULT_PORT_STR : argv[3]), &hints, &res);
    if(err != 0) {
        fprintf(stderr, "%s\n", gai_strerror(err));
        return EXIT_FAILURE;
    }

    struct addrinfo *rp;

    for(rp = res; rp != NULL; rp = rp->ai_next) {

        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sock == -1) continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1) break;  /* Success */

        close(sock);

    }

    freeaddrinfo(res);
    if (rp == NULL) {
        fprintf(stderr, "Could not connect to host\n");
        return EXIT_FAILURE;
    }

    // Send username information
    char *username_msg = malloc(sizeof(char) * (username_length + 2));
    username_msg[0] = username_length;
    strcpy(&username_msg[1], username);

    write(sock, username_msg, sizeof(char) * (username_length + 2));

    printf("Connected to %s on port %s as %s !\n", argv[2], (port == -1 ? DEFAULT_PORT_STR : argv[3]), argv[1]);

    pthread_t receive_thread;
    pthread_create(&receive_thread, NULL, handle_receive, NULL);

    char buff[MAX_MSG_LENGTH + 1];

    while (true) {

        char *res = fgets(buff, MAX_MSG_LENGTH + 1, stdin);

        if (res == NULL) continue;

        // Remove trailing '\n'
        int len = strlen(buff);
        if (len > 0 && buff[len - 1] == '\n') buff[len - 1] = '\0';

        write(sock, buff, len + 1);

    }

    return EXIT_SUCCESS;
}