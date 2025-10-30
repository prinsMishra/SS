#include "utils.h"
#include "Struct.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

// External semaphores declared in server.c
extern sem_t *sem_userdb;
extern sem_t *sem_account;



void add_new_customer(int connfd) {
    char username[64], password[64], deposit_str[32];
    int deposit;

    send_message(connfd, "Enter new customer username: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    send_message(connfd, "Enter password: ");
    if (receive_message(connfd, password, sizeof(password)) <= 0) return;
    trim_newline(password);

    send_message(connfd, "Enter initial deposit amount: ");
    if (receive_message(connfd, deposit_str, sizeof(deposit_str)) <= 0) return;
    trim_newline(deposit_str);
    deposit = atoi(deposit_str);

    /* --- Write to customer.txt --- */
    sem_wait(sem_userdb);

    if (check_existing_user("customer.txt", username)) {
    sem_post(sem_userdb);
    send_message(connfd, "Error: Username already exists.\n");
    return;
}

    int fd_cust = open("customer.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_cust == -1) {
        sem_post(sem_userdb);
        send_message(connfd, "Error: cannot open customer.txt\n");
        return;
    }

    int new_cust_id = get_next_id("customer.txt");

    char buf[256];
    int len = 0;

    // manually format using write (no snprintf)
    len += sprintf(buf + len, "%d ", new_cust_id);
    len += sprintf(buf + len, "%s ", username);
    len += sprintf(buf + len, "%s ", password);
    len += sprintf(buf + len, "%d\n", 1); // active

    write(fd_cust, buf, len);
    close(fd_cust);
    sem_post(sem_userdb);

    /* --- Write to account_db.txt --- */
    sem_wait(sem_account);

    int fd_acc = open("account_db.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_acc == -1) {
        sem_post(sem_account);
        send_message(connfd, "Error: cannot open account_db.txt\n");
        return;
    }

    int new_acc_id = get_next_id("account_db.txt");

    char abuf[256];
    int alen = 0;

    alen += sprintf(abuf + alen, "%d ", new_acc_id);
    alen += sprintf(abuf + alen, "%s ", username);
    alen += sprintf(abuf + alen, "%d\n", deposit);

    write(fd_acc, abuf, alen);
    close(fd_acc);
    sem_post(sem_account);

    send_message(connfd, "✅ New customer added successfully.\n");
}

void modify_customer_details_emp(int connfd) {
    char username[64], new_password[64], status_str[8];
    char buf[512];
    int fd_old, fd_new;
    int found = 0;

    send_message(connfd, "Enter customer username to modify: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    sem_wait(sem_userdb);

    fd_old = open("customer.txt", O_RDONLY);
    if (fd_old == -1) {
        sem_post(sem_userdb);
        send_message(connfd, "Error: cannot open customer.txt\n");
        return;
    }

    fd_new = open("temp_customer.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_new == -1) {
        close(fd_old);
        sem_post(sem_userdb);
        send_message(connfd, "Error: cannot create temp file.\n");
        return;
    }

    char line[256];
    ssize_t r;
    int pos = 0;

    while ((r = read(fd_old, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;
                if (line[0] != '\0') {
                    int id, active;
                    char user[64], pass[64];
                    if (sscanf(line, "%d %s %s %d", &id, user, pass, &active) == 4) {
                        if (strcmp(user, username) == 0) {
                            found = 1;

                            // Ask for new password
                            send_message(connfd, "Enter new password: ");
                            if (receive_message(connfd, new_password, sizeof(new_password)) <= 0) {
                                close(fd_old);
                                close(fd_new);
                                sem_post(sem_userdb);
                                unlink("temp_customer.txt");
                                return;
                            }
                            trim_newline(new_password);

                            // Ask for new status
                            send_message(connfd, "Enter new status (1 = Active, 0 = Inactive): ");
                            if (receive_message(connfd, status_str, sizeof(status_str)) <= 0) {
                                close(fd_old);
                                close(fd_new);
                                sem_post(sem_userdb);
                                unlink("temp_customer.txt");
                                return;
                            }
                            trim_newline(status_str);
                            active = atoi(status_str);

                            // Write modified record
                            int len = snprintf(line, sizeof(line), "%d %s %s %d\n", id, user, new_password, active);
                            write(fd_new, line, len);
                        } else {
                            // Write unchanged line
                            int len = snprintf(line, sizeof(line), "%d %s %s %d\n", id, user, pass, active);
                            write(fd_new, line, len);
                        }
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
    }

    close(fd_old);
    close(fd_new);

    if (!found) {
        unlink("temp_customer.txt");
        sem_post(sem_userdb);
        send_message(connfd, "Customer not found.\n");
        return;
    }

    // Replace old file with new one
    rename("temp_customer.txt", "customer.txt");
    sem_post(sem_userdb);

    send_message(connfd, "✅ Customer details updated successfully.\n");
}

void process_loan_applications(int connfd, const char *username) {
    int fd = open("loan_db.txt", O_RDONLY);
    if (fd == -1) {
        send_message(connfd, "Error: cannot open loan_db.txt\n");
        return;
    }

    sem_wait(sem_account);  // shared with loans for sync

    char buf[512], line[256];
    ssize_t r;
    int pos = 0, found = 0;

    send_message(connfd, "\nYour Pending Loan Applications:\n--------------------------------\n");

    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                if (line[0] != '\0') {
                    int loan_id, amount;
                    char cust_user[64], assigned_emp[64], status[32];

                    if (sscanf(line, "%d %s %d %s %s", &loan_id, cust_user, &amount, assigned_emp, status) == 5) {
                        if (strcmp(assigned_emp, username) == 0 && strcmp(status, "pending") == 0) {
                            found = 1;
                            char msg[256];
                            int len = snprintf(msg, sizeof(msg),
                                "Loan ID: %d | Customer: %s | Amount: %d | Status: %s\n",
                                loan_id, cust_user, amount, status);
                            write(connfd, msg, len);
                        }
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
    }

    if (!found)
        send_message(connfd, "No pending loan applications assigned to you.\n");

    close(fd);
    sem_post(sem_account);
}

void approve_reject_loan(int connfd, const char *username) {
    int fd_old, fd_new;
    char buf[512], line[256];
    ssize_t r;
    int pos = 0;
    int found = 0;

    sem_wait(sem_account);

    fd_old = open("loan_db.txt", O_RDONLY);
    if (fd_old == -1) {
        sem_post(sem_account);
        send_message(connfd, "Error: cannot open loan_db.txt\n");
        return;
    }

    fd_new = open("temp_loan.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_new == -1) {
        close(fd_old);
        sem_post(sem_account);
        send_message(connfd, "Error: cannot create temp_loan.txt\n");
        return;
    }

    // Step 1: Display pending loans for this employee
    send_message(connfd, "\nPending Loans Assigned to You:\n--------------------------------\n");
    lseek(fd_old, 0, SEEK_SET);

    while ((r = read(fd_old, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                if (line[0] != '\0') {
                    int loan_id, amount;
                    char cust_user[64], emp[64], status[32];
                    if (sscanf(line, "%d %s %d %s %s", &loan_id, cust_user, &amount, emp, status) == 5) {
                        if (strcmp(emp, username) == 0 && strcmp(status, "pending") == 0) {
                            found = 1;
                            char msg[256];
                            int len = snprintf(msg, sizeof(msg),
                                "Loan ID: %d | Customer: %s | Amount: %d | Status: %s\n",
                                loan_id, cust_user, amount, status);
                            write(connfd, msg, len);
                        }
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
    }

    if (!found) {
        close(fd_old);
        close(fd_new);
        unlink("temp_loan.txt");
        sem_post(sem_account);
        send_message(connfd, "No pending loans assigned to you.\n");
        return;
    }

    // Step 2: Ask employee to choose a loan to approve/reject
    char loanid_str[16], action[16];
    send_message(connfd, "\nEnter Loan ID to process: ");
    if (receive_message(connfd, loanid_str, sizeof(loanid_str)) <= 0) {
        close(fd_old);
        close(fd_new);
        sem_post(sem_account);
        return;
    }
    trim_newline(loanid_str);
    int target_id = atoi(loanid_str);

    send_message(connfd, "Enter action (approve/reject): ");
    if (receive_message(connfd, action, sizeof(action)) <= 0) {
        close(fd_old);
        close(fd_new);
        sem_post(sem_account);
        return;
    }
    trim_newline(action);

    // Step 3: Reset file pointer and rewrite all loans
    lseek(fd_old, 0, SEEK_SET);
    pos = 0;
    found = 0;
    memset(buf, 0, sizeof(buf));

    while ((r = read(fd_old, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                if (line[0] != '\0') {
                    int loan_id, amount;
                    char cust_user[64], emp[64], status[32];

                    if (sscanf(line, "%d %s %d %s %s", &loan_id, cust_user, &amount, emp, status) == 5) {
                        if (loan_id == target_id && strcmp(emp, username) == 0 && strcmp(status, "pending") == 0) {
                            found = 1;
                            char new_status[32];
                            if (strcmp(action, "approve") == 0)
                                strcpy(new_status, "approved");
                            else if (strcmp(action, "reject") == 0)
                                strcpy(new_status, "rejected");
                            else {
                                send_message(connfd, "Invalid action. Use 'approve' or 'reject'.\n");
                                close(fd_old);
                                close(fd_new);
                                unlink("temp_loan.txt");
                                sem_post(sem_account);
                                return;
                            }

                            char newline[256];
                            int len = snprintf(newline, sizeof(newline),
                                "%d %s %d %s %s\n", loan_id, cust_user, amount, emp, new_status);
                            write(fd_new, newline, len);
                        } else {
                            write(fd_new, line, strlen(line));
                            write(fd_new, "\n", 1);
                        }
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
    }

    close(fd_old);
    close(fd_new);

    if (!found) {
        unlink("temp_loan.txt");
        sem_post(sem_account);
        send_message(connfd, "Error: Loan ID not found or not assigned to you.\n");
        return;
    }

    rename("temp_loan.txt", "loan_db.txt");
    sem_post(sem_account);
    send_message(connfd, "✅ Loan status updated successfully!\n");
}

void view_customer_transactions(int connfd) {
    char cust_username[64];

    // Ask for customer username
    send_message(connfd, "Enter the customer's username: ");
    if (receive_message(connfd, cust_username, sizeof(cust_username)) <= 0)
        return;
    trim_newline(cust_username);

    int fd = open("transactions_db.txt", O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        send_message(connfd, "Error: Cannot open transaction_db.txt\n");
        return;
    }

    sem_wait(sem_account);  // protect file access

    char line[512];
    int found = 0;

    // Read line-by-line
    while (read_line(fd, line, sizeof(line)) > 0) {
        if (strlen(line) < 3) continue;

        int tx_id;
        char uname[64], tx_type[32], timestamp[64];
        double amount, balance;

        // Parse transaction line
        int parsed = sscanf(line, "%d %63s %31s %lf %63s %lf",
                            &tx_id, uname, tx_type, &amount, timestamp, &balance);

        // Match username
        if (parsed == 6 && strcmp(uname, cust_username) == 0) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "Txn ID: %d | Type: %s | Amount: %.2f | Time: %s | Balance: %.2f\n",
                     tx_id, tx_type, amount, timestamp, balance);
            send_message(connfd, msg);
            found = 1;
        }
    }

    sem_post(sem_account);
    close(fd);

    if (!found)
        send_message(connfd, "No transactions found for this customer.\n");
    else
        send_message(connfd, "---- End of Customer Transactions ----\n");
}

  
void change_employee_password(int connfd) {
    char username[64], old_pass[64], new_pass[64];
    char buffer[512], line[256];
    char temp_file[] = "temp_emp.txt";
    int fd_read = -1, fd_write = -1;
    ssize_t bytes;
    int pos = 0;
    int found = 0;

    send_message(connfd, "Enter your employee username: ");
    if (receive_message(connfd, username, sizeof(username)) <= 0) return;
    trim_newline(username);

    send_message(connfd, "Enter your current password: ");
    if (receive_message(connfd, old_pass, sizeof(old_pass)) <= 0) return;
    trim_newline(old_pass);

    send_message(connfd, "Enter your new password: ");
    if (receive_message(connfd, new_pass, sizeof(new_pass)) <= 0) return;
    trim_newline(new_pass);

    sem_wait(sem_userdb);

    fd_read = open("employee.txt", O_RDONLY);
    if (fd_read < 0) {
        sem_post(sem_userdb);
        send_message(connfd, "Error opening Employee file.\n");
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
        if (rename(temp_file, "employee.txt") != 0) {
            send_message(connfd, "Error updating employee password.\n");
            unlink(temp_file);
        } else {
            send_message(connfd, "Employee password updated successfully.\n");
        }
    } else {
        unlink(temp_file);
        send_message(connfd, "Invalid username or password.\n");
    }

    sem_post(sem_userdb);
}
/* ------------------------------------------------------------
   EMPLOYEE MENU
   ------------------------------------------------------------ */
void employee_menu(int connfd, const char *username) {
    char choice_str[16];
    int choice = 0;
    
    char welcome[128];
    snprintf(welcome, sizeof(welcome), "\nWelcome, %s (Employee)\n", username);
    send_message(connfd, welcome);

    while (1) {
        send_message(connfd,
            "\n====== EMPLOYEE MENU ======\n"
            "1. Add New Customer\n"
            "2. Modify Customer Details\n"
            "3. Process Loan Applications (View Pending)\n"
            "4. Approve / Deny Loan Application\n"
            "5. View Customer Transactions (Passbook)\n"
            "6. Change Password\n"
            "7. Logout\n"
            "8. Exit\n"
            "Enter your choice: "
        );

        if (receive_message(connfd, choice_str, sizeof(choice_str)) <= 0)
            return;

        trim_newline(choice_str); 
        choice = atoi(choice_str);

        switch (choice) {
            case 1:
                add_new_customer(connfd);
                break;

            case 2:
                modify_customer_details_emp(connfd);
               break;

            case 3:
                process_loan_applications(connfd, username);
               break;

            case 4:
               approve_reject_loan(connfd, username);
                break;

            case 5:
               view_customer_transactions(connfd);
                break;

            case 6:
                change_employee_password(connfd);
                break;

            case 7:
               send_message(connfd, "Logging out...\n");
                return;  
                break;

            case 8:
                send_message(connfd, "Exiting system...\n");
                _exit(0);
                break;

            default:
                send_message(connfd, "Invalid choice. Try again.\n");
                break;
        }
    }
}
