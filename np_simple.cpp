/* Server 1 */
/* Concurrent connection-oriented */

#include "common.h"
#include "npshell.h"

using namespace std;
using npshell::child_handler;
using npshell::run_npshell;

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