#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

/* ---------- File Locking ---------- */
int lock_file(int fd, int lock_type);    // F_RDLCK or F_WRLCK
int unlock_file(int fd);

/* ---------- User Authentication ---------- */
int validate_login(const char *filename, const char *username, const char *password);

/* ---------- User Management ---------- */
int add_user(const char *filename, const char *username, const char *password);
int get_next_id(const char *filename);
int get_next_global_id(const char *filename);


/* ---------- Input Utilities ---------- */
void trim_newline(char *str);

/* ---------- Error Handling ---------- */
void error_exit(const char *msg);


/* ---------- Socket Message Helpers ---------- */
int send_message(int sockfd, const char *msg);
int receive_message(int sockfd, char *buffer, size_t size);
int check_existing_user(const char *filename, const char *username);
ssize_t read_line(int fd, char *buf, size_t maxlen);
void mark_user_logged_out(const char *filename, const char *username);

#endif // UTILS_H
