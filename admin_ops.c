#include "utils.h"
#include "Struct.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

// extern semaphore from server.c
extern sem_t *sem_userdb;

/* ------------------------------------------------------------
   Helper: Add new user (Employee or Manager)
   ------------------------------------------------------------ */
static void add_new_user(int connfd) {
    char buffer[256], username[64], password[64], role[32];
    char filename[64];

    send_message(connfd, "Enter role to add (employee/manager): ");
    if (receive_message(connfd, role, sizeof(role)) <= 0) return;
    trim_newline(role);

    if (strcmp(role, "employee") == 0)
        strcpy(filename, "employee.txt");
    else if (strcmp(role, "manager") == 0)
        strcpy(filename, "manager.txt");
    else {
        send_message(connfd, "Invalid role type.\n");
        return;
    }

    send_message(connfd, "Enter new username: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    send_message(connfd, "Enter password for user: ");
    if (receive_message(connfd, password, sizeof(password)) <= 0) return;
    trim_newline(password);

    // Protect file 
     sem_wait(sem_userdb);

    // 🔹 Check for duplicate username before adding
    int exists = check_existing_user(filename, username);
    if (exists == 1) {
        sem_post(sem_userdb);
        send_message(connfd, "Error: Username already exists!\n");
        return;
    } else if (exists == -1) {
        sem_post(sem_userdb);
        send_message(connfd, "Error checking username.\n");
        return;
    }
   
    

    if (add_user(filename, username, password))
        send_message(connfd, "User added successfully!\n");
    else
        send_message(connfd, "Failed to add user.\n");

    sem_post(sem_userdb);
}

/* ------------------------------------------------------------
   Modify Customer / Employee Details (Pure System Call Version)
   ------------------------------------------------------------ */
static void modify_user_details(int connfd) {
    char role[32], target_username[64], new_password[64], active_str[8];
    char filename[64], temp_file[64] = "temp.txt";
    char buffer[512];
    int fd_read, fd_write;
    int found = 0;

    char id_str[16], file_username[64], file_password[64], active_buf[8];
    int id, active;

    // Step 1: Ask which file to modify
    send_message(connfd, "Modify which role? (customer/employee): ");
    if (receive_message(connfd, role, sizeof(role)) <= 0) return;
    trim_newline(role);

    if (strcmp(role, "customer") == 0)
        strcpy(filename, "customer.txt");
    else if (strcmp(role, "employee") == 0)
        strcpy(filename, "employee.txt");
    else {
        send_message(connfd, "Invalid role type.\n");
        return;
    }

    // Step 2: Ask username
    send_message(connfd, "Enter username to modify: ");
    if (receive_message(connfd, target_username, sizeof(target_username)) <= 0) return;
    trim_newline(target_username);

    // Step 3: Ask new password
    send_message(connfd, "Enter new password: ");
    if (receive_message(connfd, new_password, sizeof(new_password)) <= 0) return;
    trim_newline(new_password);

    // Step 4: Ask new active flag
    send_message(connfd, "Set account active? (1=Active, 0=Inactive): ");
    if (receive_message(connfd, active_str, sizeof(active_str)) <= 0) return;
    trim_newline(active_str);
    int new_active = atoi(active_str);
    if (new_active != 0 && new_active != 1) new_active = 1;

    // Step 5: Lock file
    sem_wait(sem_userdb);
    fd_read = open(filename, O_RDONLY);
    if (fd_read < 0) {
        perror("open");
        sem_post(sem_userdb);
        return;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        sem_post(sem_userdb);
        perror("open temp");
        return;
    }

    // Step 6: Read file manually line by line
    ssize_t n;
    char line[256];
    int pos = 0;

    while ((n = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buffer[i] == '\n' || pos >= sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                // Parse line manually using strtok (no sscanf)
                char *token = strtok(line, " ");
                if (!token) continue;
                strcpy(id_str, token);

                token = strtok(NULL, " ");
                if (!token) continue;
                strcpy(file_username, token);

                token = strtok(NULL, " ");
                if (!token) continue;
                strcpy(file_password, token);

                token = strtok(NULL, " ");
                if (!token) continue;
                strcpy(active_buf, token);

                id = atoi(id_str);
                active = atoi(active_buf);

                if (strcmp(file_username, target_username) == 0) {
                    found = 1;

                    // --- write updated record manually ---
                    write(fd_write, id_str, strlen(id_str));
                    write(fd_write, " ", 1);
                    write(fd_write, file_username, strlen(file_username));
                    write(fd_write, " ", 1);
                    write(fd_write, new_password, strlen(new_password));
                    write(fd_write, " ", 1);

                    char act_buf[8];
                    sprintf(act_buf, "%d\n", new_active); // just one sprintf for tiny integer
                    write(fd_write, act_buf, strlen(act_buf));
                } else {
                    // --- rewrite old record ---
                    write(fd_write, id_str, strlen(id_str));
                    write(fd_write, " ", 1);
                    write(fd_write, file_username, strlen(file_username));
                    write(fd_write, " ", 1);
                    write(fd_write, file_password, strlen(file_password));
                    write(fd_write, " ", 1);
                    write(fd_write, active_buf, strlen(active_buf));
                    write(fd_write, "\n", 1);
                }
            } else {
                line[pos++] = buffer[i];
            }
        }
    }

    close(fd_read);
    close(fd_write);

    if (found) {
        rename(temp_file, filename);
        send_message(connfd, "Password and status updated successfully.\n");
    } else {
        unlink(temp_file);
        send_message(connfd, "User not found.\n");
    }

    sem_post(sem_userdb);
}

