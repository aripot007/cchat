# Communication Protocol

This document describes the communication protocol used by this program.

# 1. Packets

All packets sent between the client and the server are preceeded by their packet number (`uint32_t`), identifying which type of packet it is.
The packet numbers are defined in `packets.h` and explained in the following sections.

## 1.1 User message (PA_MSG)

This packet corresponds to a message send by a user. It can be sent by both the client and the server.

uint32_t  packet_num
uint32_t  client_id
uint32_t  msg_len
char*     msg

`client_id` MUST correspond to a connected user when sent by the server. Its value is ignored when sent by a client.

`msg` MUST be a null-terminated string of `msg_len` bytes (including the null terminator). The `msg` string MUST NOT contain a newline character (`'\n'`).

## 1.2 System message (PA_SYS)

This packet may be sent by the server to display an informative message to the client (e.g. status about the server).

uint32_t  packet_num
uint32_t  msg_len
char*     msg

`msg` MUST be a null-terminated string of `msg_len` bytes (including the null terminator). The `msg` string MUST NOT contain a newline character (`'\n'`).

## 1.3 Username (PA_USERNAME)

Sent by the client to give the server its client username.

uint32_t  packet_num
uint32_t  username_len
char*     username

`username` MUST be a null-terminated string of `username_len` bytes (including the null terminator). The `username` string MUST NOT contain a newline character (`'\n'`).

## 1.4 User ID (PA_USERID)

Sent back by the server to give a client its id.

uint32_t  packet_num
uint32_t  client_id

## 1.5 Client connection (PA_USRJOIN)
 
Sent by the server to all clients when a new user connects.

uint32_t  packet_num
uint32_t  client_id
uint32_t  username_len
char*     username

`username` MUST be a null-terminated string of `username_len` bytes (including the null terminator). The `username` string MUST NOT contain a newline character (`'\n'`).

## 1.6 Client disconnect (PA_USRLEAVE)

Sent by the server to all clients when a user disconnects.

uint32_t  packet_num
uint32_t  client_id

## 1.7 Connected users list (PA_USRLIST)

Sent by the server to the client on connexion.

uint32_t        packet_num
uint32_t        num_clients
struct client*  clients


struct client:
uint32_t  client_id
uint32_t  username_len
char*     username

`clients` is an array of `num_clients` `struct client` corresponding to all online users, except the one this packet was sent to.

## 1.8 Connection accepted (PA_CONNACCEPT)

Sent by the server to accept a new client connection

uint32_t  packet_num

## 1.9 Error : username already taken (PA_ERRNAME)

Sent back to a client that wants an already used username

uint32_t  packet_num

## 1.10 Error : max number of connections reached (PA_ERRMAXCONN)

Sent by the server when it cannot handle a new connection

uint32_t  packet_num


# 2 - Connection protocol

1. Client opens a connection.
2. Server responds with a `PA_ERRMAXCONN` or a `PA_CONNACCEPT` packet to refuse or accept the connection.
3. Client sends its username with a `PA_USERNAME` packet.
4. If the username is unavailable, server responds with a `PA_ERRNAME` and closes the connection. Otherwise, it attributes an id to the new user and sends it back with a `PA_USERID` packet
5. Server sends the connected users list, except te currently connecting client, with a `PA_USERLIST` packet.
6. Server sends a `PA_USRJOIN` packet to all other connected clients.