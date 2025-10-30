
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
            case 8:
                view_transaction_history(connfd, username);
                break;
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
