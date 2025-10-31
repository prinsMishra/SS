// // #include "utils.h"
// // #include "Struct.h"
// // #include "admin_ops.c"
// // #include "manager_ops.c"
// // #include "customer_ops.c"
// // #include "employee_ops.c"
// // #include <sys/socket.h>
// // #include <sys/types.h>
// // #include <arpa/inet.h>
// // #include <unistd.h>
// // #include <stdlib.h>
// // #include <stdio.h>
// // #include <string.h>
// // #include <errno.h>
// // #include <sys/stat.h>
// // #include <signal.h>
// // #include <sys/wait.h>
// // #include <semaphore.h>
// // #include <fcntl.h>   // for O_CREAT in sem_open

// // #define PORT 9090
// // #define BACKLOG 10
// // #define BUFSZ 256

// // /* ------------------------------------------------------------
// //    Global named semaphores for system-wide coordination
// //    ------------------------------------------------------------ */
// // sem_t *sem_userdb;   // protects user DBs (login files)
// // sem_t *sem_account;  // will be used later for account operations
// // sem_t *sem_loan;
// // /* ------------------------------------------------------------
// //    Helper: ensure required data files and directories exist
// //    ------------------------------------------------------------ */
// // static void ensure_files_exist() {
// //     const char *files[] = {
// //         "admin.txt", "manager.txt", "employee.txt", "customer.txt"
// //     };
// //     for (int i = 0; i < 4; i++) {
// //         int fd = open(files[i], O_CREAT | O_RDWR, 0644);
// //         if (fd == -1) perror("open create");
// //         close(fd);
// //     }
// //     mkdir("accounts", 0755); // for future account files
// // }

// // /* ------------------------------------------------------------
// //    SIGCHLD handler to reap zombie children
// //    ------------------------------------------------------------ */
// // static void reap_children(int sig) {
// //     (void)sig;
// //     while (waitpid(-1, NULL, WNOHANG) > 0);
// // }

// // /* ------------------------------------------------------------
// //    Handle one client connection (child process)
// //    ------------------------------------------------------------ */
// // static void handle_client(int connfd) {
// //     char role[BUFSZ], username[BUFSZ], password[BUFSZ];
// //     char filename[64];
// //     int valid;

// //     while (1) {
// //         send_message(connfd, "\nEnter role (admin/manager/employee/customer): ");
// //         if (receive_message(connfd, role, sizeof(role)) <= 0)
// //             break;
// //         trim_newline(role);

// //         send_message(connfd, "Enter username: ");
// //         if (receive_message(connfd, username, sizeof(username)) <= 0)
// //             break;
// //         trim_newline(username);

// //         send_message(connfd, "Enter password: ");
// //         if (receive_message(connfd, password, sizeof(password)) <= 0)
// //             break;
// //         trim_newline(password);

// //         /* Select correct file based on role */
// //         if      (strcmp(role, "admin")    == 0) strcpy(filename, "admin.txt");
// //         else if (strcmp(role, "manager")  == 0) strcpy(filename, "manager.txt");
// //         else if (strcmp(role, "employee") == 0) strcpy(filename, "employee.txt");
// //         else if (strcmp(role, "customer") == 0) strcpy(filename, "customer.txt");
// //         else {
// //             send_message(connfd, "Invalid role.\n");
// //             continue;
// //         }

// //         /* ---------- Semaphore region ---------- */
// //         sem_wait(sem_userdb);
// //         valid = validate_login(filename, username, password);
// //         sem_post(sem_userdb);
// //         /* -------------------------------------- */

// //         if (valid) {
// //             send_message(connfd, "Login successful!\n");

// //             if (strcmp(role, "admin" ) == 0)
// //                 admin_menu(connfd, username);
// //             else if (strcmp(role, "manager") == 0)
// //                 manager_menu(connfd, username);
// //             else if (strcmp(role, "employee") == 0)
// //                 employee_menu(connfd ,username);    
// //             else if (strcmp(role, "customer") == 0)
// //                 customer_menu(connfd ,username); 
// //             else
// //                 send_message(connfd, "No operations implemented for this role yet.\n");

// //             // after logout (return from menu), continue loop to re-login
// //             send_message(connfd, "\nLogged out. Returning to login screen...\n");

// //         } else {
// //             send_message(connfd, "Invalid username or password.\n");
// //         }
// //     }

// //     // if receive_message() failed or client closed connection:
// //     send_message(connfd, "Disconnected from server.\n");
// //     close(connfd);
// //     _exit(0);
// // }

// // /* ------------------------------------------------------------
// //    Main server setup
// //    ------------------------------------------------------------ */
// // int main() {
// //     int listenfd, connfd;
// //     struct sockaddr_in servaddr, cliaddr;
// //     socklen_t clilen = sizeof(cliaddr);

