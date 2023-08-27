#ifndef DEF_PACKETS
#define DEF_PACKETS

#define PA_MSG          0   // User message
#define PA_SYS          1   // System message
#define PA_USERNAME     20  // Username
#define PA_USERID       21  // User id
#define PA_USRJOIN      22  // User connect
#define PA_USRLEAVE     23  // User disconnect
#define PA_USRLIST      24  // User list
#define PA_CONNACCEPT   40  // Connection accepted
#define PA_ERRNAME      50  // Error : username already taken
#define PA_ERRMAXCONN   51  // Error : max number of connections reached

#endif