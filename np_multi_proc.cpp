#include "np_multi_proc.h"

int main(int argc,char const *argv[]) {
    if (argc != 2) {
        cout << "Usage: prog port" << endl;
        exit(0);
    }

    /* Initialize shared memory */
    init_shm();
    init_lock();

    /* Setup server signal handler */
    signal(SIGCHLD, signal_server_handler);
    signal(SIGINT, signal_server_handler);
    signal(SIGQUIT, signal_server_handler);
    signal(SIGTERM, signal_server_handler);
    // signal(SIGUSR2, signal_server_handler);

    /* Variables */
    struct sockaddr_in c_addr;
    int client_sock, c_addr_len;
    int status_code;

    // Initilize variables
    bzero((char *)&c_addr, sizeof(c_addr));
    c_addr_len = sizeof(c_addr);
    listen_sock = get_listen_socket(argv[1]);

    while (true) {
        client_sock = accept(listen_sock, (struct sockaddr *) &c_addr, (socklen_t *) &c_addr_len);
        if (client_sock < 0) {
            perror("Sever accept");
            exit(0);
        }

        if (is_user_up_to_limit()) {
            cerr << "Online users are up to limit (" << USER_LIMIT << ")" << endl;
            close(client_sock);
            continue;
        } 
        pid_t pid = fork();

        if (pid > 0) {
            /* Parent */
            close(client_sock);
        } else if (pid == 0) {
            /* Child */
            // Setup signal handler
            signal(SIGUSR1, signal_child_handler);
            // signal(SIGUSR2, signal_child_handler);
            signal(SIGINT, signal_child_handler);
            signal(SIGQUIT, signal_child_handler);
            signal(SIGTERM, signal_child_handler);
            // Create user
            int uid = create_user(client_sock, c_addr);
            // dup2(client_sock, STDIN_FILENO);
            // dup2(client_sock, STDOUT_FILENO);
            // dup2(client_sock, STDERR_FILENO);
            // close(client_sock);
            serve_client(uid);
        } else {
            perror("Server fork");
            server_exit_procedure();
        }
        
        #if 0
        debug_user();
        #endif
    }
}