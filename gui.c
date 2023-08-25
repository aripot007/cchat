#include <stdbool.h>
#include <sys/cdefs.h>
#include <ncurses.h>
#include <curses.h>
#include <wchar.h>
#include <locale.h>
#include <string.h>
#include <unctrl.h>
#include <stdlib.h>
#include "gui.h"
#include "common.h"

#include <time.h>

WINDOW *chat_win, *input_win, *users_win;

wchar_t *buf_left = NULL;
wchar_t *buf_right = NULL;
int length_right = 0;
int length_left = 0;
int capacity = MAX_MSG_LENGTH + 1;

int cursor_pos = 0;
int cursor_block = 0;

bool input_enabled = true;

void init_zipper(int max_length) {
    free(buf_left);
    free(buf_right);
    buf_right = malloc(sizeof(wchar_t) * (max_length + 1));
    buf_left = malloc(sizeof(wchar_t) * (max_length + 1));
    for (int i = 0; i < max_length + 1; i++) {
        buf_left[i] = '\0';
    }
    buf_right[max_length] = '\0';
    capacity = max_length;
    length_left = 0;
    length_right = 0;
}

// Move the zipper cursor one char to the left
void zip_move_left() {
    if (length_left == 0) return;

    length_left--;
    buf_right[capacity - length_right - 1] = buf_left[length_left];
    buf_left[length_left] = '\0';
    length_right++;
}

// Move the zipper cursor one char to the left
void zip_move_right() {
    if (length_right == 0) return;

    length_right--;
    buf_left[length_left] = buf_right[capacity - length_right - 1];
    length_left++;
}

// Add a character at the current position
void zip_add_char(int ch) {
    if (length_left + length_right >= capacity) return;
    buf_left[length_left] = ch;
    length_left++;
}

// Delete a character at the current position
void zip_del_char() {
    if (length_left == 0) return;
    length_left--;
    buf_left[length_left] = '\0';
}

// Delete a character at the current position from the right
void zip_del_char_right() {
    if (length_right == 0) return;
    length_right--;
}

void zip_clear() {
    for (int i = 0; i < capacity + 1; i++) {
        buf_left[i] = '\0';
    }
    length_left = 0;
    length_right = 0;
    cursor_pos = 0;
    cursor_block = 0;
}

// Display the zipper un the input field
void display_zipper() {

    if (cursor_pos == 0 && cursor_block > 0) {
        // Scroll to the left
        mvwprintw(input_win, 0, 0, "%-*s", COLS - 2, "");
        cursor_pos = COLS - 3 - SCROLL_BUFFER_SIZE;
        cursor_block--;

    } else if (cursor_pos == COLS - 2) {
        // Scroll to the right
        mvwprintw(input_win, 0, 0, "%-*s", COLS - 2, "");
        cursor_pos = SCROLL_BUFFER_SIZE + 1;
        cursor_block++;
    }

    if (cursor_block == 0) {
        // No scroll to the left
        mvwprintw(input_win, 0, 0, "%ls%.*ls ", buf_left, COLS - 3 - length_left, buf_right + (capacity - length_right));

        if (length_right > COLS - 2 - length_left) {
            // Text overflows to right
            wattron(input_win, A_REVERSE);
            mvwprintw(input_win, 0, COLS - 3, ">");
            wattroff(input_win, A_REVERSE);
        }

    } else {
    
        // Number of chars to skip in the left buffer
        int nb_skip_left = 1 + cursor_block * (COLS - 3 - SCROLL_BUFFER_SIZE);

        wattron(input_win, A_REVERSE);
        mvwprintw(input_win, 0, 0, "<");
        wattroff(input_win, A_REVERSE);
        wprintw(input_win, "%ls%.*ls ", buf_left + nb_skip_left, COLS - 3 - (length_left - nb_skip_left), buf_right + (capacity - length_right));

        if (length_right > COLS - 2 - (length_left - nb_skip_left)) {
            // Text overflows to right
            wattron(input_win, A_REVERSE);
            mvwprintw(input_win, 0, COLS - 3, ">");
            wattroff(input_win, A_REVERSE);
        }

    }
    
    wmove(input_win, 0, cursor_pos);
    wrefresh(input_win);
}

// Draw window borders
void drawborders() {

    // Horizontal borders
    mvaddwstr(0, 0, L"╔");
    mvaddwstr(LINES - 3, 0, L"╭"); //┌ ┐ └ ┘

    for (int i = 1; i < COLS - 1; i++) {
        mvaddwstr(0, i, L"═");
        mvaddwstr(LINES - 4, i, L"═");
        mvaddwstr(LINES - 3, i, L"─");
        mvaddwstr(LINES - 1, i, L"─");
    }

    mvaddwstr(0, COLS - 1, L"╗");
    mvaddwstr(0, COLS - 17, L"╦");
    mvaddwstr(LINES - 3, COLS - 1, L"╮");

    mvaddwstr(LINES - 4, 0, L"╚");
    mvaddwstr(LINES - 1, 0, L"╰");

    mvaddwstr(LINES - 4, COLS - 1, L"╝");
    mvaddwstr(LINES - 4, COLS - 17, L"╩");
    mvaddwstr(LINES - 1, COLS - 1, L"╯");

    // Vertical borders
    mvaddwstr(LINES - 2, 0, L"│");
    mvaddwstr(LINES - 2, COLS - 1, L"│");

    for (int i = 1; i < LINES - 4; i++) {
        mvaddwstr(i, 0, L"║");
        mvaddwstr(i, COLS - 1, L"║");
        mvaddwstr(i, COLS - 17, L"║");
    }

    // Horizontal line in user list
    mvaddwstr(2, COLS - 17 , L"╟───────────────╢");

}

