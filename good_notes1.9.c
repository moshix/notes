#include <ncurses.h>
#include <mysql/mysql.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS ""
#define DB_NAME "notes"
#define VERSION "1.9"

// Define color pairs for easy modification
#define COLOR_INPUT 1
#define COLOR_CONFIRM 2
#define COLOR_TOPIC_ROW 3

void init_screen() {
    initscr();                  
    start_color();              
    init_pair(COLOR_INPUT, COLOR_CYAN, COLOR_BLACK);  
    init_pair(COLOR_CONFIRM, COLOR_GREEN, COLOR_BLACK); 
    init_pair(COLOR_TOPIC_ROW, COLOR_CYAN, COLOR_BLACK);
    cbreak();                   
    keypad(stdscr, TRUE);       
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

int authenticate_user(MYSQL *conn, char *username) {
    char password[256];

    clear();
    mvprintw(0, 0, "NOTES version: %s", VERSION);
    refresh();

    attron(COLOR_PAIR(COLOR_INPUT));  
    mvprintw(2, 0, "Enter username: ");
    echo();  
    getstr(username);
    mvprintw(3, 0, "Enter password: ");
    getstr(password);
    attroff(COLOR_PAIR(COLOR_INPUT)); 
    noecho();  
    refresh();

    char query[512];
    sprintf(query, "SELECT password FROM users WHERE username = '%s'", username);
    if (mysql_query(conn, query)) {
        return 0; 
    }

    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(result);
    int auth_result = 0; 

    if (row && strcmp(row[0], password) == 0) {
        auth_result = 1; 
    }

    mysql_free_result(result);
    return auth_result;
}

void display_topics(MYSQL *conn, WINDOW *topics_win, int start_row) {
    wclear(topics_win);
    wmove(topics_win, 0, 0);

    // Define column widths
    int id_width = 8;  // Adjusted width for topic_id
    int title_width = 30;
    int user_width = 12;
    int notes_width = 4;
    int date_width = 19;
    int follow_by_width = 12;
    int status_width = 6;

    // Print the title line in a single wprintw call
    wattron(topics_win, A_REVERSE);
    wprintw(topics_win, "%-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n", 
            id_width, "Topic ID",
            title_width, "Title", 
            user_width, "User", 
            notes_width, "Notes", 
            date_width, "Last update", 
            follow_by_width, "Last follow by", 
            status_width, "Status");
    wattroff(topics_win, A_REVERSE);

    if (mysql_query(conn, "SELECT topic_id, title, created_by, feedback_count, DATE_FORMAT(last_update, '%Y-%m-%d %H:%i'), closed, last_follow_by FROM topics")) {
        wprintw(topics_win, "Failed to load topics: %s", mysql_error(conn));
        wrefresh(topics_win);
        return;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW topic;

    int row = 0;
    int max_rows = getmaxy(topics_win) - 1; // Account for title line

    // Skip rows until start_row
    while ((topic = mysql_fetch_row(result))) {
        if (row++ < start_row) {
            continue;
        }
        
        // Stop displaying if we've filled the window
        if (row - start_row > max_rows) {
            break;
        }

        wattron(topics_win, COLOR_PAIR(COLOR_TOPIC_ROW));

        // Truncate title if it's too long
        char truncated_title[title_width + 1];
        strncpy(truncated_title, topic[1], title_width);
        truncated_title[title_width] = '\0'; // Ensure null-terminated string

        const char *status = strcmp(topic[5], "1") == 0 ? "Closed" : "Open";

        // Print the data row in a single wprintw call
        wprintw(topics_win, "%-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n", 
                id_width, topic[0], 
                title_width, truncated_title, 
                user_width, topic[2], 
                notes_width, topic[3], 
                date_width, topic[4], 
                follow_by_width, topic[6], 
                status_width, status);
        wattroff(topics_win, COLOR_PAIR(COLOR_TOPIC_ROW));
    }
    mysql_free_result(result);
    wrefresh(topics_win);
}

void close_topic(MYSQL *conn, const char *username) {
    WINDOW *input_win = newwin(5, 50, 5, 10);
    box(input_win, 0, 0);

    char title[256] = "";

    wattron(input_win, COLOR_PAIR(COLOR_INPUT));  
    mvwprintw(input_win, 1, 2, "Enter topic title to close: ");
    echo();
    wgetnstr(input_win, title, 46); 
    noecho();
    wattroff(input_win, COLOR_PAIR(COLOR_INPUT));  

    mvwprintw(input_win, 3, 2, "Press 'Y' to confirm, 'N' to cancel.");
    wrefresh(input_win);

    char confirm = wgetch(input_win);
    if (confirm == 'Y' || confirm == 'y') {
        char query[1024];
        snprintf(query, sizeof(query), "UPDATE topics SET closed = 1 WHERE title = '%s' AND created_by = '%s'", title, username);
        if (mysql_query(conn, query)) {
            mvwprintw(input_win, 3, 2, "Failed to close topic: %s", mysql_error(conn));
        } else if (mysql_affected_rows(conn) == 0) {
            mvwprintw(input_win, 3, 2, "You are not the creator or topic not found.");
        } else {
            mvwprintw(input_win, 3, 2, "Topic closed successfully!");
        }
    } else {
        mvwprintw(input_win, 3, 2, "Topic closure canceled.");
    }

    mvwprintw(input_win, 4, 2, "Press any key to close this window.");
    wrefresh(input_win);
    wgetch(input_win);

    delwin(input_win);
}

void add_topic(MYSQL *conn, const char *username) {
    WINDOW *input_win = newwin(10, 50, 5, 10);
    box(input_win, 0, 0);

    char title[256] = "";
    char description[2048] = "";
    char esc_title[512];
    char esc_description[4096];

    wattron(input_win, COLOR_PAIR(COLOR_INPUT));  
    mvwprintw(input_win, 1, 2, "Enter topic title: ");
    echo();
    wgetnstr(input_win, title, 46); 
    mvwprintw(input_win, 3, 2, "Enter description: ");
    wgetnstr(input_win, description, 2046); 
    noecho();
    wattroff(input_win, COLOR_PAIR(COLOR_INPUT));  

    mvwprintw(input_win, 5, 2, "Press 'Y' to confirm, 'N' to cancel.");
    wrefresh(input_win);

    char confirm = wgetch(input_win);
    if (confirm == 'Y' || confirm == 'y') {
        mysql_real_escape_string(conn, esc_title, title, strlen(title));
        mysql_real_escape_string(conn, esc_description, description, strlen(description));
        
        char query[8192]; 
        snprintf(query, sizeof(query), "INSERT INTO topics (title, description, created_by, closed) VALUES ('%s', '%s', '%s', 0)",
                 esc_title, esc_description, username);
        if (mysql_query(conn, query)) {
            mvwprintw(input_win, 7, 2, "Failed to add topic: %s", mysql_error(conn));
        } else {
            mvwprintw(input_win, 7, 2, "Topic added successfully!");
        }
    } else {
        mvwprintw(input_win, 7, 2, "Topic addition canceled.");
    }

    mvwprintw(input_win, 9, 2, "Press any key to close this window.");
    wrefresh(input_win);
    wgetch(input_win);

    delwin(input_win);
}

void handle_user_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *topics_win, const char *username) {
    wattron(cmd_win, COLOR_PAIR(COLOR_INPUT));  
    mvwprintw(cmd_win, 1, 0, "Available commands: add, close, quit");
    wrefresh(cmd_win);

    int start_row = 0;
    int ch;
    char command[256];  // Declare command variable

    display_topics(conn, topics_win, start_row);

    while (true) {
        mvwprintw(cmd_win, 0, 0, "Command: ");
        echo();  
        wgetstr(cmd_win, command);  

        if (strcmp(command, "quit") == 0) {
            break;
        } else if (strcmp(command, "add") == 0) {
            add_topic(conn, username);  
            display_topics(conn, topics_win, start_row);  
        } else if (strcmp(command, "close") == 0) {
            close_topic(conn, username);  
            display_topics(conn, topics_win, start_row);  
        } else {
            mvwprintw(cmd_win, 2, 0, "Unknown command.");
            wgetch(cmd_win); 
        }

        // Clear the command input area after processing the command
        wmove(cmd_win, 0, 0);
        wclrtoeol(cmd_win);
        wrefresh(cmd_win);

        // Handle scrolling
        nodelay(cmd_win, TRUE); // Non-blocking input
        ch = wgetch(cmd_win);
        if (ch == KEY_UP && start_row > 0) {
            start_row--;
            display_topics(conn, topics_win, start_row);
        } else if (ch == KEY_DOWN) {
            start_row++;
            display_topics(conn, topics_win, start_row);
        }
    }
}

int main() {
    init_screen();
    MYSQL *conn = init_db();

    char username[256];
    if (!authenticate_user(conn, username)) {
        mvprintw(4, 0, "Authentication failed!");
        getch();
        endwin();
        return 1;
    }

    WINDOW *cmd_win = newwin(3, COLS, 0, 0); 
    WINDOW *topics_win = newwin(LINES - 3, COLS, 3, 0); 

    mvprintw(1, 0, "NOTES version: %s", VERSION);
    refresh();

    handle_user_commands(cmd_win, conn, topics_win, username);

    endwin();
    mysql_close(conn);
    return 0;
}

