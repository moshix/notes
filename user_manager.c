// user_manager.c
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MYSQL *connect_db() {
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, "localhost", "root", "psw", "notes", 0, NULL, 0)) {
        fprintf(stderr, "Database connection failed: %s\n", mysql_error(conn));
        exit(1);
    }
    return conn;
}

void add_user(MYSQL *conn, const char *username, const char *password) {
    char query[512];
    sprintf(query, "INSERT INTO users (username, password) VALUES ('%s', '%s')", username, password);
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Failed to add user: %s\n", mysql_error(conn));
    } else {
        printf("User added successfully.\n");
    }
}

void delete_user(MYSQL *conn, const char *username) {
    char query[512];
    sprintf(query, "DELETE FROM users WHERE username = '%s'", username);
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Failed to delete user: %s\n", mysql_error(conn));
    } else {
        printf("User deleted successfully.\n");
    }
}

void change_user_password(MYSQL *conn, const char *username, const char *new_password) {
    char query[512];
    sprintf(query, "UPDATE users SET password = '%s' WHERE username = '%s'", new_password, username);
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Failed to update user password: %s\n", mysql_error(conn));
    } else {
        printf("User password updated successfully.\n");
    }
}

void list_users(MYSQL *conn) {
    if (mysql_query(conn, "SELECT username FROM users")) {
        fprintf(stderr, "Failed to list users: %s\n", mysql_error(conn));
    } else {
        MYSQL_RES *result = mysql_store_result(conn);
        MYSQL_ROW row;
        printf("List of users:\n");
        while ((row = mysql_fetch_row(result))) {
            printf("Username: %s\n", row[0]);
        }
        mysql_free_result(result);
    }
}

int main(int argc, char **argv) {
    MYSQL *conn;
    conn = connect_db();

    if (argc < 2) {
        printf("Usage: %s add|delete|change|list <username> [new_password]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "add") == 0 && argc == 4) {
        add_user(conn, argv[2], argv[3]);
    } else if (strcmp(argv[1], "delete") == 0 && argc == 3) {
        delete_user(conn, argv[2]);
    } else if (strcmp(argv[1], "change") == 0 && argc == 4) {
        change_user_password(conn, argv[2], argv[3]);
    } else if (strcmp(argv[1], "list") == 0 && argc == 2) {
        list_users(conn);
    } else {
        printf("Invalid command or number of arguments.\n");
        printf("Usage: %s add|delete|change|list <username> [new_password]\n", argv[0]);
    }

    mysql_close(conn);
    return 0;
}