// //     /* reap zombie children */
// //     signal(SIGCHLD, reap_children);

// //     ensure_files_exist();

// //     /* ---------- Semaphore initialization ---------- */
// //     sem_userdb = sem_open("/sem_userdb", O_CREAT, 0644, 1);
// //     sem_account = sem_open("/sem_account", O_CREAT, 0644, 1);
// //     if (sem_userdb == SEM_FAILED || sem_account == SEM_FAILED) {
// //         perror("sem_open");
// //         exit(EXIT_FAILURE);
// //     }
// //     /* ------------------------------------------------ */

// //     listenfd = socket(AF_INET, SOCK_STREAM, 0);
// //     if (listenfd < 0) error_exit("socket");

// //     int opt = 1;
// //     setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// //     memset(&servaddr, 0, sizeof(servaddr));
// //     servaddr.sin_family = AF_INET;
// //     servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
// //     servaddr.sin_port = htons(PORT);

// //     if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
// //         error_exit("bind");

// //     if (listen(listenfd, BACKLOG) < 0)
// //         error_exit("listen");

// //     printf("Server listening on port %d...\n", PORT);

// //     while (1) {
// //         connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
// //         if (connfd < 0) {
// //             if (errno == EINTR) continue;
// //             perror("accept");
// //             continue;
// //         }

// //         pid_t pid = fork();
// //         if (pid == 0) {
// //             close(listenfd);
// //             handle_client(connfd);
// //         } else if (pid > 0) {
// //             close(connfd);
// //         } else {
// //             perror("fork");
// //             close(connfd);
// //         }
// //     }

// //     close(listenfd);
// //     sem_close(sem_userdb);
// //     sem_close(sem_account);
// //     sem_unlink("/sem_userdb");
// //     sem_unlink("/sem_account");
// //     return 0;
// // }

#include "utils.h"
#include "Struct.h"
#include "admin_ops.c"
#include "manager_ops.c"
#include "customer_ops.c"
#include "employee_ops.c"
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>   // for O_CREAT in sem_open

#define PORT 9090
#define BACKLOG 10
#define BUFSZ 256

/* ------------------------------------------------------------
   Global named semaphores for system-wide coordination
   ------------------------------------------------------------ */
sem_t *sem_userdb;   // protects user DBs (login files)
sem_t *sem_account;  // protects account_db.txt
sem_t *sem_loan;     // ✅ protects loan_db.txt
/* ------------------------------------------------------------
   Helper: ensure required data files and directories exist
   ------------------------------------------------------------ */
static void ensure_files_exist() {
    const char *files[] = {
        "admin.txt", "manager.txt", "employee.txt", "customer.txt",
        "loan_db.txt", "account_db.txt", "feedback_db.txt"
    };
    for (int i = 0; i < 7; i++) {
        int fd = open(files[i], O_CREAT | O_RDWR, 0644);
        if (fd == -1) perror("open create");
        close(fd);
    }
    mkdir("accounts", 0755); // optional future directory
}

/* ------------------------------------------------------------
   SIGCHLD handler to reap zombie children
   ------------------------------------------------------------ */
