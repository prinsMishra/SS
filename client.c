#include "utils.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>



#define PORT 9090
#define BUFSZ 256

int main() {
    int sockfd;
    struct sockaddr_in servaddr;
    char buffer[BUFSZ];

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // connect to localhost

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server on port %d\n", PORT);

    // Interactive login process
    while (1) {
        // Receive message from server
        if (receive_message(sockfd, buffer, sizeof(buffer)) <= 0)
            break;
        printf("%s", buffer);

        // Take input from user
        if (!fgets(buffer, sizeof(buffer), stdin))
            break;
        trim_newline(buffer);

        // Send to server
        send_message(sockfd, buffer);

        // If server confirms login or error, break appropriately
        if (strstr(buffer, "exit") != NULL || strstr(buffer, "quit") != NULL)
            break;
    }

    // Final response from server (Login result)
    if (receive_message(sockfd, buffer, sizeof(buffer)) > 0)
        printf("%s", buffer);

    printf("Disconnected.\n");
    close(sockfd);
    return 0;
}
