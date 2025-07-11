#include <arpa/inet.h>
#include <libnotify/notify.h>
#include <openssl/types.h>
#include <stdint.h>
#include <sys/poll.h>
#include <sys/types.h>
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
#include "packets.h"
#include "client.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

Socket sock;
SSL_CTX *ssl_ctx;
int sock_fd;
uint32_t client_id;

struct client *clients = NULL;

char information_message[1024];
char msg_buff[MAX_MSG_LENGTH + 1];

void print_usage(char *progName) {
    printf("Usage : %s USERNAME HOST [port]\n", progName);
}

Socket init_socket(char *host, char *port) {

    // Create new SSL context    
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Use cchat CA for certificate verification
    if (!SSL_CTX_load_verify_locations(ssl_ctx, "ssl/ca-cert.pem", NULL)) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Abort connection if handshake fails
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

    struct addrinfo *res = NULL;

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol*/

    int err = getaddrinfo(host, port, &hints, &res);
    if(err != 0) {
        fprintf(stderr, "%s\n", gai_strerror(err));
        return NULL;
    }

    struct addrinfo *rp;

    for(rp = res; rp != NULL; rp = rp->ai_next) {

        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sock_fd == -1) continue;

        if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) != -1) break;  /* Success */

        close(sock_fd);

    }

    freeaddrinfo(res);
    if (rp == NULL) {
        fprintf(stderr, "Could not connect to host\n");
        return NULL;
    }

     /* Create client SSL structure using dedicated client socket */
    SSL *ssl = SSL_new(ssl_ctx);
    if (!SSL_set_fd(ssl, sock_fd)) {
        ERR_print_errors_fp(stderr);
        goto exit;
    }

    /* Configure server hostname check */
    if (!SSL_set1_host(ssl, SSL_SERVER_HOSTNAME)) {
        ERR_print_errors_fp(stderr);
        goto exit;
    }
    
    /* Now do SSL connect with server */
    if (SSL_connect(ssl) != 1) {
        
        long verify_result = SSL_get_verify_result(ssl);
        if (verify_result != X509_V_OK) {
            printf("SSL certificate verification failed: %s", X509_verify_cert_error_string(verify_result));
        }
        
        printf("SSL connection to server failed");
        
        ERR_print_errors_fp(stderr);
        exit:
        /* Close up */
        if (ssl != NULL) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
        
        if (sock_fd != -1)
            close(sock_fd);
        return NULL;
    }
    
    return ssl;

}

void close_socket() {
    /* Close up */
    if (sock != NULL) {
        SSL_shutdown(sock);
        SSL_free(sock);
    }
    if (ssl_ctx != NULL) {
        SSL_CTX_free(ssl_ctx);
    }
    
    if (sock_fd != -1)
        close(sock_fd);
}

void send_notification(char *title, char *msg) {
    // Create the notification: (summary, body, icon)
    NotifyNotification *notification = notify_notification_new(
        title,              // Title
        msg, // Body
        NULL    // Icon (can be NULL)
    );

    // Show the notification
    notify_notification_show(notification, NULL);

    // cleanup
    g_object_unref(G_OBJECT(notification));
}

void throw(const char *format, ...) {
    va_list args;
    va_start(args, format);
    destroy_gui();
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    close_socket();
    exit(EXIT_FAILURE);
}

char *get_client_name(uint32_t id) {

    struct client *c = clients;

    while(c != NULL && c->id != id) {
        c = c->next;
    }

    if (c == NULL) {
        print_system_msg("[ERROR] Unknown user id %d", id);
        return "?????";
    }

    return c->name;

}

void handle_client_message() {

    uint32_t id;
    SSL_read(sock, &id, sizeof(uint32_t));
    id = ntohl(id);

    uint32_t len;
    SSL_read(sock, &len, sizeof(uint32_t));
    len = ntohl(len);

    if (len >= sizeof(msg_buff)) {
        close_socket();
        throw("Error while receiving message : packet too large (got %d, buffer is %d)", len, sizeof(msg_buff));
    }

    uint32_t nSSL_read = SSL_read(sock, msg_buff, len);

    if (nSSL_read != len) {
        close_socket();
        throw("Error while receiving message : packet size does not match");
    }    

    print_user_msg("%s : %s", get_client_name(id), msg_buff);

    send_notification(get_client_name(id), msg_buff);

}

void handle_system_message() {

    uint32_t len;
    SSL_read(sock, &len, sizeof(uint32_t));
    len = ntohl(len);

    if (len >= sizeof(msg_buff)) {
        close_socket();
        throw("Error while receiving message : packet too large (got %d, buffer is %d)", len, sizeof(msg_buff));
    }

    uint32_t nSSL_read = SSL_read(sock, msg_buff, len);

    if (nSSL_read != len) {
        close_socket();
        throw("Error while receiving message : packet size does not match");
    }    

    print_user_msg("%s", msg_buff);

    send_notification("CChat", msg_buff);
}

void handle_new_client() {

    struct client *c = malloc(sizeof(struct client));
    SSL_read(sock, &c->id, sizeof(uint32_t));
    c->id = ntohl(c->id);
    
    uint32_t username_len;
    SSL_read(sock, &username_len, sizeof(uint32_t));
    username_len = ntohl(username_len);
    
    c->name = malloc(sizeof(char) * username_len);
    SSL_read(sock, c->name, username_len);
    
    c->next = clients;
    clients = c;

    display_userlist(clients);
    print_system_msg("%s joined the chat !", c->name);
    send_notification(c->name, "joined the chat !");

}

