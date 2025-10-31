/* manager_ops.c
   Manager operations: view customers, activate/deactivate accounts,
   assign loan processes to employees, review feedback, change password.
   Designed to be included directly into server.c (no header).
*/

#include "utils.h"
#include "Struct.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

// extern semaphores created in server.c
extern sem_t *sem_userdb;   // protects user DBs
extern sem_t *sem_account;  // protects loan/feedback/account operations

/* ------------------------------------------------------------
   Helper: send whole file contents to client (line by line)
   ------------------------------------------------------------ */
static void send_file_contents(const char *filename, int connfd) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        send_message(connfd, "File not found or error opening file.\n");
        return;
    }

    // read and send in chunks (safe for larger files)
    char buf[512];
    ssize_t n;
    // we'll gather lines and send them; simpler: send chunk as-is
    // but to keep messages readable, send per read()
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        // write chunk to client (may split lines) — acceptable for viewing
        // ensure null termination for send_message usage
        char tmp[513];
        memcpy(tmp, buf, n);
        tmp[n] = '\0';
        send_message(connfd, tmp);
    }
    close(fd);
}

/* ------------------------------------------------------------
   1) View All Customers (read-only)
   ------------------------------------------------------------ */
static void view_all_customers(int connfd) {
    sem_wait(sem_userdb);
    send_message(connfd, "----- All Customers -----\n");
    send_file_contents("customer.txt", connfd);
    send_message(connfd, "\n----- End of Customers -----\n");
    sem_post(sem_userdb);
}

/* ------------------------------------------------------------
   2) Activate/Deactivate Customer Accounts
   (toggle active flag by username)
   ------------------------------------------------------------ */