/* ------------------------------------------------------------
   Manage User Roles (Simplified)
 /* ------------------------------------------------------------
   Manage User Roles (Fixed: parse from a copy so original line
   isn't modified; write original line to dest/temp)
   ------------------------------------------------------------ */
/* ------------------------------------------------------------
   Manage User Roles (Move user and assign new ID in destination)
   - 1: Manager -> Employee
   - 2: Employee -> Manager
   ------------------------------------------------------------ */
static void manage_user_roles(int connfd) {
    char choice_str[8], username[64];
    char src_file[64], dest_file[64], temp_file[64] = "temp.txt";
    char buffer[512];
    int fd_read = -1, fd_write = -1, dest_fd = -1;
    int found = 0;
    ssize_t bytes;
    int pos = 0;

    send_message(connfd, "What do you want to do?\n1. Manager -> Employee\n2. Employee -> Manager\nEnter choice: ");
    if (receive_message(connfd, choice_str, sizeof(choice_str)) <= 0) return;
    trim_newline(choice_str);
    int choice = atoi(choice_str);

    if (choice == 1) {
        strcpy(src_file, "manager.txt");
        strcpy(dest_file, "employee.txt");
    } else if (choice == 2) {
        strcpy(src_file, "employee.txt");
        strcpy(dest_file, "manager.txt");
    } else {
        send_message(connfd, "Invalid option.\n");
        return;
    }

    send_message(connfd, "Enter username to transfer: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    sem_wait(sem_userdb);

    // 1) Verify destination doesn't already contain this username
    dest_fd = open(dest_file, O_RDONLY);
    if (dest_fd >= 0) {
        char line2[256];
        ssize_t n2;
        int p2 = 0;
        while ((n2 = read(dest_fd, buffer, sizeof(buffer))) > 0) {
            for (ssize_t i = 0; i < n2; i++) {
                if (buffer[i] == '\n' || p2 >= (int)sizeof(line2) - 1) {
                    line2[p2] = '\0';
                    p2 = 0;
                    // parse username safely from a copy
                    char copy[256];
                    strncpy(copy, line2, sizeof(copy)-1);
                    copy[sizeof(copy)-1] = '\0';
                    char *tok = strtok(copy, " ");
                    if (!tok) continue; // no id
                    tok = strtok(NULL, " ");
                    if (!tok) continue; // no username
                    if (strcmp(tok, username) == 0) {
                        close(dest_fd);
                        sem_post(sem_userdb);
                        send_message(connfd, "User already exists in destination role. Abort.\n");
                        return;
                    }
                } else {
                    line2[p2++] = buffer[i];
                }
            }
        }
        close(dest_fd);
    }

    // compute next id for destination
    int new_id = get_next_id(dest_file);

    // open source file and temp
    fd_read = open(src_file, O_RDONLY);
    if (fd_read < 0) {
        sem_post(sem_userdb);
        send_message(connfd, "Error opening source file.\n");
        return;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        sem_post(sem_userdb);
        send_message(connfd, "Error creating temp file.\n");
        return;
    }

    // open dest for append (we'll write new record with new id)
    dest_fd = open(dest_file, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (dest_fd < 0) {
        close(fd_read);
        close(fd_write);
        sem_post(sem_userdb);
        send_message(connfd, "Error opening destination file.\n");
        return;
    }

    // Read source file line-by-line, parse from a copy, write others to temp,
    // and when match is found, write a NEW record into dest with new_id.
    char line[256];
    pos = 0;
    while ((bytes = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes; i++) {
            if (buffer[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                // make a copy for tokenization so original line remains intact
                char line_copy[256];
                strncpy(line_copy, line, sizeof(line_copy)-1);
                line_copy[sizeof(line_copy)-1] = '\0';

                char *tok = strtok(line_copy, " ");
                if (!tok) {
                    // malformed line, just keep it
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                    continue;
                }
                int src_id = atoi(tok);

                tok = strtok(NULL, " ");
                if (!tok) {
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                    continue;
                }
                char file_username[128];
                strncpy(file_username, tok, sizeof(file_username)-1);
                file_username[sizeof(file_username)-1] = '\0';

                tok = strtok(NULL, " ");
                if (!tok) {
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                    continue;
                }
                char file_password[128];
                strncpy(file_password, tok, sizeof(file_password)-1);
                file_password[sizeof(file_password)-1] = '\0';

                tok = strtok(NULL, " ");
                if (!tok) {
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                    continue;
                }
                int active = atoi(tok);

                if (strcmp(file_username, username) == 0) {
                    found = 1;
                    // write new record to destination with incremented id (new_id)
                    char id_buf[16], act_buf[8];
                    snprintf(id_buf, sizeof(id_buf), "%d", new_id);
                    snprintf(act_buf, sizeof(act_buf), "%d", active);

                    write(dest_fd, id_buf, strlen(id_buf));
                    write(dest_fd, " ", 1);
                    write(dest_fd, file_username, strlen(file_username));
                    write(dest_fd, " ", 1);
                    write(dest_fd, file_password, strlen(file_password));
                    write(dest_fd, " ", 1);
                    write(dest_fd, act_buf, strlen(act_buf));
                    write(dest_fd, "\n", 1);

                    // increment new_id so multiple transfers in the same run get unique ids
                    new_id++;
                    // do NOT write this line into temp -> effectively remove from source
                } else {
                    // keep unchanged line in temp
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                }
            } else {
                line[pos++] = buffer[i];
            }
        }
    }

    close(fd_read);
    close(fd_write);
    close(dest_fd);

    if (found) {
        if (rename(temp_file, src_file) != 0) {
            send_message(connfd, "Error replacing source file.\n");
            // cleanup: remove temp
            unlink(temp_file);
        } else {
            send_message(connfd, "User role changed successfully (new ID assigned in destination).\n");
        }
    } else {
        unlink(temp_file);
        send_message(connfd, "User not found in source role.\n");
    }

    sem_post(sem_userdb);
}



/* ------------------------------------------------------------
   Change Admin Password
   ------------------------------------------------------------ */
static void change_admin_password(int connfd) {
    char username[64], old_pass[64], new_pass[64];
    char buffer[512], line[256];
    char temp_file[64] = "temp_admin.txt";
    int fd_read, fd_write;
    int id, active, found = 0;
    ssize_t bytes;
    int pos = 0;
    char file_username[64], file_password[64];

    // Step 1: Take credentials
    send_message(connfd, "Enter your username: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    send_message(connfd, "Enter your current password: ");
    if (receive_message(connfd, old_pass, sizeof(old_pass)) <= 0) return;
    trim_newline(old_pass);

    send_message(connfd, "Enter your new password: ");
    if (receive_message(connfd, new_pass, sizeof(new_pass)) <= 0) return;
    trim_newline(new_pass);

    sem_wait(sem_userdb);
    fd_read = open("admin.txt", O_RDONLY);
    if (fd_read < 0) {
        sem_post(sem_userdb);
        send_message(connfd, "Error opening admin file.\n");
        return;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        sem_post(sem_userdb);
        send_message(connfd, "Error creating temp file.\n");
        return;
    }

    // Step 2: Read and rewrite file
    while ((bytes = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes; i++) {
            if (buffer[i] == '\n' || pos >= sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                // Parse manually
                char *token = strtok(line, " ");
                if (!token) continue;
                id = atoi(token);

                token = strtok(NULL, " ");
                if (!token) continue;
                strcpy(file_username, token);

                token = strtok(NULL, " ");
                if (!token) continue;
                strcpy(file_password, token);

                token = strtok(NULL, " ");
                if (!token) continue;
                active = atoi(token);

                // If username and old password match
                if (strcmp(file_username, username) == 0 &&
                    strcmp(file_password, old_pass) == 0) {

                    found = 1;
                    write(fd_write, line, 0); // just to reset, not needed

                    // Write updated line
                    char id_str[16];
                    sprintf(id_str, "%d", id);

                    write(fd_write, id_str, strlen(id_str));
                    write(fd_write, " ", 1);
                    write(fd_write, file_username, strlen(file_username));
                    write(fd_write, " ", 1);
                    write(fd_write, new_pass, strlen(new_pass));
                    write(fd_write, " ", 1);
                    char act_str[8];
                    sprintf(act_str, "%d\n", active);
                    write(fd_write, act_str, strlen(act_str));
                } else {
                    // Keep old line
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                }
            } else {
                line[pos++] = buffer[i];
            }
        }
    }

    close(fd_read);
    close(fd_write);

    if (found) {
        rename(temp_file, "admin.txt");
        send_message(connfd, "Password updated successfully.\n");
    } else {
        unlink(temp_file);
        send_message(connfd, "Invalid username or password.\n");
    }

    sem_post(sem_userdb);
}


/* ------------------------------------------------------------
   Admin Menu
   ------------------------------------------------------------ */
void admin_menu(int connfd , const char *username) {
    char choice_str[32];
    int choice;

     char welcome[128];
    snprintf(welcome, sizeof(welcome), "\nWelcome, %s (Admin)\n", username);
    send_message(connfd, welcome);

    while (1) {
        send_message(connfd,
            "\n====== ADMIN MENU ======\n"
            "1. Add New Bank Employee/Manager\n"
            "2. Modify Customer/Employee Details\n"
            "3. Manage User Roles\n"
            "4. Change Password\n"
            "5. Logout\n"
            "6. Exit\n"
            "Enter your choice: ");

        if (receive_message(connfd, choice_str, sizeof(choice_str)) <= 0)
            return;
        choice = atoi(choice_str);

        switch (choice) {
            case 1:
                add_new_user(connfd);
                break;
            case 2:
                 modify_user_details(connfd);
                break;
            case 3:
                manage_user_roles(connfd);
                break;

            case 4:
                change_admin_password(connfd);
                break;    

            case 5:
                send_message(connfd, "Logging out...\n");
                return; // back to login
            case 6:
                send_message(connfd, "Exiting system...\n");
                sem_wait(sem_userdb);
                mark_user_logged_out("admin.txt", username);
                sem_post(sem_userdb);
                _exit(0);
            default:
                send_message(connfd, "Option not available yet.\n");
                break;
        }
    }
}
