/* Concurrent connection-oriented */
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include "npshell.h"

using namespace std;
using npshell::child_handler;
using npshell::run_npshell;

int get_listen_socket(const char *port) {
    struct sockaddr_in s_addr;
    int listen_sock;
    int status_code;

    // Init variable
    bzero((char *)&s_addr, sizeof(s_addr));

    // Init server address
    s_addr.sin_family = AF_INET;
    s_addr.sin_addr.s_addr = INADDR_ANY;
    s_addr.sin_port = htons((u_short)atoi(port));

    // Create socket
    listen_sock = socket(s_addr.sin_family, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("Server create socket");
        exit(0);
    }

    // Bind socket
    status_code = bind(listen_sock, (struct sockaddr *) &s_addr, sizeof(s_addr));
    if (status_code < 0) {
        perror("Server bind");
        exit(0);
    }

    // Listen socket
    status_code = listen(listen_sock, 128);
    if (status_code < 0) {
        perror("Server listen");
        exit(0);
    }

    return listen_sock;
}

int main(int argc,char const *argv[]) {
    if (argc != 2) {
        cout << "Usage: prog port" << endl;
        exit(0);
    }

    struct sockaddr_in c_addr;
    int listen_sock, client_sock, c_addr_len;
    int status_code, optval = 1;

    bzero((char *)&c_addr, sizeof(c_addr));

    listen_sock = get_listen_socket(argv[1]);
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    
    signal(SIGCHLD, child_handler);

    while (1) {
        c_addr_len = sizeof(c_addr);
        client_sock = accept(listen_sock, (struct sockaddr *) &c_addr, (socklen_t *) &c_addr_len);
        if (client_sock < 0) {
            perror("Sever accept");
            exit(0);
        }
        #if 0
        cout << "Accept connection: " << client_sock << endl;
        #endif

        pid_t pid = fork();
        if (pid < 0) {
            perror("Server fork");
            exit(0);
        }

        if (pid > 0) {
            /* Parent */
            close(client_sock);
        } else {
            /* Child */
            dup2(client_sock, STDIN_FILENO);
            dup2(client_sock, STDOUT_FILENO);
            dup2(client_sock, STDERR_FILENO);
            close(client_sock);
            close(listen_sock);
            run_npshell();
            exit(0);
        }
    }
}