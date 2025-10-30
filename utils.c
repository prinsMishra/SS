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
static ssize_t read_line(int fd, char *buf, size_t maxlen) {
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
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        // File doesn’t exist yet; start with ID = 1
        return 1;
    }

    lock_file(fd, F_RDLCK);

    char line[256];
    int id, max_id = 0;
    char uname[64], pass[64];

    while (read_line(fd, line, sizeof(line)) > 0) {
        if (sscanf(line, "%d %63s %63s", &id, uname, pass) == 3) {
            if (id > max_id)
                max_id = id;
        }
    }

    unlock_file(fd);
    close(fd);

    
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
