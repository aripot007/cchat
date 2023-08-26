#ifndef DEF_GUI
#define DEF_GUI

// How many characters should be kept from the current buffer when scrolling
#define SCROLL_BUFFER_SIZE 5

// How many messages should be kept in the buffer when they scroll out of the chat window 
#define CHAT_BUFFER_SIZE 50

void init_gui();
void destroy_gui();

/*
 * Processes input from the user. If the user submitted its input by pressing ENTER, returns a buffer containing the message, otherwise returns NULL.
*/
char *process_input();

// Print a user message in the chat window
void print_user_msg(char *msg);

// Print a system message in the chat window
void print_system_msg(char *msg);

#endif