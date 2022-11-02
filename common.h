#ifndef COMMON_H
#define COMMON_H

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
#include <map>

using namespace std;

namespace user_space {
    // int UID = 1;

    // int get_uid() {
    //     return UID++;
    // }


    class UserInfo {
    private:
        static int UID;

        int id, sock;
        string name;
        sockaddr_in addr;

    public:

        UserInfo() {}
        UserInfo(int id, int sock, string name, sockaddr_in addr) {
            this->id   = id;
            this->sock = sock;
            this->name = name;
            this->addr = addr;
        }

        /* Static methods */
        static UserInfo create_user(int sock, sockaddr_in addr) {
            // TODO: UID range 1~30
            static string default_name = string("(no name)");
            UserInfo user = UserInfo(UID, sock, default_name, addr);
            ++UID;

            return user; 
        }





        /* Member methods */
        int get_id()         { return this->id;  }
        int get_sockfd()     { return this->sock;  }
        string get_name()    { return this->name; }
        string get_ip_addr() { return string(inet_ntoa(this->addr.sin_addr));  }
        int get_port()       { return ntohs(this->addr.sin_port); }

        void set_name(string new_name) {
            this->name = new_name;
        }

        void show() {
            cout << "User Info" << endl
                << "\tuid: " << this->get_id() << endl
                << "\tname: " << this->get_name() << endl
                << "addr: " << this->get_ip_addr() << ":" << this->get_port() << endl;
        }
    };
    
    int UserInfo::UID = 1;

    /* User Table */
    map<int, UserInfo> user_table = {}; // uid: user

    void add_user(UserInfo user) {
        // user_table[user.get_id()] = user;
        pair<int, UserInfo> p(user.get_id(), user);
        user_table.insert(p);
    }

    void del_user(int id) {
        user_table.erase(id);
    }

    bool has_user(int id) {
        map<int, UserInfo>::iterator iter = user_table.find(id);
        return (iter != user_table.end());
    }

    UserInfo get_user_by_id(int id) {
        return user_table[id];
    }

    UserInfo get_user_by_sockfd(int sockfd) {
        int tid;
        for (auto &elem: user_table) {
            if (elem.second.get_sockfd() == sockfd) {
                tid = elem.first;
            }
        }
        return user_table[tid];
    }

    void show_table() {
        for (auto &elem: user_table) {
            elem.second.show();
        }
    }
}



void child_handler(int sig);
void interrupt_handler(int sig);

int get_listen_socket(const char *port);


void child_handler(int sig) {
    // Handle SIGCHLD
    // Prevent the zombie process
    int stat;

    while(waitpid(-1, &stat, WNOHANG) > 0) {
        // Remove zombie process
    }

    return;
}


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




#endif