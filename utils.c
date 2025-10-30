#include "utils.h"

/* =========================================================
   FILE LOCKING FUNCTIONS
   ========================================================= */
int lock_file(int fd, int lock_type) {
    struct flock fl;
    fl.l_type = lock_type;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0; // Lock entire file

    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("Error locking file");
        return -1;
    }
    return 0;
}

int unlock_file(int fd) {
    struct flock fl;
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        perror("Error unlocking file");
        return -1;
    }
    return 0;
}

/* =========================================================
   HELPER FUNCTION: Read one line from file descriptor
   ========================================================= */
ssize_t read_line(int fd, char *buf, size_t maxlen) {
    ssize_t n, rc;
    char c;
    for (n = 0; n < (ssize_t)(maxlen - 1); n++) {
        rc = read(fd, &c, 1);
        if (rc == 1) {
            buf[n] = c;
            if (c == '\n') break;
        } else if (rc == 0) {
            break; // EOF
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }
    buf[n] = '\0';
    return n;
}

/* =========================================================
   AUTHENTICATION FUNCTION (open/read)
   Checks username/password in text DB file.
   Format: <id> <username> <password>
   ========================================================= */

   /* ----------------------------------------------------------
   check_existing_user()
   Checks if a username already exists in the given file.
   Returns 1 if found, 0 if not found, -1 on error.
   ---------------------------------------------------------- */
int check_existing_user(const char *filename, const char *username) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        // If file doesn't exist, no duplicates.
        return 0;
    }

    char buf[512], line[256];
    ssize_t r;
    int pos = 0;

    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;
                if (line[0] != '\0') {
                    // parse the username token
                    char copy[256];
                    strncpy(copy, line, sizeof(copy) - 1);
                    copy[sizeof(copy) - 1] = '\0';
                    char *tok = strtok(copy, " "); // skip id
                    tok = strtok(NULL, " ");       // username
                    if (tok && strcmp(tok, username) == 0) {
                        close(fd);
                        return 1;  // found duplicate
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
    }

    close(fd);
    return 0; // not found
}

int validate_login(const char *filename, const char *username, const char *password) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 0;
    }

    char buffer[256];
    char file_username[64], file_password[64];
    int id, active;
    ssize_t n;
    lseek(fd, 0, SEEK_SET);

    char temp_line[256];
    int pos = 0;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buffer[i] == '\n' || pos >= sizeof(temp_line) - 1) {
                temp_line[pos] = '\0';
                pos = 0;

                // Parse: id username password active
                if (sscanf(temp_line, "%d %s %s %d", &id, file_username, file_password, &active) == 4) {
                    if (strcmp(file_username, username) == 0 &&
                        strcmp(file_password, password) == 0) {
                        close(fd);
                        if (active == 1)
                            return 1; // success
                        else
                            return 0; // inactive account
                    }
                }
            } else {
                temp_line[pos++] = buffer[i];
            }
        }
    }

    close(fd);
    return 0;
}

/* =========================================================
   ADD USER FUNCTION (open/write)
   Adds a new record: <id> <username> <password>\n
   ========================================================= */
int add_user(const char *filename, const char *username, const char *password) {
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return 0;
    }

    char buffer[256];
    int id = 0, max_id = 0;
    ssize_t n;
    lseek(fd, 0, SEEK_SET);

    // Find the highest ID
    while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        char *line = strtok(buffer, "\n");
        while (line) {
            sscanf(line, "%d", &id);
            if (id > max_id)
                max_id = id;
            line = strtok(NULL, "\n");
        }
    }

    int new_id = max_id + 1;
    char entry[256];
    snprintf(entry, sizeof(entry), "%d %s %s %d\n", new_id, username, password, 1); 
    // active=1 (default active)

    // Append new entry
    lseek(fd, 0, SEEK_END);
    write(fd, entry, strlen(entry));

    close(fd);
    return 1;
}

/* =========================================================
   GET NEXT ID FUNCTION (open/read)
   Returns max ID + 1 from file
   ========================================================= */
int get_next_id(const char *filename) {
    int fd = open(filename, O_RDONLY | O_CREAT, 0644);
    if (fd == -1) return 1;

    char line[512];
    int id, max_id = 0;

    // read each line and extract only the first integer
    while (read_line(fd, line, sizeof(line)) > 0) {
        if (sscanf(line, "%d", &id) == 1) {  // only read the first number
            if (id > max_id)
                max_id = id;
        }
    }
    close(fd);

    // assign unique range per file
    int base = 0;
    if      (strstr(filename, "admin")    != NULL) base = 100;
    else if (strstr(filename, "manager")  != NULL) base = 200;
    else if (strstr(filename, "employee") != NULL) base = 300;
    else if (strstr(filename, "customer") != NULL) base = 400;
    else if (strstr(filename, "account")  != NULL) base = 500;
    else if (strstr(filename, "loan")     != NULL) base = 600;
    else if (strstr(filename, "feedback") != NULL) base = 700;
    else base = 1000;

    if (max_id < base)
        max_id = base;

    return max_id + 1;
}

