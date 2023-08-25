#ifndef DEF_GUI
#define DEF_GUI

// How many characters should be kept from the current buffer when scrolling
#define SCROLL_BUFFER_SIZE 5

// How many messages should be kept in the buffer when they scroll out of the chat window 
#define CHAT_BUFFER_SIZE 50

void init_gui();
void destroy_gui();

/* Get the next message typed by the user, with a maximum length of maxlength, and store it in dest.
   Returns the number of characters read. */
int gui_input(char *dest, int maxlength);

// Print a user message with wide characters in the chat window
void print_user_wmsg(wchar_t *msg);
// Print a user message in the chat window
void print_user_msg(char *msg);

// Print a system message with wide characters to the chat window
void print_system_wmsg(wchar_t *msg);
// Print a system message in the chat window
void print_system_msg(char *msg);

void enable_input();
void disable_input();

#endif