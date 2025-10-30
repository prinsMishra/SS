
#include "utils.h"
#include "Struct.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>

// External semaphores declared in server.c
extern sem_t *sem_userdb;
extern sem_t *sem_account;
extern sem_t *sem_loan;


void view_balance(int connfd, const char *username) {
    int fd;
    char buf[512], line[256];
    ssize_t r;
    int pos = 0;
    int found = 0;

    sem_wait(sem_account);  // Lock account file while reading

    fd = open("account_db.txt", O_RDONLY);
    if (fd == -1) {
        sem_post(sem_account);
        send_message(connfd, "Error: cannot open account_db.txt\n");
        return;
    }

    // Read line-by-line manually using system calls
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                if (line[0] != '\0') {
                    int acc_id, balance;
                    char user[64];

                    // Parse one line: id username balance
                    if (sscanf(line, "%d %s %d", &acc_id, user, &balance) == 3) {
                        if (strcmp(user, username) == 0) {
                            found = 1;
                            char msg[128];
                            int len = snprintf(msg, sizeof(msg),
                                "\nYour current account balance: ₹%d\n", balance);
                            write(connfd, msg, len);
                            break;
                        }
                    }
                }
            } else {
                line[pos++] = buf[i];
            }
        }
        if (found) break;  // Stop reading once found
    }

    close(fd);
    sem_post(sem_account);  // Unlock file

    if (!found)
        send_message(connfd, "Error: Account not found.\n");
}