/* =========================================================
   STRING CLEANUP
   Removes newline from fgets() input
   ========================================================= */
void trim_newline(char *str) {
    if (!str) return;
    size_t len = strlen(str);
    if (len == 0) return;
    if (str[len - 1] == '\n' || str[len - 1] == '\r')
        str[len - 1] = '\0';
}

/* =========================================================
   ERROR HANDLER
   Prints message and exits
   ========================================================= */
void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

/* =========================================================
   SOCKET MESSAGE HELPERS
   For client-server communication
   ========================================================= */
int send_message(int sockfd, const char *msg) {
    if (!msg) return -1;
    size_t len = strlen(msg);
    ssize_t sent = write(sockfd, msg, len);
    if (sent < 0) {
        perror("Send failed");
        return -1;
    }
    return 0;
}

int receive_message(int sockfd, char *buffer, size_t size) {
    if (!buffer || size == 0) return -1;
    ssize_t n = read(sockfd, buffer, size - 1);
    if (n <= 0) {
        return 0; // connection closed or error
    }
    buffer[n] = '\0';
    return 1;
}


// /*
// #include "utils.h"
// #include "Struct.h"
// #include <errno.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>

// /* =========================================================
//    FILE LOCKING HELPERS
//    ========================================================= */
// int lock_file(int fd, int lock_type) {
//     struct flock fl;
//     fl.l_type = lock_type;
//     fl.l_whence = SEEK_SET;
//     fl.l_start = 0;
//     fl.l_len = 0;
//     return fcntl(fd, F_SETLKW, &fl);
// }

// int unlock_file(int fd) {
//     struct flock fl;
//     fl.l_type = F_UNLCK;
//     fl.l_whence = SEEK_SET;
//     fl.l_start = 0;
//     fl.l_len = 0;
//     return fcntl(fd, F_SETLK, &fl);
// }

// /* =========================================================
//    READ LINE FUNCTION (System call–based)
//    ========================================================= */
// ssize_t read_line(int fd, char *buf, size_t maxlen) {
//     ssize_t n, rc;
//     char c;
//     for (n = 0; n < (ssize_t)(maxlen - 1); n++) {
//         rc = read(fd, &c, 1);
//         if (rc == 1) {
//             buf[n] = c;
//             if (c == '\n') break;
//         } else if (rc == 0) {
//             break; // EOF
//         } else {
//             if (errno == EINTR) continue;
//             return -1;
//         }
//     }
//     buf[n] = '\0';
//     return n;
// }

// /* =========================================================
//    TRIM NEWLINE
//    ========================================================= */
// void trim_newline(char *str) {
//     if (!str) return;
//     size_t len = strlen(str);
//     if (len && (str[len - 1] == '\n' || str[len - 1] == '\r'))
//         str[len - 1] = '\0';
// }

// /* =========================================================
//    ERROR HANDLER
//    ========================================================= */
// void error_exit(const char *msg) {
//     perror(msg);
//     exit(EXIT_FAILURE);
// }

// /* =========================================================
//    SOCKET MESSAGE HELPERS
//    ========================================================= */
// int send_message(int sockfd, const char *msg) {
//     if (!msg) return -1;
//     size_t len = strlen(msg);
//     return (write(sockfd, msg, len) < 0) ? -1 : 0;
// }

// int receive_message(int sockfd, char *buffer, size_t size) {
//     ssize_t n = read(sockfd, buffer, size - 1);
//     if (n <= 0) return 0;
//     buffer[n] = '\0';
//     return 1;
// }

// /* =========================================================
//    ID GENERATION (Text File Based)
//    ========================================================= */
// int get_next_id(const char *filename) {
//     int fd = open(filename, O_RDONLY | O_CREAT, 0644);
//     if (fd == -1) return 1;

//     int id = 0, max_id = 0;
//     char line[256];

//     while (read_line(fd, line, sizeof(line)) > 0) {
//         if (sscanf(line, "%d", &id) == 1) {
//             if (id > max_id)
//                 max_id = id;
//         }
//     }
//     close(fd);

