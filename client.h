#ifndef DEF_CLIENT
#define DEF_CLIENT

#include <stdint.h>

// Linked list of clients
struct client {
    uint32_t id;
    char *name;
    struct client *next;
};

#endif