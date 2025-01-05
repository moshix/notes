#include <ncurses.h>
#include <mysql/mysql.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS "psw"
#define DB_NAME "notes"
#define VERSION "2.1"

// Define color pairs for easy modification
#define COLOR_INPUT 1
#define COLOR_CONFIRM 2
#define COLOR_TOPIC_ROW 3

// Function declarations
void close_topic(MYSQL *conn, const char *username);
void add_topic(MYSQL *conn, const char *username);
void display_topics(MYSQL *conn, WINDOW *topics_win, int start_row, const char *search_query);
void display_followups(MYSQL *conn, WINDOW *followups_win, int topic_id, int start_row, const char *search_query);
void write_followup(MYSQL *conn, int topic_id, const char *username);
void open_followup(MYSQL *conn, int followup_id);
void handle_followup_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *followups_win, int topic_id, const char *username);
void handle_user_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *topics_win, const char *username);

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

void display_topics(MYSQL *conn, WINDOW *topics_win, int start_row, const char *search_query) {
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

    char query[1024];
    if (search_query && strlen(search_query) > 0) {
        snprintf(query, sizeof(query), "SELECT topic_id, title, created_by, feedback_count, DATE_FORMAT(last_update, '%%Y-%%m-%%d %%H:%%i'), closed, last_follow_by FROM topics WHERE title LIKE '%%%s%%'", search_query);
    } else {
        snprintf(query, sizeof(query), "SELECT topic_id, title, created_by, feedback_count, DATE_FORMAT(last_update, '%%Y-%%m-%%d %%H:%%i'), closed, last_follow_by FROM topics");
    }

    if (mysql_query(conn, query)) {
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

void display_followups(MYSQL *conn, WINDOW *followups_win, int topic_id, int start_row, const char *search_query) {
    wclear(followups_win);
    wmove(followups_win, 0, 0);

    // Define column widths
    int id_width = 4;
    int content_width = 15;
    int user_width = 12;
    int date_width = 19;
    int length_width = 4;

    // Print the title line in a single wprintw call
    wattron(followups_win, A_REVERSE);
    wprintw(followups_win, "%-*s  %-*s  %-*s  %-*s  %-*s\n", 
            id_width, "ID",
            content_width, "Follow-up",
            user_width, "User", 
            date_width, "Date/Time", 
            length_width, "Len");
    wattroff(followups_win, A_REVERSE);

    char query[1024];
    if (search_query && strlen(search_query) > 0) {
        snprintf(query, sizeof(query), "SELECT followup_id, SUBSTRING(content, 1, 15), user_id, DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i'), CHAR_LENGTH(content) FROM followups WHERE topic_id = %d AND content LIKE '%%%s%%'", topic_id, search_query);
    } else {
        snprintf(query, sizeof(query), "SELECT followup_id, SUBSTRING(content, 1, 15), user_id, DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i'), CHAR_LENGTH(content) FROM followups WHERE topic_id = %d", topic_id);
    }

    if (mysql_query(conn, query)) {
        wprintw(followups_win, "Failed to load follow-ups: %s", mysql_error(conn));
        wrefresh(followups_win);
        return;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW followup;

    int row = 0;
    int max_rows = getmaxy(followups_win) - 1; // Account for title line

    // Skip rows until start_row
    while ((followup = mysql_fetch_row(result))) {
        if (row++ < start_row) {
            continue;
        }
        
        // Stop displaying if we've filled the window
        if (row - start_row > max_rows) {
            break;
        }

        wattron(followups_win, COLOR_PAIR(COLOR_TOPIC_ROW));

        // Print the data row in a single wprintw call
        wprintw(followups_win, "%-*s  %-*s  %-*s  %-*s  %-*s\n", 
                id_width, followup[0], 
                content_width, followup[1], 
                user_width, followup[2], 
                date_width, followup[3], 
                length_width, followup[4]);
        wattroff(followups_win, COLOR_PAIR(COLOR_TOPIC_ROW));
    }
    mysql_free_result(result);
    wrefresh(followups_win);
}

void write_followup(MYSQL *conn, int topic_id, const char *username) {
    WINDOW *input_win = newwin(10, 50, 5, 10);
    box(input_win, 0, 0);

    char content[2048] = "";
    char esc_content[4096];
    char query[8192]; 
    unsigned long user_id = 0;

    // Lookup user_id based on username
    snprintf(query, sizeof(query), "SELECT id FROM users WHERE username = '%s'", username);
    if (mysql_query(conn, query)) {
        mvwprintw(input_win, 7, 2, "Failed to retrieve user ID: %s", mysql_error(conn));
        wrefresh(input_win);
        wgetch(input_win);
        delwin(input_win);
        return;
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(result);
    
    if (row && row[0]) {
        user_id = strtoul(row[0], NULL, 10);
    } else {
        mvwprintw(input_win, 7, 2, "Failed to find user ID for username '%s'.", username);
        wrefresh(input_win);
        wgetch(input_win);
        mysql_free_result(result);
        delwin(input_win);
        return;
    }

    mysql_free_result(result);

    wattron(input_win, COLOR_PAIR(COLOR_INPUT));  
    mvwprintw(input_win, 1, 2, "Write follow-up: ");
    echo();
    wgetnstr(input_win, content, 2046); 
    noecho();
    wattroff(input_win, COLOR_PAIR(COLOR_INPUT));  

    mvwprintw(input_win, 5, 2, "Press 'Y' to confirm, 'N' to cancel.");
    wrefresh(input_win);

    char confirm = wgetch(input_win);
    if (confirm == 'Y' || confirm == 'y') {
        mysql_real_escape_string(conn, esc_content, content, strlen(content));
        
        snprintf(query, sizeof(query), "INSERT INTO followups (topic_id, content, user_id) VALUES (%d, '%s', %lu)",
                 topic_id, esc_content, user_id);
        
        if (mysql_query(conn, query)) {
            mvwprintw(input_win, 7, 2, "Failed to add follow-up: %s", mysql_error(conn));
        } else {
            mvwprintw(input_win, 7, 2, "Follow-up added successfully!");
        }
    } else {
        mvwprintw(input_win, 7, 2, "Follow-up addition canceled.");
    }

    mvwprintw(input_win, 9, 2, "Press any key to close this window.");
    wrefresh(input_win);
    wgetch(input_win);

    delwin(input_win);
}

void open_followup(MYSQL *conn, int followup_id) {
    WINDOW *view_win = newwin(10, 50, 5, 10);
    box(view_win, 0, 0);

    char query[512];
    sprintf(query, "SELECT content FROM followups WHERE followup_id = %d", followup_id);

    if (mysql_query(conn, query)) {
        mvwprintw(view_win, 1, 2, "Failed to load follow-up: %s", mysql_error(conn));
    } else {
        MYSQL_RES *result = mysql_store_result(conn);
        MYSQL_ROW followup = mysql_fetch_row(result);

        if (followup) {
            mvwprintw(view_win, 1, 2, "%s", followup[0]);
        } else {
            mvwprintw(view_win, 1, 2, "Follow-up not found.");
        }

        mysql_free_result(result);
    }

    mvwprintw(view_win, 9, 2, "Press any key to close this window.");
    wrefresh(view_win);
    wgetch(view_win);

    delwin(view_win);
}

void handle_followup_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *followups_win, int topic_id, const char *username) {
    wattron(cmd_win, COLOR_PAIR(COLOR_INPUT));  
    mvwprintw(cmd_win, 1, 0, "Available commands: write, open (followup_id), search (word), Main");
    wrefresh(cmd_win);

    int start_row = 0;
    int ch;
    char command[256];
    char search_query[256] = "";

    display_followups(conn, followups_win, topic_id, start_row, search_query);

    while (true) {
        mvwprintw(cmd_win, 0, 0, "Command: ");
        echo();  
        wgetnstr(cmd_win, command, sizeof(command) - 1);  
        noecho();

        if (strcmp(command, "Main") == 0) {
            break;
        } else if (strcmp(command, "write") == 0) {
            write_followup(conn, topic_id, username);  
            display_followups(conn, followups_win, topic_id, start_row, search_query);  
        } else if (strncmp(command, "open", 4) == 0) {
            int followup_id = atoi(command + 5);
            open_followup(conn, followup_id);
            display_followups(conn, followups_win, topic_id, start_row, search_query);
        } else if (strncmp(command, "search", 6) == 0) {
            strcpy(search_query, command + 7);
            display_followups(conn, followups_win, topic_id, start_row, search_query);
        } else {
            mvwprintw(cmd_win, 2, 0, "Unknown command.");
            wrefresh(cmd_win);
            wgetch(cmd_win);  // Wait for user input to continue
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
            display_followups(conn, followups_win, topic_id, start_row, search_query);
        } else if (ch == KEY_DOWN) {
            start_row++;
            display_followups(conn, followups_win, topic_id, start_row, search_query);
        }
    }
}

void handle_user_commands(WINDOW *cmd_win, MYSQL *conn, WINDOW *topics_win, const char *username) {
    wattron(cmd_win, COLOR_PAIR(COLOR_INPUT));  
    mvwprintw(cmd_win, 1, 0, "Available commands: add, close, read (topic_id), search (word), quit");
    wrefresh(cmd_win);

    int start_row = 0;
    int ch;
    char command[256];
    char search_query[256] = "";

    display_topics(conn, topics_win, start_row, search_query);

    while (true) {
        mvwprintw(cmd_win, 0, 0, "Command: ");
        echo();  
        wgetnstr(cmd_win, command, sizeof(command) - 1);  
        noecho();

        if (strcmp(command, "quit") == 0) {
            break;
        } else if (strcmp(command, "add") == 0) {
            add_topic(conn, username);  
            display_topics(conn, topics_win, start_row, search_query);  
        } else if (strcmp(command, "close") == 0) {
            close_topic(conn, username);  
            display_topics(conn, topics_win, start_row, search_query);  
        } else if (strncmp(command, "read", 4) == 0) {
            int topic_id = atoi(command + 5);
            if (topic_id > 0) {
                WINDOW *followups_win = newwin(LINES - 4, COLS, 3, 0);
                handle_followup_commands(cmd_win, conn, followups_win, topic_id, username);
                delwin(followups_win);
                display_topics(conn, topics_win, start_row, search_query);
            } else {
                mvwprintw(cmd_win, 2, 0, "Invalid topic ID.");
                wrefresh(cmd_win);
                wgetch(cmd_win);  // Wait for user input to continue
            }
        } else if (strncmp(command, "search", 6) == 0) {
            strcpy(search_query, command + 7);
            display_topics(conn, topics_win, start_row, search_query);
        } else {
            mvwprintw(cmd_win, 2, 0, "Unknown command.");
            wrefresh(cmd_win);
            wgetch(cmd_win);  // Wait for user input to continue
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
            display_topics(conn, topics_win, start_row, search_query);
        } else if (ch == KEY_DOWN) {
            start_row++;
            display_topics(conn, topics_win, start_row, search_query);
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

