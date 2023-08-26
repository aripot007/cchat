#include <arpa/inet.h>
#include <sys/poll.h>
#include <wchar.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <iconv.h>
#include <stdarg.h>
#include <signal.h>
#include <poll.h>
#include "socket.h"
#include "common.h"
#include "gui.h"

Socket sock;

char information_message[1024];

void print_usage(char *progName) {
    printf("Usage : %s USERNAME HOST [port]\n", progName);
}

Socket init_socket(char *host, char *port) {

    Socket s;

    struct addrinfo *res = NULL;

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol*/

    int err = getaddrinfo(host, port, &hints, &res);
    if(err != 0) {
        fprintf(stderr, "%s\n", gai_strerror(err));
        return -1;
    }

    struct addrinfo *rp;

    for(rp = res; rp != NULL; rp = rp->ai_next) {

        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (s == -1) continue;

        if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1) break;  /* Success */

        close(s);

    }

    freeaddrinfo(res);
    if (rp == NULL) {
        fprintf(stderr, "Could not connect to host\n");
        return -1;
    }

    return s;

}

void throw(const char *format, ...) {
    va_list args;
    va_start(args, format);
    destroy_gui();
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

void handle_receive() {

    char utf8_buff[(MAX_MSG_LENGTH + 1) * UTF8_SEQUENCE_MAXLEN];

    size_t nread = 0;

    nread = read(sock, utf8_buff, sizeof(uint32_t));

    if (nread != sizeof(uint32_t)) {
        close(sock);
        throw("Error while reading packet size (read %d instead of %d)", nread, sizeof(uint32_t));
    }

    uint32_t msg_len = ntohl(*(uint32_t*)(utf8_buff));

    if (msg_len >= sizeof(utf8_buff)) {
        close(sock);
        throw("Error while receiving message : packet too large (got %d, buffer is %d)", msg_len, sizeof(utf8_buff));
    }

    nread = read(sock, utf8_buff, msg_len);

    if (nread != msg_len) {
        close(sock);
        throw("Error while receiving message : packet size does not match");
    }

    print_user_msg(utf8_buff);

}

int main(int argc, char* argv[]) {

    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *username = argv[1];
    unsigned long username_length = strlen(username);
    if (username_length > MAX_USERNAME_LENGTH || username_length <= 0) {
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
    
    sock = init_socket(argv[2], (port == -1 ? DEFAULT_PORT_STR : argv[3]));

    if (sock < 0 ) {
        return EXIT_FAILURE;
    }

    init_gui(MAX_MSG_LENGTH - username_length - 1);

    // Send username information
    char *username_msg = malloc(sizeof(char) * (username_length + 2));
    username_msg[0] = username_length;
    strncpy(&username_msg[1], username, username_length);
    username_msg[username_length + 1] = '\0';
    write(sock, username_msg, sizeof(char) * (username_length + 2));

    sprintf(information_message, "Connected to %s on port %s as %s !\n", argv[2], (port == -1 ? DEFAULT_PORT_STR : argv[3]), argv[1]);
    print_system_msg(information_message);

    struct pollfd *fds = malloc(sizeof(struct pollfd) * 2);

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sock;
    fds[1].events = POLLIN;

    int poll_res;

    while (true) {

        poll_res = poll(fds, 2, -1);

        if (poll_res < 0 && errno != EINTR) {
            destroy_gui();
            fprintf(stderr, "Error while polling : %d\n", errno);
            close(sock);
            return EXIT_FAILURE;
        }

        if (poll_res == 0) continue;

        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            print_system_msg("Connection lost. Quitting in 5 seconds ...");
            sleep(5);
            destroy_gui();
            fprintf(stderr, "Server connection lost.\n");
            return EXIT_SUCCESS;
        }

        // Process user input
        if (fds[0].revents & POLLIN) {

            char *msg = process_input();

            if (msg != NULL) {

                int msg_len = strlen(msg) + 1;

                uint32_t packet_size = htonl(msg_len);
                write(sock, &packet_size, sizeof(uint32_t));
                write(sock, msg, msg_len);

            }

        }

        // Process received data
        if (fds[1].revents & POLLIN) {

            handle_receive();

        }

    }

    sleep(5);
    destroy_gui();
    return EXIT_SUCCESS;
}