//     int base = 0;
//     if      (strstr(filename, "admin")    != NULL) base = 100;
//     else if (strstr(filename, "manager")  != NULL) base = 200;
//     else if (strstr(filename, "employee") != NULL) base = 300;
//     else if (strstr(filename, "customer") != NULL) base = 400;
//     else if (strstr(filename, "account")  != NULL) base = 500;
//     else if (strstr(filename, "loan")     != NULL) base = 600;
//     else if (strstr(filename, "feedback") != NULL) base = 700;
//     else if (strstr(filename, "transaction") != NULL) base = 800;
//     else base = 1000;

//     if (max_id < base)
//         max_id = base;

//     return max_id + 1;
// }

// /* =========================================================
//    CHECK IF USERNAME ALREADY EXISTS
//    ========================================================= */
// int check_existing_user(const char *filename, const char *username) {
//     int fd = open(filename, O_RDONLY);
//     if (fd == -1) return 0;  // file empty

//     char line[256];
//     char uname[64];
//     int id, active, logged_in;
//     char pass[64];

//     while (read_line(fd, line, sizeof(line)) > 0) {
//         if (sscanf(line, "%d %63s %63s %d %d", &id, uname, pass, &active, &logged_in) >= 3) {
//             if (strcmp(uname, username) == 0) {
//                 close(fd);
//                 return 1;  // duplicate found
//             }
//         }
//     }
//     close(fd);
//     return 0;
// }

// /* =========================================================
//    ADD NEW USER (Appends to text file)
//    Format: id username password active logged_in
//    ========================================================= */
// int add_user(const char *filename, const char *username, const char *password) {
//     if (check_existing_user(filename, username)) return 0;

//     int fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);
//     if (fd == -1) return 0;

//     int new_id = get_next_id(filename);
//     char entry[256];
//     int len = snprintf(entry, sizeof(entry), "%d %s %s %d %d\n", new_id, username, password, 1, 0);
//     write(fd, entry, len);

//     close(fd);
//     return 1;
// }

// /* =========================================================
//    VALIDATE LOGIN
//    Returns:
//      1  - success
//      0  - invalid credentials
//     -1  - inactive account
//     -2  - already logged in
//    ========================================================= */
// int validate_login(const char *filename, const char *username, const char *password) {
//     int fd = open(filename, O_RDWR);
//     if (fd == -1) return 0;

//     lock_file(fd, F_WRLCK);

//     char line[256];
//     off_t offset = 0;
//     int id, active, logged_in;
//     char uname[64], pass[64];
//     ssize_t len;
//     char newline = '\n';

//     while ((len = read_line(fd, line, sizeof(line))) > 0) {
//         if (sscanf(line, "%d %63s %63s %d %d", &id, uname, pass, &active, &logged_in) >= 4) {
//             if (strcmp(uname, username) == 0 && strcmp(pass, password) == 0) {
//                 if (active == 0) {
//                     unlock_file(fd);
//                     close(fd);
//                     return -1; // inactive
//                 }
//                 if (logged_in == 1) {
//                     unlock_file(fd);
//                     close(fd);
//                     return -2; // already logged in
//                 }

//                 // mark logged in (overwrite same line)
//                 off_t cur = lseek(fd, offset, SEEK_SET);
//                 char updated[256];
//                 int updated_len = snprintf(updated, sizeof(updated),
//                                            "%d %s %s %d %d\n",
//                                            id, uname, pass, active, 1);
//                 write(fd, updated, updated_len);
//                 unlock_file(fd);
//                 close(fd);
//                 return 1;
//             }
//         }
//         offset = lseek(fd, 0, SEEK_CUR);
//     }

//     unlock_file(fd);
//     close(fd);
//     return 0;
// }

// /* =========================================================
//    MARK USER LOGGED OUT
//    ========================================================= */
// void mark_user_logged_out(const char *filename, const char *username) {
//     int fd = open(filename, O_RDWR);
//     if (fd == -1) return;

//     lock_file(fd, F_WRLCK);

//     char line[256];
//     off_t offset = 0;
//     int id, active, logged_in;
//     char uname[64], pass[64];

//     while (read_line(fd, line, sizeof(line)) > 0) {
//         if (sscanf(line, "%d %63s %63s %d %d", &id, uname, pass, &active, &logged_in) >= 4) {
//             if (strcmp(uname, username) == 0) {
//                 off_t cur = lseek(fd, offset, SEEK_SET);
//                 char updated[256];
//                 int len = snprintf(updated, sizeof(updated),
//                                    "%d %s %s %d %d\n",
//                                    id, uname, pass, active, 0);
//                 write(fd, updated, len);
//                 break;
//             }
//         }
//         offset = lseek(fd, 0, SEEK_CUR);
//     }

//     unlock_file(fd);
//     close(fd);
// }