static void reap_children(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* ------------------------------------------------------------
   Handle one client connection (child process)
   ------------------------------------------------------------ */
static void handle_client(int connfd) {
    char role[BUFSZ], username[BUFSZ], password[BUFSZ];
    char filename[64];
    int valid;

    while (1) {
        send_message(connfd, "\nEnter role (admin/manager/employee/customer): ");
        if (receive_message(connfd, role, sizeof(role)) <= 0)
            break;
        trim_newline(role);

        send_message(connfd, "Enter username: ");
        if (receive_message(connfd, username, sizeof(username)) <= 0)
            break;
        trim_newline(username);

        send_message(connfd, "Enter password: ");
        if (receive_message(connfd, password, sizeof(password)) <= 0)
            break;
        trim_newline(password);

        /* Select correct file based on role */
        if      (strcmp(role, "admin")    == 0) strcpy(filename, "admin.txt");
        else if (strcmp(role, "manager")  == 0) strcpy(filename, "manager.txt");
        else if (strcmp(role, "employee") == 0) strcpy(filename, "employee.txt");
        else if (strcmp(role, "customer") == 0) strcpy(filename, "customer.txt");
        else {
            send_message(connfd, "Invalid role.\n");
            continue;
        }

        /* ---------- Semaphore region ---------- */
        sem_wait(sem_userdb);
        valid = validate_login(filename, username, password);
        sem_post(sem_userdb);
        /* -------------------------------------- */

     if (valid == 1) { // 1 is now SUCCESS
            send_message(connfd, "Login successful!\n");

            if (strcmp(role, "admin" ) == 0)
                admin_menu(connfd, username);
            else if (strcmp(role, "manager") == 0)
                manager_menu(connfd, username);
            else if (strcmp(role, "employee") == 0)
                employee_menu(connfd, username);    
            else if (strcmp(role, "customer") == 0)
                customer_menu(connfd, username); 
            else
                send_message(connfd, "No operations implemented for this role yet.\n");

            // LOGOUT STEP: Mark user as logged out in the file after menu returns
            sem_wait(sem_userdb);
            mark_user_logged_out(filename, username); // CALL THE NEW FUNCTION
            sem_post(sem_userdb);
            
            // after logout (return from menu), continue loop to re-login
            send_message(connfd, "\nLogged out. Returning to login screen...\n");

        } else if (valid == -2) { // -2 is now ALREADY LOGGED IN
            send_message(connfd, "This user is already logged in from another session.\n");
        } else if (valid == 0) { // 0 is now INVALID CREDENTIALS or INACTIVE
            send_message(connfd, "Invalid username or password (or account is inactive).\n");
        }
    }

    send_message(connfd, "Disconnected from server.\n");
    close(connfd);
    _exit(0);
}

/* ------------------------------------------------------------
   Main server setup
   ------------------------------------------------------------ */
int main() {
    int listenfd, connfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen = sizeof(cliaddr);

    signal(SIGCHLD, reap_children);
    ensure_files_exist();

    /* ---------- Semaphore initialization ---------- */
    sem_userdb = sem_open("/sem_userdb", O_CREAT, 0644, 1);
    sem_account = sem_open("/sem_account", O_CREAT, 0644, 1);
    sem_loan = sem_open("/sem_loan", O_CREAT, 0644, 1);   // ✅ new

    if (sem_userdb == SEM_FAILED || sem_account == SEM_FAILED || sem_loan == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    /* ------------------------------------------------ */

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) error_exit("socket");

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        error_exit("bind");

    if (listen(listenfd, BACKLOG) < 0)
        error_exit("listen");

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(listenfd);
            handle_client(connfd);
        } else if (pid > 0) {
            close(connfd);
        } else {
            perror("fork");
            close(connfd);
        }
    }

    close(listenfd);
    /* ---------- Cleanup ---------- */
    sem_close(sem_userdb);
    sem_close(sem_account);
    sem_close(sem_loan);          // ✅ new
    sem_unlink("/sem_userdb");
    sem_unlink("/sem_account");
    sem_unlink("/sem_loan");      // ✅ new
    return 0;
}


// #include "utils.h"
// #include "Struct.h"
// #include "admin_ops.c"
// #include "manager_ops.c"
// #include "customer_ops.c"
// #include "employee_ops.c"
// #include <sys/socket.h>
// #include <sys/types.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>
// #include <errno.h>
// #include <sys/stat.h>
// #include <signal.h>
// #include <sys/wait.h>
// #include <semaphore.h>
// #include <fcntl.h>   // for O_CREAT in sem_open

// #define PORT 9090
// #define BACKLOG 10
// #define BUFSZ 256

// /* ------------------------------------------------------------
//    Global named semaphores for system-wide coordination
//    ------------------------------------------------------------ */
// sem_t *sem_userdb;   // protects user DBs (login files)
// sem_t *sem_account;  // protects account_db.txt
// sem_t *sem_loan;     // protects loan_db.txt
// /* ------------------------------------------------------------
//    Helper: ensure required data files and directories exist
//    ------------------------------------------------------------ */
// static void ensure_files_exist() {
//     const char *files[] = {
//         "admin.txt", "manager.txt", "employee.txt", "customer.txt",
//         "loan_db.txt", "account_db.txt", "feedback_db.txt", "transaction_db.txt"
//     };
//     for (int i = 0; i < (int)(sizeof(files)/sizeof(files[0])); i++) {
//         int fd = open(files[i], O_CREAT | O_RDWR, 0644);
//         if (fd == -1) perror("open create");
//         else close(fd);
//     }
//     /* optional directory for per-account files */
//     mkdir("accounts", 0755);
// }

// /* ------------------------------------------------------------
//    SIGCHLD handler to reap zombie children
//    ------------------------------------------------------------ */
// static void reap_children(int sig) {
//     (void)sig;
//     while (waitpid(-1, NULL, WNOHANG) > 0);
// }

// /* ------------------------------------------------------------
//    Handle one client connection (child process)
//    ------------------------------------------------------------ */
// static void handle_client(int connfd) {
//     char role[BUFSZ], username[BUFSZ], password[BUFSZ];
//     char filename[64];
//     int valid;