void deposit_money(int connfd, const char *username) {
    char amount_str[32];
    int amount;

    send_message(connfd, "Enter amount to deposit: ");
    if (receive_message(connfd, amount_str, sizeof(amount_str)) <= 0) return;
    trim_newline(amount_str);
    amount = atoi(amount_str);

    if (amount <= 0) {
        send_message(connfd, "Invalid deposit amount.\n");
        return;
    }

    sem_wait(sem_account); // lock account file
    int fd_old = open("account_db.txt", O_RDONLY);
    if (fd_old == -1) {
        sem_post(sem_account);
        send_message(connfd, "Error: cannot open account_db.txt\n");
        return;
    }

    int fd_new = open("temp_account.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_new == -1) {
        close(fd_old);
        sem_post(sem_account);
        send_message(connfd, "Error: cannot create temp file.\n");
        return;
    }

    char buf[512], line[256];
    ssize_t r;
    int pos = 0, found = 0;
    int new_balance = 0;

    // Read each account record and update target user's balance
    while ((r = read(fd_old, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;
                if (line[0] != '\0') {
                    int acc_id, balance;
                    char user[64];
                    if (sscanf(line, "%d %s %d", &acc_id, user, &balance) == 3) {
                        if (strcmp(user, username) == 0) {
                            found = 1;
                            new_balance = balance + amount;
                            char newline[128];
                            int len = snprintf(newline, sizeof(newline),
                                "%d %s %d\n", acc_id, user, new_balance);
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
        unlink("temp_account.txt");
        sem_post(sem_account);
        send_message(connfd, "Error: Account not found.\n");
        return;
    }

    rename("temp_account.txt", "account_db.txt");
    sem_post(sem_account);

    // Send confirmation
    char msg[128];
    int len = snprintf(msg, sizeof(msg),
        "Deposit successful! New balance: ₹%d\n", new_balance);
    write(connfd, msg, len);

    /* ---------- Transaction Logging ---------- */
    sem_wait(sem_account);
    int fd_txn = open("transactions_db.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_txn != -1) {
        int txn_id = get_next_id("transactions_db.txt");

        // Generate simple timestamp
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char ts[64];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d_%02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        char log_entry[256];
        int log_len = snprintf(log_entry, sizeof(log_entry),
            "%d %s deposit %d %s %d\n",
            txn_id, username, amount, ts, new_balance);

        write(fd_txn, log_entry, log_len);
        close(fd_txn);
    }
    sem_post(sem_account);
}

void withdraw_money(int connfd, const char *username) {
    char amount_str[32];
    int amount;

    send_message(connfd, "Enter amount to withdraw: ");
    if (receive_message(connfd, amount_str, sizeof(amount_str)) <= 0) return;
    trim_newline(amount_str);
    amount = atoi(amount_str);

    if (amount <= 0) {
        send_message(connfd, "Invalid withdrawal amount.\n");
        return;
    }

    sem_wait(sem_account);
    int fd_old = open("account_db.txt", O_RDONLY);
    if (fd_old == -1) {
        sem_post(sem_account);
        send_message(connfd, "Error: cannot open account_db.txt\n");
        return;
    }

    int fd_new = open("temp_account.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_new == -1) {
        close(fd_old);
        sem_post(sem_account);
        send_message(connfd, "Error: cannot create temp_account.txt\n");
        return;
    }

    char buf[512], line[256];
    ssize_t r;
    int pos = 0, found = 0;
    int new_balance = 0, current_balance = 0;

    while ((r = read(fd_old, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;

                if (line[0] != '\0') {
                    int acc_id, balance;
                    char user[64];
                    if (sscanf(line, "%d %s %d", &acc_id, user, &balance) == 3) {
                        if (strcmp(user, username) == 0) {
                            found = 1;
                            current_balance = balance;

                            if (amount > balance) {
                                close(fd_old);
                                close(fd_new);
                                unlink("temp_account.txt");
                                sem_post(sem_account);
                                send_message(connfd, "Insufficient balance.\n");
                                return;
                            }

                            new_balance = balance - amount;
                            char newline[128];
                            int len = snprintf(newline, sizeof(newline),
                                "%d %s %d\n", acc_id, user, new_balance);
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
        unlink("temp_account.txt");
        sem_post(sem_account);
        send_message(connfd, "Error: Account not found.\n");
        return;
    }

    rename("temp_account.txt", "account_db.txt");
    sem_post(sem_account);

    char msg[128];
    int len = snprintf(msg, sizeof(msg),
        "Withdrawal successful! New balance: ₹%d\n", new_balance);
    write(connfd, msg, len);

    /* ---------- Transaction Logging ---------- */
    sem_wait(sem_account);
    int fd_txn = open("transactions_db.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_txn != -1) {
        int txn_id = get_next_id("transactions_db.txt");

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char ts[64];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d_%02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        char log_entry[256];
        int log_len = snprintf(log_entry, sizeof(log_entry),
            "%d %s withdraw %d %s %d\n",
            txn_id, username, amount, ts, new_balance);

        write(fd_txn, log_entry, log_len);
        close(fd_txn);
    }
    sem_post(sem_account);
}

void transfer_funds(int connfd, const char *username) {
    char receiver[64], amount_str[32];
    int amount;

    send_message(connfd, "Enter recipient username: ");
    if (receive_message(connfd, receiver, sizeof(receiver)) <= 0) return;
    trim_newline(receiver);

    if (strcmp(receiver, username) == 0) {
        send_message(connfd, "Error: Cannot transfer to your own account.\n");
        return;
    }

    send_message(connfd, "Enter amount to transfer: ");
    if (receive_message(connfd, amount_str, sizeof(amount_str)) <= 0) return;
    trim_newline(amount_str);
    amount = atoi(amount_str);

    if (amount <= 0) {
        send_message(connfd, "Invalid amount.\n");
        return;
    }

    sem_wait(sem_account);

    int fd_old = open("account_db.txt", O_RDONLY);
    if (fd_old == -1) {
        sem_post(sem_account);
        send_message(connfd, "Error: cannot open account_db.txt\n");
        return;
    }

    int fd_new = open("temp_account.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_new == -1) {
        close(fd_old);
        sem_post(sem_account);
        send_message(connfd, "Error: cannot create temp_account.txt\n");
        return;
    }

    char buf[512], line[256];
    ssize_t r;
    int pos = 0;
    int found_sender = 0, found_receiver = 0;
    int sender_balance = 0, receiver_balance = 0;
    int new_sender_balance = 0, new_receiver_balance = 0;

    // --- Read & update both accounts ---
    while ((r = read(fd_old, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n' || pos >= (int)sizeof(line) - 1) {
                line[pos] = '\0';
                pos = 0;
                if (line[0] != '\0') {
                    int acc_id, balance;
                    char user[64];
                    if (sscanf(line, "%d %s %d", &acc_id, user, &balance) == 3) {
                        if (strcmp(user, username) == 0) {
                            found_sender = 1;
                            sender_balance = balance;

                            if (balance < amount) {
                                close(fd_old);
                                close(fd_new);
                                unlink("temp_account.txt");
                                sem_post(sem_account);
                                send_message(connfd, "❌ Insufficient balance.\n");
                                return;
                            }

                            new_sender_balance = balance - amount;
                            char newline[128];
                            int len = snprintf(newline, sizeof(newline),
                                "%d %s %d\n", acc_id, user, new_sender_balance);
                            write(fd_new, newline, len);
                        } 
                        else if (strcmp(user, receiver) == 0) {
                            found_receiver = 1;
                            receiver_balance = balance;
                            new_receiver_balance = balance + amount;
                            char newline[128];
                            int len = snprintf(newline, sizeof(newline),
                                "%d %s %d\n", acc_id, user, new_receiver_balance);
                            write(fd_new, newline, len);
                        } 
                        else {
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

    if (!found_sender) {
        unlink("temp_account.txt");
        sem_post(sem_account);
        send_message(connfd, "Error: Your account not found.\n");
        return;
    }

    if (!found_receiver) {
        unlink("temp_account.txt");
        sem_post(sem_account);
        send_message(connfd, "Error: Receiver account not found.\n");
        return;
    }

    rename("temp_account.txt", "account_db.txt");
    sem_post(sem_account);

    // Notify sender
    char msg[128];
    int len = snprintf(msg, sizeof(msg),
        "Transfer successful! New balance: ₹%d\n", new_sender_balance);
    write(connfd, msg, len);

    /* ---------- Transaction Logging ---------- */
    sem_wait(sem_account);
    int fd_txn = open("transactions_db.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_txn != -1) {
        int txn_id = get_next_id("transactions_db.txt");

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char ts[64];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d_%02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        char log1[256], log2[256];
        int len1 = snprintf(log1, sizeof(log1),
            "%d %s transfer-out %d %s %d\n",
            txn_id, username, amount, ts, new_sender_balance);
        int len2 = snprintf(log2, sizeof(log2),
            "%d %s transfer-in %d %s %d\n",
            txn_id + 1, receiver, amount, ts, new_receiver_balance);

        write(fd_txn, log1, len1);
        write(fd_txn, log2, len2);
        close(fd_txn);
    }
    sem_post(sem_account);
}

void apply_for_loan(int connfd, const char *username) {
    char amount_str[32];
    int amount;

    send_message(connfd, "Enter loan amount: ");
    if (receive_message(connfd, amount_str, sizeof(amount_str)) <= 0) return;
    trim_newline(amount_str);
    amount = atoi(amount_str);

    if (amount <= 0) {
        send_message(connfd, "Invalid loan amount.\n");
        return;
    }

    sem_wait(sem_loan);

    int fd = open("loan_db.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) {
        sem_post(sem_loan);
        send_message(connfd, "Error: cannot open loan_db.txt\n");
        return;
    }

    int loan_id = get_next_id("loan_db.txt");
    if (loan_id < 100) loan_id = 100;  // Start loan IDs from 100

    char entry[256];
    int len = snprintf(entry, sizeof(entry), "%d %s %d none pending\n",
                       loan_id, username, amount);
    write(fd, entry, len);
    close(fd);
    sem_post(sem_loan);

    send_message(connfd, " Loan application submitted successfully! Status: pending\n");
}

void change_customer_password(int connfd, const char *username) {
    char old_pass[64], new_pass[64];

    send_message(connfd, "Enter old password: ");
    if (receive_message(connfd, old_pass, sizeof(old_pass)) <= 0) return;
    trim_newline(old_pass);

    send_message(connfd, "Enter new password: ");
    if (receive_message(connfd, new_pass, sizeof(new_pass)) <= 0) return;
    trim_newline(new_pass);

    sem_wait(sem_userdb);

    int fd_old = open("customer.txt", O_RDONLY);
    if (fd_old == -1) {
        sem_post(sem_userdb);
        send_message(connfd, "Error: cannot open customer.txt\n");
        return;
    }

    int fd_new = open("temp_customer.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_new == -1) {
        close(fd_old);
        sem_post(sem_userdb);
        send_message(connfd, "Error: cannot create temp_customer.txt\n");
        return;
    }

    char buf[512], line[256];
    ssize_t r;
    int pos = 0;
    int found = 0;

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
                            if (strcmp(pass, old_pass) != 0) {
                                close(fd_old);
                                close(fd_new);
                                unlink("temp_customer.txt");
                                sem_post(sem_userdb);
                                send_message(connfd, "❌ Incorrect old password.\n");
                                return;
                            }

                            found = 1;
                            char newline[256];
                            int len = snprintf(newline, sizeof(newline),
                                               "%d %s %s %d\n", id, user, new_pass, active);
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
        unlink("temp_customer.txt");
        sem_post(sem_userdb);
        send_message(connfd, "❌ Customer not found.\n");
        return;
    }

    rename("temp_customer.txt", "customer.txt");
    sem_post(sem_userdb);

    send_message(connfd, "✅ Password updated successfully.\n");
}


void add_feedback(int connfd, const char *username) {
    char feedback[256];

    send_message(connfd, "Enter your feedback (max 200 chars): ");
    if (receive_message(connfd, feedback, sizeof(feedback)) <= 0) return;
    trim_newline(feedback);

    if (strlen(feedback) == 0) {
        send_message(connfd, "Feedback cannot be empty.\n");
        return;
    }

    sem_wait(sem_userdb);  // lock feedback file (safe shared use)

    int fd = open("feedback_db.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) {
        sem_post(sem_userdb);
        send_message(connfd, "Error: cannot open feedback_db.txt\n");
        return;
    }

    int feedback_id = get_next_id("feedback_db.txt");

    char entry[512];
    int len = snprintf(entry, sizeof(entry), "%d %s %s\n", feedback_id, username, feedback);

    write(fd, entry, len);
    close(fd);

    sem_post(sem_userdb);

    send_message(connfd, "✅ Thank you! Your feedback has been recorded.\n");
}






void customer_menu(int connfd, const char *username) {
    char choice_str[8];
    int choice = 0;

    // Greet the logged-in customer
    char welcome_msg[128];
    snprintf(welcome_msg, sizeof(welcome_msg),
             "\nWelcome, %s (Customer)\n", username);
    send_message(connfd, welcome_msg);

    while (1) {
        send_message(connfd,
            "\n====== CUSTOMER MENU ======\n"
            "1. View Account Balance\n"
            "2. Deposit Money\n"
            "3. Withdraw Money\n"
            "4. Transfer Funds\n"
            "5. Apply for a Loan\n"
            "6. Change Password\n"
            "7. Add Feedback\n"
            "8. View Transaction History\n"
            "9. Logout\n"
            "10. Exit\n"
            "Enter your choice: "
        );

        if (receive_message(connfd, choice_str, sizeof(choice_str)) <= 0)
            return;
        trim_newline(choice_str);
        choice = atoi(choice_str);

        switch (choice) {
            case 1:
                view_balance(connfd, username);
                break;
            case 2:
                deposit_money(connfd, username);
                break;
            case 3:
               withdraw_money(connfd, username);
              break;
            case 4:
               transfer_funds(connfd, username);
                break;
             case 5:
               apply_for_loan(connfd, username);
                 break;
            case 6:
               change_customer_password(connfd, username);
                 break;
             case 7:
              add_feedback(connfd, username);
              break;
            // case 8:
            //     view_transaction_history(connfd, username);
            //     break;
            case 9:
                send_message(connfd, "Logging out...\n");
                return;
            case 10:
                send_message(connfd, "Exiting system...\n");
                _exit(0);
            default:
                send_message(connfd, "Invalid choice. Try again.\n");
                break;
        }
    }
}
