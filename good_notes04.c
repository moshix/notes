// notes.c
#include <ncurses.h>
#include <mysql/mysql.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS ""
#define DB_NAME "notes"
#define VERSION "0.4"

void init_screen() {
    initscr();                  // Start ncurses mode
    cbreak();                   // Line buffering disabled
    noecho();                   // Don't echo while we do getch
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
    mvprintw(0, 0, "Enter username: ");
    echo();
    getstr(username);
    mvprintw(1, 0, "Enter password: ");
    getstr(password);
    noecho();

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
    int row = 0;
    wclear(topics_win);

    // Display Topics
    if (mysql_query(conn, "SELECT title, feedback_count, DATE_FORMAT(last_update, '%Y-%m-%d %H:%i') FROM topics")) {
        wprintw(topics_win, "Failed to load topics: %s", mysql_error(conn));
        wrefresh(topics_win);
        return;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW topic;
    while ((topic = mysql_fetch_row(result))) {
        if (row == 0) {
            wattron(topics_win, A_REVERSE);
            wprintw(topics_win, "Topics:                      Notes:                  Last update:\n");
            wattroff(topics_win, A_REVERSE);
        }
        wprintw(topics_win, "%-25s %-25s %-25s\n", topic[0], topic[1], topic[2]);
        row++;
    }
    mysql_free_result(result);
    wrefresh(topics_win);
}

void handle_user_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *topics_win) {
    char command[256];

    while (1) {
        wclear(cmd_win);
        mvwprintw(cmd_win, 0, 0, "Command: ");
        wgetstr(cmd_win, command);

        if (strcmp(command, "quit") == 0) {
            break;
        } else if (strcmp(command, "add") == 0) {
            char title[256];
            echo();
            wprintw(cmd_win, "Enter topic title: ");
            wgetstr(cmd_win, title);
            noecho();

            char query[512];
            sprintf(query, "INSERT INTO topics (title) VALUES ('%s')", title);
            if (mysql_query(conn, query)) {
                wprintw(cmd_win, "Failed to add topic: %s", mysql_error(conn));
            } else {
                wprintw(cmd_win, "Topic added successfully!");
            }
            display_topics(conn, topics_win);
            wgetch(cmd_win); // Pause to show message
        } else {
            wprintw(cmd_win, "Unknown command.");
            wgetch(cmd_win); // Pause to show message
        }
    }
}

int main() {
    init_screen();
    MYSQL *conn = init_db();
    
    if (!authenticate_user(conn)) {
        mvprintw(2, 0, "Authentication failed!");
        getch();
        endwin();
        return 1;
    }

    WINDOW *cmd_win = newwin(1, 80, 0, 0); // Command window at the top
    WINDOW *topics_win = newwin(20, 80, 1, 0); // Topics window below command window

    mvprintw(1, 0, "NOTES version: %s", VERSION);
    refresh();

    display_topics(conn, topics_win);
    handle_user_commands(cmd_win, conn, topics_win);

    endwin();
    mysql_close(conn);
    return 0;
}