void init_gui() {
    
    setlocale(LC_CTYPE, "");

    initscr();
    keypad(stdscr, TRUE);
    noecho();
    cbreak();
    
    if (has_colors()) {
        use_default_colors();
        start_color();
        init_pair(1, COLOR_BLUE, -1);
    }

    chat_win = subwin(stdscr, LINES - 5, COLS - 18, 1, 1);
    input_win = subwin(stdscr, 1, COLS - 2, LINES - 2, 1);
    users_win = subwin(stdscr, LINES - 7, 15, 3, COLS - 16);

    drawborders();

    mvprintw(1, COLS - 16, "Online users :");

    scrollok(chat_win, true);

    move(LINES - 2, 1);
    wmove(input_win, 0, 0);
    wmove(chat_win, LINES - 6, 0);

    wrefresh(chat_win);
    wrefresh(input_win);
    wrefresh(users_win); 

    input_enabled = true;

}

void destroy_gui() {
    echo();
    endwin();
}

void enable_input() {
    input_enabled = true;
}

void disable_input() {
    input_enabled = false;
}

void print_user_wmsg(wchar_t *msg) {
    wprintw(chat_win, "\n%ls", msg);
    wrefresh(chat_win);
}

void print_user_msg(char *msg) {
    wprintw(chat_win, "\n%s", msg);
    wrefresh(chat_win);
}

void print_system_wmsg(wchar_t *msg) {
    wattron(chat_win, COLOR_PAIR(1));
    wprintw(chat_win, "\n%ls", msg);
    wattroff(chat_win, COLOR_PAIR(1));
    wrefresh(chat_win);
}

void print_system_msg(char *msg) {
    wattron(chat_win, COLOR_PAIR(1));
    wprintw(chat_win, "\n%s", msg);
    wattroff(chat_win, COLOR_PAIR(1));
    wrefresh(chat_win);
}

int gui_input(char *dest, int maxlength) {
    int ch = 0;

    init_zipper(maxlength);
    display_zipper();

    wmove(input_win, 0, 0);
    while (input_enabled) {

        ch = getch();

        if (ch == KEY_LEFT) {

            if (cursor_pos == 0) continue;
            zip_move_left();
            cursor_pos--;

        } else if (ch == KEY_RIGHT) {

            if (length_right == 0) continue;

            zip_move_right();
            cursor_pos++;
        } else if (ch == KEY_BACKSPACE) {

            if (cursor_pos == 0) continue;
            zip_del_char();
            cursor_pos--;

        } else if (ch == KEY_DC) {

            zip_del_char_right();

        } else if (ch == KEY_ENTER || ch == '\n') {

            if (length_left + length_right == 0) continue;

            break;

        } else {

            if (length_left + length_right >= capacity) continue;

            // Non-printable / control character
            if (strlen(unctrl(ch)) != 1) continue;

            zip_add_char(ch);
            cursor_pos++;

        }

        display_zipper();
    }

    if (!input_enabled) return -1;

    int length = length_left + length_right;

    fwprintf(stderr, L"Read : '%ls%ls'\n", buf_left, buf_right + (capacity - length_right));

    #if false
    memcpy(dest, buf_left, length_left);
    memcpy(dest + length_left, buf_right + capacity - length_right, length_right);

    dest[length] = '\0';
    #endif

    zip_clear();
    mvwprintw(input_win, 0, 0, "%-*s", COLS - 2, "");
    wrefresh(input_win);

    return 0;
    return length;
}

void test_gui() {

    int ch = 0;

    init_zipper(COLS * 2 + 10);
    display_zipper();

    wmove(input_win, 0, 0);
    wmove(chat_win, LINES - 6, 0);
    while (true) {

        ch = getch();

        if (ch == KEY_LEFT) {

            if (cursor_pos == 0) continue;
            zip_move_left();
            cursor_pos--;

        } else if (ch == KEY_RIGHT) {

            if (length_right == 0) continue;

            zip_move_right();
            cursor_pos++;
        } else if (ch == KEY_BACKSPACE) {

            if (cursor_pos == 0) continue;
            zip_del_char();
            cursor_pos--;

        } else if (ch == KEY_DC) {

            zip_del_char_right();

        } else if (ch == KEY_ENTER || ch == '\n') {

            if (length_left + length_right == 0) continue;

            wattron(chat_win, COLOR_PAIR(1));
            wprintw(chat_win, "\n%ls%ls", buf_left, buf_right + (capacity - length_right));
            wattroff(chat_win, COLOR_PAIR(1));
            zip_clear();
            mvwprintw(input_win, 0, 0, "%-*s", COLS - 2, "");
            wrefresh(chat_win);

        } else {

            if (length_left + length_right >= capacity) continue;

            // Non-printable / control character
            if (strlen(unctrl(ch)) != 1) continue;

            zip_add_char(ch);
            cursor_pos++;

        }

        display_zipper();

    }
}
