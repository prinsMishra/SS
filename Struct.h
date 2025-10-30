#ifndef STRUCT_H
#define STRUCT_H

#include <stdint.h>

#define MAX_NAME       64
#define MAX_USERNAME   48
#define MAX_PASSWORD   64
#define MAX_FEEDBACK   256
#define MAX_REMARK     128

typedef enum {
    ROLE_ADMIN   = 1,
    ROLE_MANAGER = 2,
    ROLE_EMPLOYEE= 3,
    ROLE_CUSTOMER= 4
} UserRole;

// ---- User info (used for all role files: admin.txt, manager.txt, etc.) ----
typedef struct {
    int id;                          // Unique user ID
    char username[MAX_USERNAME];     // Username
    char password[MAX_PASSWORD];     // Password
    UserRole role;                   // Role type
    int active;      
    int logged_in;                // 1 = active, 0 = deactivated
} User;

// ---- Customer account ----
typedef struct {
    int account_no;                  // Account number
    int user_id;                     // Linked to user ID
    double balance;                  // Account balance
    int is_closed;                   // 0 = open, 1 = closed
} CustomerAccount;

// ---- Loan record ----
typedef struct {
    int loan_id;                     // Unique loan ID
    int user_id;                     // Customer ID
    double amount;                   // Loan amount
    double interest_rate;            // Interest rate
    int term_months;                 // Loan term in months
    int status;                      // 0 = applied, 1 = approved, 2 = rejected
    int assigned_employee_id;        // Employee processing this loan
} Loan;

// ---- Feedback ----
typedef struct {
    int fb_id;                       // Feedback ID
    int user_id;                     // Customer ID
    char message[MAX_FEEDBACK];      // Feedback text
} Feedback;

// ---- Transaction ----
typedef struct {
    int tx_id;                       // Transaction ID
    int account_no;                  // Account number
    int user_id;                     // Customer ID
    int tx_type;                     // 1=deposit, 2=withdraw, 3=transfer
    double amount;                   // Transaction amount
    char remark[MAX_REMARK];         // Note or description
} Transaction;

#endif // STRUCT_H
