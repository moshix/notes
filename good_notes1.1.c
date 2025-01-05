// notes.c
#include <ncurses.h>
#include <mysql/mysql.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS "psw"
#define DB_NAME "notes"
#define VERSION "1.1"

// Define color pairs for easy modification
#define COLOR_INPUT 1
#define COLOR_CONFIRM 2

void init_screen() {
    initscr();                  // Start ncurses mode
    start_color();              // Start color functionality
    init_pair(COLOR_INPUT, COLOR_CYAN, COLOR_BLACK);  // Define cyan on black color pair
    init_pair(COLOR_CONFIRM, COLOR_GREEN, COLOR_BLACK); // Define green on black color pair
    cbreak();                   // Line buffering disabled
    keypad(stdscr, TRUE);       // We get F1, F2, etc..
    refresh();
}

MYSQL *init_db() {
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        endwin();
        fprintf(stderr, "Database connection failed: %s\n", mysql_error(conn));
        exit(1);
    }
    return conn;
}

int authenticate_user(MYSQL *conn) {
    char username[256], password[256];

    clear();
    mvprintw(0, 0, "NOTES version: %s", VERSION);
    refresh();

    attron(COLOR_PAIR(COLOR_INPUT));  // Apply cyan color for input
    mvprintw(2, 0, "Enter username: ");
    echo();  // Ensure echo is enabled to see input
    getstr(username);
    mvprintw(3, 0, "Enter password: ");
    getstr(password);
    attroff(COLOR_PAIR(COLOR_INPUT)); // Turn off cyan color
    noecho();  // Turn off echo after input
    refresh();

    char query[512];
    sprintf(query, "SELECT password FROM users WHERE username = '%s'", username);
    if (mysql_query(conn, query)) {
        return 0; // Query failed
    }

    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(result);
    int auth_result = 0; // Default to failed authentication

    if (row && strcmp(row[0], password) == 0) {
        auth_result = 1; // Successful authentication
    }

    mysql_free_result(result);
    return auth_result;
}

void display_topics(MYSQL *conn, WINDOW *topics_win) {
    wclear(topics_win);
    wmove(topics_win, 0, 0);

    if (mysql_query(conn, "SELECT title, feedback_count, DATE_FORMAT(last_update, '%Y-%m-%d %H:%i') FROM topics")) {
        wprintw(topics_win, "Failed to load topics: %s", mysql_error(conn));
        wrefresh(topics_win);
        return;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW topic;
    int row = 0;
    if (row == 0) {
        wattron(topics_win, A_REVERSE);
        wprintw(topics_win, "Topics:                      Notes:                  Last update:\n");
        wattroff(topics_win, A_REVERSE);
    }
    while ((topic = mysql_fetch_row(result))) {
        wprintw(topics_win, "%-25s %-25s %-25s\n", topic[0], topic[1], topic[2]);
        row++;
    }
    mysql_free_result(result);
    wrefresh(topics_win);
}

void add_topic(MYSQL *conn) {
    WINDOW *input_win = newwin(10, 50, 5, 10);
    box(input_win, 0, 0);

    char title[256] = "";
    char description[2048] = "";

    wattron(input_win, COLOR_PAIR(COLOR_INPUT));  // Apply cyan color for input
    mvwprintw(input_win, 1, 1, "Enter topic title: ");
    echo();
    wgetnstr(input_win, title, 48); // Limit title to window width
    mvwprintw(input_win, 3, 1, "Enter description: ");
    wgetnstr(input_win, description, 2048); // Limit description to 2048 bytes
    noecho();
    wattroff(input_win, COLOR_PAIR(COLOR_INPUT));  // Turn off cyan color

    mvwprintw(input_win, 5, 1, "Press 'Y' to confirm, 'N' to cancel.");
    wrefresh(input_win);

    char confirm = wgetch(input_win);
    if (confirm == 'Y' || confirm == 'y') {
        char query[4096];
        sprintf(query, "INSERT INTO topics (title, description) VALUES ('%s', '%s')", title, description);
        if (mysql_query(conn, query)) {
            mvwprintw(input_win, 7, 1, "Failed to add topic: %s", mysql_error(conn));
        } else {
            mvwprintw(input_win, 7, 1, "Topic added successfully!");
        }
    } else {
        mvwprintw(input_win, 7, 1, "Topic addition canceled.");
    }

    mvwprintw(input_win, 9, 1, "Press any key to close this window.");
    wrefresh(input_win);
    wgetch(input_win);

    delwin(input_win);
}

void handle_user_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *topics_win) {
    wattron(cmd_win, COLOR_PAIR(COLOR_INPUT));  // Apply cyan color for command input
    mvwprintw(cmd_win, 1, 0, "Available commands: add, quit");
    wrefresh(cmd_win);

    char command[256];
    while (true) {
        mvwprintw(cmd_win, 0, 0, "Command: ");
        echo();  // Enable echo to show input
        wgetstr(cmd_win, command);  // Input should now be visible

        if (strcmp(command, "quit") == 0) {
            break;
        } else if (strcmp(command, "add") == 0) {
            add_topic(conn);  // Call the function to add a topic
            display_topics(conn, topics_win);  // Refresh the topics list
        } else {
            mvwprintw(cmd_win, 2, 0, "Unknown command.");
            wgetch(cmd_win); // Pause to show message
        }
    }
}

int main() {
    init_screen();
    MYSQL *conn = init_db();
    
    if (!authenticate_user(conn)) {
        mvprintw(4, 0, "Authentication failed!");
        getch();
        endwin();
        return 1;
    }

    WINDOW *cmd_win = newwin(3, 80, 0, 0); // Command window at the top
    WINDOW *topics_win = newwin(20, 80, 3, 0); // Topics window below command window

    mvprintw(1, 0, "NOTES version: %s", VERSION);
    refresh();

    display_topics(conn, topics_win);
    handle_user_commands(cmd_win, conn, topics_win);

    endwin();
    mysql_close(conn);
    return 0;
}

