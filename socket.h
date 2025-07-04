#ifndef DEF_SOCKET
#define DEF_SOCKET

#include <openssl/ssl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 7777
#define DEFAULT_PORT_STR "7777"

typedef SSL *Socket;

#endif