#include "utils.h"
#include<semaphore.h>

#ifdef SERVER_SIDE // <-- ADDED
extern sem_t *sem_userdb;
#endif // <-- ADDED
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

// In utils.c, REPLACE existing validate_login(...)

// Returns: 1 (Success), 0 (Failure: Invalid credentials or Inactive), -2 (Failure: Already logged in)
int validate_login(const char *filename, const char *username, const char *password) {
    char temp_file[] = "temp_login.txt";
    int fd_read, fd_write;
    char buffer[512], line[256];
    ssize_t n, r;
    int pos = 0;
    int result = 0; // Default: Failure/Inactive
    int found_match = 0;

    fd_read = open(filename, O_RDONLY);
    if (fd_read < 0) {
        // file open error or file doesn't exist.
        return 0;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        return 0;
    }

    // Read and rewrite file, applying changes only to the matching user
    while ((n = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buffer[i] == '\n' || pos >= sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;
                
                if (line[0] != '\0') {
                    int id, active, logged_in;
                    char file_username[64], file_password[64];
                    char newline[256];
                    int len;

                    // MUST read 5 fields now
                    if (sscanf(line, "%d %s %s %d %d", &id, file_username, file_password, &active, &logged_in) == 5) {
                        
                        if (strcmp(file_username, username) == 0 && strcmp(file_password, password) == 0) {
                            found_match = 1;
                            
                            if (active == 0) {
                                result = 0; // Inactive
                                // Rewrite original line
                                len = snprintf(newline, sizeof(newline), "%d %s %s %d %d\n", id, file_username, file_password, active, logged_in);
                            } else if (logged_in == 1) {
                                result = -2; // Already logged in
                                // Rewrite original line
                                len = snprintf(newline, sizeof(newline), "%d %s %s %d %d\n", id, file_username, file_password, active, logged_in);
                            } else {
                                // SUCCESS: Mark logged_in = 1
                                result = 1;
                                logged_in = 1; 
                                len = snprintf(newline, sizeof(newline), "%d %s %s %d %d\n", id, file_username, file_password, active, logged_in);
                            }
                            write(fd_write, newline, len);
                            
                        } else {
                            // Not the target user: Rewrite original line
                            len = snprintf(newline, sizeof(newline), "%d %s %s %d %d\n", id, file_username, file_password, active, logged_in);
                            write(fd_write, newline, len);
                        }
                    } else {
                        // Malformed line: write original line + newline
                        write(fd_write, line, strlen(line));
                        write(fd_write, "\n", 1);
                    }
                }
            } else {
                line[pos++] = buffer[i];
            }
        }
    }

    close(fd_read);
    close(fd_write);

    // Finalize: replace old file only if the user was found
    if (found_match) {
        rename(temp_file, filename);
    } else {
        unlink(temp_file);
    }
    
    return result;
}

// In utils.c, ADD this new function

/* =========================================================
   MARK USER LOGGED OUT
   ========================================================= */
void mark_user_logged_out(const char *filename, const char *username) {
    char temp_file[] = "temp_logout.txt";
    int fd_read = open(filename, O_RDONLY);
    if (fd_read < 0) return;

    int fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) { close(fd_read); return; }

    char buf[512], line[256];
    ssize_t r;
    int pos = 0;

    while ((r = read(fd_read, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                if (line[0] != '\0') {
                    int id, active, logged_in;
                    char user[64], pass[64];
                    char newline[256];
                    int len;

                    // MUST read 5 fields
                    if (sscanf(line, "%d %s %s %d %d", &id, user, pass, &active, &logged_in) == 5) {
                        if (strcmp(user, username) == 0) {
                            // Target user: write with logged_in = 0
                            logged_in = 0;
                            len = snprintf(newline, sizeof(newline), "%d %s %s %d %d\n", id, user, pass, active, logged_in);
                        } else {
                            // Other user: write original line
                            len = snprintf(newline, sizeof(newline), "%d %s %s %d %d\n", id, user, pass, active, logged_in);
                        }
                        write(fd_write, newline, len);
                    } else {
                        // Malformed line: write original line + newline
                        write(fd_write, line, strlen(line));
                        write(fd_write, "\n", 1);
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
    }

    close(fd_read);
    close(fd_write);
    rename(temp_file, filename);
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
     snprintf(entry, sizeof(entry), "%d %s %s %d %d\n", new_id, username, password, 1, 0);    // active=1 (default active)

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

/* atomic_add_user_text: atomically compute next id and append new user line
   - filename: "customer.txt" / "employee.txt" etc.
   - username, password: user credentials
   - returns 1 on success, 0 on failure
   Uses: sem_userdb (extern sem_t *sem_userdb)
*/
#ifdef SERVER_SIDE // <-- ADDED
int atomic_add_user_text(const char *filename, const char *username, const char *password) {
             // ensure declared in the file with extern
    sem_wait(sem_userdb);                 // serialize ID allocation + append

    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        sem_post(sem_userdb);
        return 0;
    }

    // lock the file (extra safety)
    lock_file(fd, F_WRLCK);

    // find max id by scanning lines
    char line[512];
    int max_id = 0, id;
    lseek(fd, 0, SEEK_SET);

    while (read_line(fd, line, sizeof(line)) > 0) {
        if (sscanf(line, "%d", &id) == 1) {
            if (id > max_id) max_id = id;
        }
    }

    // determine base for this file (same base logic you use)
    int base = 0;
    if      (strstr(filename, "admin")    != NULL) base = 100;
    else if (strstr(filename, "manager")  != NULL) base = 200;
    else if (strstr(filename, "employee") != NULL) base = 300;
    else if (strstr(filename, "customer") != NULL) base = 400;
    else if (strstr(filename, "account")  != NULL) base = 500;
    else if (strstr(filename, "loan")     != NULL) base = 600;
    else if (strstr(filename, "feedback") != NULL) base = 700;
    else base = 1000;

    if (max_id < base) max_id = base;
    int new_id = max_id + 1;

    // append the new entry
    char entry[512];
    int len = snprintf(entry, sizeof(entry), "%d %s %s %d %d\n",
                       new_id, username, password, 1, 0);

    // append safely using lseek to end
    lseek(fd, 0, SEEK_END);
    ssize_t w = write(fd, entry, len);

    // unlock and cleanup
    unlock_file(fd);
    close(fd);
    sem_post(sem_userdb);

    return (w == len) ? 1 : 0;
}
#endif

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