//     while (1) {
//         send_message(connfd, "\nEnter role (admin/manager/employee/customer): ");
//         if (receive_message(connfd, role, sizeof(role)) <= 0)
//             break;
//         trim_newline(role);

//         send_message(connfd, "Enter username: ");
//         if (receive_message(connfd, username, sizeof(username)) <= 0)
//             break;
//         trim_newline(username);

//         send_message(connfd, "Enter password: ");
//         if (receive_message(connfd, password, sizeof(password)) <= 0)
//             break;
//         trim_newline(password);

//         /* Select correct file based on role */
//         if      (strcmp(role, "admin")    == 0) strcpy(filename, "admin.txt");
//         else if (strcmp(role, "manager")  == 0) strcpy(filename, "manager.txt");
//         else if (strcmp(role, "employee") == 0) strcpy(filename, "employee.txt");
//         else if (strcmp(role, "customer") == 0) strcpy(filename, "customer.txt");
//         else {
//             send_message(connfd, "Invalid role.\n");
//             continue;
//         }

//         /* ---------- Semaphore region ---------- */
//         sem_wait(sem_userdb);
//         valid = validate_login(filename, username, password);
//         sem_post(sem_userdb);
//         /* -------------------------------------- */

//         if (valid == 1) {
//             send_message(connfd, "Login successful!\n");

//             if (strcmp(role, "admin" ) == 0) {
//                 admin_menu(connfd, username);
//             } else if (strcmp(role, "manager") == 0) {
//                 manager_menu(connfd, username);
//             } else if (strcmp(role, "employee") == 0) {
//                 employee_menu(connfd, username);
//             } else if (strcmp(role, "customer") == 0) {
//                 customer_menu(connfd, username);
//             } else {
//                 send_message(connfd, "No operations implemented for this role yet.\n");
//             }

//             /* User returned from their menu -> mark logged out */
//             sem_wait(sem_userdb);
//             mark_user_logged_out(filename, username);
//             sem_post(sem_userdb);

//             send_message(connfd, "\nLogged out. Returning to login screen...\n");
//         } else if (valid == -1) {
//             send_message(connfd, "Your account is deactivated. Contact admin.\n");
//         } else if (valid == -2) {
//             send_message(connfd, "This user is already logged in from another session.\n");
//         } else {
//             send_message(connfd, "Invalid username or password.\n");
//         }
//     }

//     /* client disconnected / receive_message failed */
//     send_message(connfd, "Disconnected from server.\n");
//     close(connfd);
//     _exit(0);
// }

// /* ------------------------------------------------------------
//    Main server setup
//    ------------------------------------------------------------ */
// int main() {
//     int listenfd, connfd;
//     struct sockaddr_in servaddr, cliaddr;
//     socklen_t clilen = sizeof(cliaddr);

//     /* reap zombie children */
//     signal(SIGCHLD, reap_children);

//     ensure_files_exist();

//     /* ---------- Semaphore initialization ---------- */
//     sem_userdb = sem_open("/sem_userdb", O_CREAT, 0644, 1);
//     sem_account = sem_open("/sem_account", O_CREAT, 0644, 1);
//     sem_loan = sem_open("/sem_loan", O_CREAT, 0644, 1);

//     if (sem_userdb == SEM_FAILED || sem_account == SEM_FAILED || sem_loan == SEM_FAILED) {
//         perror("sem_open");
//         exit(EXIT_FAILURE);
//     }
//     /* ------------------------------------------------ */

//     listenfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (listenfd < 0) error_exit("socket");

//     int opt = 1;
//     setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

//     memset(&servaddr, 0, sizeof(servaddr));
//     servaddr.sin_family = AF_INET;
//     servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
//     servaddr.sin_port = htons(PORT);

//     if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
//         error_exit("bind");

//     if (listen(listenfd, BACKLOG) < 0)
//         error_exit("listen");

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
//         if (connfd < 0) {
//             if (errno == EINTR) continue;
//             perror("accept");
//             continue;
//         }

//         pid_t pid = fork();
//         if (pid == 0) {
//             /* child */
//             close(listenfd);
//             handle_client(connfd);
//             /* handle_client should _exit when finished */
//             _exit(0);
//         } else if (pid > 0) {
//             /* parent */
//             close(connfd);
//         } else {
//             perror("fork");
//             close(connfd);
//         }
//     }

//     /* cleanup (not reachable in current design but good practice) */
//     close(listenfd);
//     sem_close(sem_userdb);
//     sem_close(sem_account);
//     sem_close(sem_loan);
//     sem_unlink("/sem_userdb");
//     sem_unlink("/sem_account");
//     sem_unlink("/sem_loan");
//     return 0;
// }