void handle_client_leave() {

    uint32_t id;
    SSL_read(sock, &id, sizeof(uint32_t));
    id = ntohl(id);

    // Remove client from list
    struct client *c = clients;
    struct client *next = c->next;

    if (c->id == id) {
        clients = next;
    } else {

        while(next != NULL && next->id != id) {
            c = next;
            next = next->next;
        }

        if (next == NULL) {
            print_system_msg("[ERROR] Error while removing client %d from list", id);
            return;
        }

        c->next = next->next;
        c = next;

    }

    print_system_msg("%s left the chat !", c->name);
    display_userlist(clients);

    send_notification(c->name, "left the chat !");

    free(c->name);
    free(c);

}

void handle_receive() {

    uint32_t resp;
    size_t nread = 0;

    nread = SSL_read(sock, &resp, sizeof(uint32_t));
    resp = ntohl(resp);

    if (nread != sizeof(uint32_t)) {
        close_socket();
        throw("Error while reading packet number (read %d instead of %d)", nread, sizeof(uint32_t));
    }

    switch (resp) {

        case PA_MSG:
            handle_client_message();
            return;

        case PA_SYS:
            handle_system_message();
            return;
        
        case PA_USRJOIN:
            handle_new_client();
            return;

        case PA_USRLEAVE:
            handle_client_leave();
            return;
        
        default:
            print_system_msg("[ERROR] Wrong packet received : %d. Quitting in 5 seconds ...", resp);
            sleep(5);
            destroy_gui();
            close_socket();
            fprintf(stderr, "Error : wrong packet received.\n");
            exit(EXIT_SUCCESS);
    }

}

int main(int argc, char* argv[]) {

    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *username = argv[1];
    unsigned long username_length = strlen(username);
    if (username_length > MAX_USERNAME_LENGTH || username_length <= 0) {
        fprintf(stderr, "Username too long (max %d characters)\n", MAX_USERNAME_LENGTH);
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

    if (sock == NULL ) {
        return EXIT_FAILURE;
    }

    uint32_t resp;
    SSL_read(sock, &resp, sizeof(uint32_t));
    resp = ntohl(resp);

    if (resp == PA_ERRMAXCONN) {
        printf("Server refused connection : max number of connections reached.");
        return EXIT_SUCCESS;
    }

    if (resp != PA_CONNACCEPT) {
        printf("Error while SSL_reading server response : got packet number %d\n", resp);
        return EXIT_FAILURE;
    }

    // Send username information
    uint32_t username_packet[2] = {htonl(PA_USERNAME), htonl(username_length + 1)};
    SSL_write(sock, username_packet, 2 * sizeof(uint32_t));
    SSL_write(sock, username, username_length + 1);


    // Read server response
    SSL_read(sock, &resp, sizeof(uint32_t));
    resp = ntohl(resp);

    switch (resp) {
        case PA_ERRNAME:
            printf("Error : Username alSSL_ready taken\n");
            return EXIT_FAILURE;
        
        case PA_USERID:
            SSL_read(sock, &client_id, sizeof(uint32_t));
            client_id = ntohl(client_id);
            break;
        
        default:
            printf("Invalid response from server : %d\n", resp);
            return EXIT_FAILURE;
    }

    // Read users list

    SSL_read(sock, &resp, sizeof(uint32_t));
    resp = ntohl(resp);

    if (resp != PA_USRLIST) {
        printf("Error while SSL_reading user list : %d\n", resp);
        return EXIT_FAILURE;
    }

    uint32_t num_clients;
    SSL_read(sock, &num_clients, sizeof(uint32_t));
    num_clients = ntohl(num_clients);

    for (uint32_t i = 0; i < num_clients; i++) {

        uint32_t id;
        uint32_t username_len;

        struct client *c = malloc(sizeof(struct client));

        SSL_read(sock, &id, sizeof(uint32_t));
        SSL_read(sock, &username_len, sizeof(uint32_t));
        id = ntohl(id);
        username_len = ntohl(username_len);

        c->id = id;
        c->name = malloc(sizeof(char) * username_len);
        SSL_read(sock, c->name, username_len);

        c->next = clients;
        clients = c;

    }

    init_gui(MAX_MSG_LENGTH - 1);

    // Init notifications
    if (!notify_init("CChat")) {
        exit(1);
    }

    display_userlist(clients);

    sprintf(information_message, "Connected to %s on port %s as %s !\n", argv[2], (port == -1 ? DEFAULT_PORT_STR : argv[3]), argv[1]);
    print_system_msg(information_message);

    struct pollfd *fds = malloc(sizeof(struct pollfd) * 2);

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sock_fd;
    fds[1].events = POLLIN;

    int poll_res;

    while (true) {

        poll_res = poll(fds, 2, -1);

        if (poll_res < 0 && errno != EINTR) {
            destroy_gui();
            fprintf(stderr, "Error while polling : %d\n", errno);
            close_socket();
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

                uint32_t msg_packet[3] = {htonl(PA_MSG), 0, htonl(msg_len)};
                SSL_write(sock, msg_packet, sizeof(msg_packet));
                SSL_write(sock, msg, msg_len);

                print_user_msg("%s : %s", username, msg);

            }

        }

        // Process received data
        if (fds[1].revents & POLLIN) {

            handle_receive();

        }

    }

    sleep(5);
    destroy_gui();
    notify_uninit();
    return EXIT_SUCCESS;
}