static void toggle_customer_active(int connfd) {
    char username[64], active_str[8];
    char temp_file[] = "temp_customer.txt";
    char buffer[512];
    int fd_read = -1, fd_write = -1;
    int found = 0;
    ssize_t bytes;
    int pos = 0;

    send_message(connfd, "Enter customer username to modify: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    send_message(connfd, "Set account active? (1=Active, 0=Inactive): ");
    if (receive_message(connfd, active_str, sizeof(active_str)) <= 0) return;
    trim_newline(active_str);
    int new_active = atoi(active_str);
    if (new_active != 0 && new_active != 1) new_active = 1; // default

    sem_wait(sem_userdb);

    fd_read = open("customer.txt", O_RDONLY);
    if (fd_read < 0) {
        sem_post(sem_userdb);
        send_message(connfd, "Error opening customer file.\n");
        return;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        sem_post(sem_userdb);
        send_message(connfd, "Error creating temporary file.\n");
        return;
    }

    char line[256];
    pos = 0;
    while ((bytes = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i=0; i<bytes; i++) {
            if (buffer[i] == '\n' || pos >= (int)sizeof(line)-1) {
                line[pos] = '\0';
                pos = 0;

                // parse from copy
                char copy[256];
                strncpy(copy, line, sizeof(copy)-1);
                copy[sizeof(copy)-1] = '\0';

                char *tok = strtok(copy, " ");
                if (!tok) { // malformed -> keep as-is
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                    continue;
                }
                int id = atoi(tok);

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
                    // write updated line: id username password new_active
                    char idbuf[16], actbuf[8];
                    snprintf(idbuf, sizeof(idbuf), "%d", id);
                    snprintf(actbuf, sizeof(actbuf), "%d", new_active);

                    write(fd_write, idbuf, strlen(idbuf));
                    write(fd_write, " ", 1);
                    write(fd_write, file_username, strlen(file_username));
                    write(fd_write, " ", 1);
                    write(fd_write, file_password, strlen(file_password));
                    write(fd_write, " ", 1);
                    write(fd_write, actbuf, strlen(actbuf));
                    write(fd_write, "\n", 1);
                } else {
                    // copy as-is
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
        if (rename(temp_file, "customer.txt") != 0) {
            send_message(connfd, "Error updating customer file.\n");
            unlink(temp_file);
        } else {
            send_message(connfd, "Customer active status updated.\n");
        }
    } else {
        unlink(temp_file);
        send_message(connfd, "Customer not found.\n");
    }

    sem_post(sem_userdb);
}

/* ------------------------------------------------------------
   3) Assign Loan Application Processes to Employees
   - Simple format expected in loan_db.txt:
     <loan_id> <customer_username> <amount> <status> <assigned_employee>
   - status examples: pending, assigned, approved, rejected
   ------------------------------------------------------------ */
static void assign_loan_to_employee(int connfd) {
    char loan_id_str[32], emp_username[64];
    char temp_file[] = "temp_loans.txt";
    char buffer[512];
    int fd_read = -1, fd_write = -1;
    int found = 0;
    ssize_t bytes;
    int pos = 0;

    // ask loan id and employee
    send_message(connfd, "Enter loan id to assign: ");
    if (receive_message(connfd, loan_id_str, sizeof(loan_id_str)) <= 0) return;
    trim_newline(loan_id_str);

    send_message(connfd, "Enter employee username to assign to: ");
    if (receive_message(connfd, emp_username, sizeof(emp_username)) <= 0) return;
    trim_newline(emp_username);

    // Verify employee exists
    sem_wait(sem_userdb);
    int emp_exists = 0;
    int efd = open("employee.txt", O_RDONLY);
    if (efd >= 0) {
        ssize_t n2;
        int p2 = 0;
        char buf2[512], line2[256];
        while ((n2 = read(efd, buf2, sizeof(buf2))) > 0) {
            for (ssize_t i=0;i<n2;i++) {
                if (buf2[i] == '\n' || p2 >= (int)sizeof(line2)-1) {
                    line2[p2] = '\0';
                    p2 = 0;
                    // parse username token
                    char copy[256];
                    strncpy(copy, line2, sizeof(copy)-1);
                    copy[sizeof(copy)-1] = '\0';
                    char *t = strtok(copy, " "); // id
                    t = strtok(NULL, " "); // username
                    if (t && strcmp(t, emp_username) == 0) { emp_exists = 1; break; }
                } else line2[p2++] = buf2[i];
            }
            if (emp_exists) break;
        }
        close(efd);
    }
    if (!emp_exists) {
        sem_post(sem_userdb);
        send_message(connfd, "Employee not found.\n");
        return;
    }
    sem_post(sem_userdb);

    // lock loan file (account semaphore) and perform update
    sem_wait(sem_account);

    fd_read = open("loan_db.txt", O_RDONLY);
    if (fd_read < 0) {
        sem_post(sem_account);
        send_message(connfd, "No loan database found or error opening.\n");
        return;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        sem_post(sem_account);
        send_message(connfd, "Error creating temp loan file.\n");
        return;
    }

    // parse loan_db line-by-line, update matching loan_id
    char line[512];
    pos = 0;
    while ((bytes = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i=0;i<bytes;i++) {
            if (buffer[i] == '\n' || pos >= (int)sizeof(line)-1) {
                line[pos] = '\0';
                pos = 0;

                // make copy for safe tokenizing
                char copy[512];
                strncpy(copy, line, sizeof(copy)-1);
                copy[sizeof(copy)-1] = '\0';

                char *tok = strtok(copy, " ");
                if (!tok) {
                    // write unchanged
                    write(fd_write, line, strlen(line));
                    write(fd_write, "\n", 1);
                    continue;
                }
                char loanid_chk[64];
                strncpy(loanid_chk, tok, sizeof(loanid_chk)-1);
                loanid_chk[sizeof(loanid_chk)-1] = '\0';

                // compare ids
                if (strcmp(loanid_chk, loan_id_str) == 0) {
                    // parse customer, amount, status, assigned
                    char *cust = strtok(NULL, " ");
                    char *amt = strtok(NULL, " ");
                    // status
                    char *status = strtok(NULL, " ");
                    // assigned
                    char *assigned = strtok(NULL, " ");

                    // build updated line: loan_id customer amount assigned status(assigned)
                    // we'll write: loan_id customer amount assigned_employee assigned
                    write(fd_write, loanid_chk, strlen(loanid_chk));
                    write(fd_write, " ", 1);
                    if (cust) { write(fd_write, cust, strlen(cust)); write(fd_write, " ",1); }
                    if (amt) { write(fd_write, amt, strlen(amt)); write(fd_write, " ",1); }
                    // assigned employee username
                    write(fd_write, emp_username, strlen(emp_username));
                    write(fd_write, " ", 1);
                    // status
                    write(fd_write, "assigned", strlen("assigned"));
                    write(fd_write, "\n", 1);

                    found = 1;
                } else {
                    // not target loan -> copy unchanged
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
        if (rename(temp_file, "loan_db.txt") != 0) {
            send_message(connfd, "Error finalizing loan update.\n");
            unlink(temp_file);
        } else {
            send_message(connfd, "Loan assigned to employee successfully.\n");
        }
    } else {
        unlink(temp_file);
        send_message(connfd, "Loan id not found.\n");
    }

    sem_post(sem_account);
}

/* ------------------------------------------------------------
   4) Review Customer Feedback
   - Simply display feedback_db.txt to manager
   ------------------------------------------------------------ */
static void review_customer_feedback(int connfd) {
    sem_wait(sem_account); // protect feedback file
    send_message(connfd, "----- Customer Feedback -----\n");
    send_file_contents("feedback_db.txt", connfd);
    send_message(connfd, "\n----- End of Feedback -----\n");
    sem_post(sem_account);
}

/* ------------------------------------------------------------
   5) Change Manager Password (self)
   - Similar to admin change password but for manager.txt
   ------------------------------------------------------------ */
static void change_manager_password(int connfd) {
    char username[64], old_pass[64], new_pass[64];
    char buffer[512], line[256];
    char temp_file[] = "temp_manager.txt";
    int fd_read = -1, fd_write = -1;
    ssize_t bytes;
    int pos = 0;
    int found = 0;

    send_message(connfd, "Enter your manager username: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    send_message(connfd, "Enter your current password: ");
    if (receive_message(connfd, old_pass, sizeof(old_pass)) <= 0) return;
    trim_newline(old_pass);

    send_message(connfd, "Enter your new password: ");
    if (receive_message(connfd, new_pass, sizeof(new_pass)) <= 0) return;
    trim_newline(new_pass);

    sem_wait(sem_userdb);

    fd_read = open("manager.txt", O_RDONLY);
    if (fd_read < 0) {
        sem_post(sem_userdb);
        send_message(connfd, "Error opening manager file.\n");
        return;
    }

    fd_write = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_write < 0) {
        close(fd_read);
        sem_post(sem_userdb);
        send_message(connfd, "Error creating temp file.\n");
        return;
    }

    pos = 0;
    while ((bytes = read(fd_read, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i=0;i<bytes;i++) {
            if (buffer[i] == '\n' || pos >= (int)sizeof(line)-1) {
                line[pos] = '\0';
                pos = 0;

                // parse copy
                char copy[256];
                strncpy(copy, line, sizeof(copy)-1);
                copy[sizeof(copy)-1] = '\0';

                char *tok = strtok(copy, " ");
                if (!tok) { // malformed
                    write(fd_write, line, strlen(line)); write(fd_write, "\n",1);
                    continue;
                }
                int id = atoi(tok);

                tok = strtok(NULL, " ");
                if (!tok) { write(fd_write, line, strlen(line)); write(fd_write, "\n",1); continue; }
                char file_username[128];
                strncpy(file_username, tok, sizeof(file_username)-1);
                file_username[sizeof(file_username)-1] = '\0';

                tok = strtok(NULL, " ");
                if (!tok) { write(fd_write, line, strlen(line)); write(fd_write, "\n",1); continue; }
                char file_password[128];
                strncpy(file_password, tok, sizeof(file_password)-1);
                file_password[sizeof(file_password)-1] = '\0';

                tok = strtok(NULL, " ");
                if (!tok) { write(fd_write, line, strlen(line)); write(fd_write, "\n",1); continue; }
                int active = atoi(tok);

                if (strcmp(file_username, username) == 0 && strcmp(file_password, old_pass) == 0) {
                    found = 1;
                    // write updated record
                    char id_buf[16], act_buf[8];
                    snprintf(id_buf, sizeof(id_buf), "%d", id);
                    snprintf(act_buf, sizeof(act_buf), "%d", active);

                    write(fd_write, id_buf, strlen(id_buf));
                    write(fd_write, " ", 1);
                    write(fd_write, file_username, strlen(file_username));
                    write(fd_write, " ", 1);
                    write(fd_write, new_pass, strlen(new_pass));
                    write(fd_write, " ", 1);
                    write(fd_write, act_buf, strlen(act_buf));
                    write(fd_write, "\n", 1);
                } else {
                    // copy as-is
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
        if (rename(temp_file, "manager.txt") != 0) {
            send_message(connfd, "Error updating manager password.\n");
            unlink(temp_file);
        } else {
            send_message(connfd, "Manager password updated successfully.\n");
        }
    } else {
        unlink(temp_file);
        send_message(connfd, "Invalid username or password.\n");
    }

    sem_post(sem_userdb);
}

/* ------------------------------------------------------------
   Manager menu
   ------------------------------------------------------------ */
void manager_menu(int connfd, const char *username){
    char choice_str[8];
    int choice = 0;

     char welcome[128];
    snprintf(welcome, sizeof(welcome), "\nWelcome, %s (Manager)\n", username);
    send_message(connfd, welcome);

    while (1) {
        send_message(connfd,
            "\n====== MANAGER MENU ======\n"
            "1. View All Customers\n"
            "2. Activate/Deactivate Customer Account\n"
            "3. Assign Loan Application to Employee\n"
            "4. Review Customer Feedback\n"
            "5. Change Password\n"
            "6. Logout\n"
            "7. Exit\n"
            "Enter your choice: "
        );

        if (receive_message(connfd, choice_str, sizeof(choice_str)) <= 0)
            return;
        trim_newline(choice_str);
        choice = atoi(choice_str);

        switch (choice) {
            case 1:
                view_all_customers(connfd);
                break;
            case 2:
                toggle_customer_active(connfd);
                break;
            case 3:
                assign_loan_to_employee(connfd);
                break;
            case 4:
                review_customer_feedback(connfd);
                break;
            case 5:
                change_manager_password(connfd);
                break;
            case 6:
                send_message(connfd, "Logging out...\n");
                return;
            case 7:
                send_message(connfd, "Exiting system...\n");
                sem_wait(sem_userdb);
                mark_user_logged_out("manager.txt", username);
                sem_post(sem_userdb);
                _exit(0);
            default:
                send_message(connfd, "Invalid choice. Try again.\n");
                break;
        }
    }
}
