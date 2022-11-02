/* Server 2 */
/* Single-process concurrent (use select) */
#include "common.h"

#define USER_LIMIT   31

using namespace std;
using namespace user_space;

int listen_sock;

void interrupt_handler(int sig) {
    // Handle SIGINT
    close(listen_sock);
    exit(0);
}


int main(int argc,char const *argv[]) {
    if (argc != 2) {
        cout << "Usage: prog port" << endl;
        exit(0);
    }
    signal(SIGINT, interrupt_handler);

    struct sockaddr_in c_addr;
    int client_sock, c_addr_len;
    int status_code, optval;
    int nfds = USER_LIMIT;
    fd_set afds, rfds;

    // Initilize variables
    bzero((char *)&c_addr, sizeof(c_addr));
    c_addr_len = sizeof(c_addr);
    optval = 1;
    FD_ZERO(&afds);

    listen_sock = get_listen_socket(argv[1]);
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
        perror("Set socket option");
        exit(0);
    }

    FD_SET(listen_sock, &afds);
    // Initilize variables done
    

    while (1) {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0) {
            perror("select error");
            exit(0);
        }

        if(FD_ISSET(listen_sock, &rfds)) {
            client_sock = accept(listen_sock, (struct sockaddr *) &c_addr, (socklen_t *) &c_addr_len);
            if (client_sock < 0) {
                perror("Sever accept");
                exit(0);
            }
            FD_SET(client_sock, &afds);
            UserInfo user = UserInfo::create_user(client_sock, c_addr);
            add_user(user);
            #if 1
            show_table();
            #endif
        }
        
        // Check if there are sockets are ready to be read
        for(int fd = 3; fd < nfds; ++fd) {
            if(fd != listen_sock && FD_ISSET(fd, &rfds)) {
                char buf[128]; bzero(buf, 128);
                int n = read(fd, buf, 128);
                cout << "(" << n << ") " << "Read: " << buf << endl;

                // User leave
                close(fd);
                FD_CLR(fd, &afds);
                del_user(get_user_by_sockfd(fd).get_id());
            }
        }


    }

}