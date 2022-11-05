/* Server 2 */
/* Single-process concurrent (use select) */
#include "np_single_proc.h"

using namespace std;
using namespace user_space;

int listen_sock;

void interrupt_handler(int sig) {
    // Handle SIGINT
    close(listen_sock);
    for (auto &elem: user_table.table) {
        close(elem.second->get_sockfd());
    }
    exit(0);
}

int main(int argc,char const *argv[]) {
    // if (argc != 2) {
    //     cout << "Usage: prog port" << endl;
    //     exit(0);
    // }
    signal(SIGINT, interrupt_handler);

    struct sockaddr_in c_addr;
    int client_sock, c_addr_len;
    int status_code;
    int nfds;
    fd_set afds, rfds;

    // Initilize variables
    bzero((char *)&c_addr, sizeof(c_addr));
    c_addr_len = sizeof(c_addr);
    FD_ZERO(&afds);

    // listen_sock = get_listen_socket(argv[1]);
    listen_sock = get_listen_socket("12345");
    nfds = listen_sock;

    FD_SET(listen_sock, &afds);
    // Initilize variables done
    

    while (1) {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds+1, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0) {
            perror("select error");
            exit(0);
        }

        if(FD_ISSET(listen_sock, &rfds)) {
            client_sock = accept(listen_sock, (struct sockaddr *) &c_addr, (socklen_t *) &c_addr_len);
            if (client_sock < 0) {
                perror("Sever accept");
                exit(0);
            }

            int uid = user_table.create_user(client_sock, c_addr);

            if (uid < 0) {
                cerr << "Online users are up to limit (" << USER_LIMIT << ")" << endl;
                close(client_sock);
            } else {
                FD_SET(client_sock, &afds);
                UserInfo *client = user_table.get_user_by_id(uid);

                welcome(client);
                login_prompt(client);
                command_prompt(client);

                if (nfds < client_sock) {
                    nfds = client_sock;
                }

                #if 1
                // user_table.show_table();
                cout << "Online users: " << user_table.table.size() << endl;
                #endif
            }
        }
        
        // Check if there are sockets are ready to be read
        for(auto &elem: user_table.table) {
            int fd = elem.second->get_sockfd();

            if (fd != listen_sock && FD_ISSET(fd, &rfds)) {
                int n = handle_client(fd);

                if (n == BUILT_IN_EXIT) {
                    // User leave
                    FD_CLR(fd, &afds);
                }
            }
        }

        // Clean the exit users
        if (user_table.del_queue.size() > 0) {
            user_table.del_process(user_pipes);
            #if 1
            cout << "Online users: " << user_table.table.size() << endl;
            #endif
        }
    }
    
    return 0;
}