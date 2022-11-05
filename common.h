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
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <vector>

using namespace std;

#define MAX_BUF_SIZE    15000
#define BUILT_IN_EXIT   99
#define BUILT_IN_TRUE   1
#define BUILT_IN_FALSE  0
#define USER_LIMIT      30

typedef struct mypipe {
    int in;
    int out;
} Pipe;

typedef struct my_number_pipe {
    int in;
    int out;
    int number;
} NumberPipe;

typedef struct my_command {
    string cmd;           // Cut by number|error pipe
    vector<string> cmds;  // Split by pipe
    int number;           // For number pipe
    int in_fd, out_fd;    // For user pipe
} Command;

typedef struct my_user_pipe {
    int src_uid;
    int dst_uid;
    Pipe pipe;
    bool is_done;
} UserPipe;

namespace user_space {
    class UserInfo {
    private:
        static int UID;

        int id, sock;
        string name;
        sockaddr_in addr;
        map<string, string> env;

    public:
        vector<Pipe> pipes;
        vector<NumberPipe> number_pipes;

        UserInfo() {}
        UserInfo(int id, int sock, string name, sockaddr_in addr) {
            this->id   = id;
            this->sock = sock;
            this->name = name;
            this->addr = addr;
            this->env = {{"PATH", "bin:."}};
        }

        /* Static methods */
        static UserInfo *create_user(int sock, sockaddr_in addr) {
            // TODO: UID range 1~30
            static string default_name = string("(no name)");
            UserInfo *user = new UserInfo(UID, sock, default_name, addr);
            ++UID;

            return user; 
        }

        /* Member methods */
        int get_id()         { return this->id;  }
        int get_sockfd()     { return this->sock;  }
        string get_name()    { return this->name; }
        string get_ip_addr() { return string(inet_ntoa(this->addr.sin_addr));  }
        int get_port()       { return ntohs(this->addr.sin_port); }
        map<string, string> get_env() { return this->env; }

        void set_name(string new_name) {
            this->name = new_name;
        }

        void set_env(string key, string val) {
            this->env[key] = val;
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
    class UserTable {
    public:
        map<int, UserInfo *> table; // uid: user
        vector<int> del_queue;

        UserTable() {
            this->table = {};
        }

        int create_user(int sock, sockaddr_in addr) {
            static string default_name = string("(no name)");
            int uid = -1;

            if (this->table.size() <= USER_LIMIT) {
                for (int x = 1; x <= USER_LIMIT; x++) {
                    if (!this->has_user(x)) {
                        UserInfo *user = new UserInfo(x, sock, default_name, addr);
                        this->add_user(user);
                        uid = x;
                        break;
                    }
                }
            }

            return uid;
        }

        void add_user(UserInfo *user) {
            // user_table[user.get_id()] = user;
            pair<int, UserInfo *> p(user->get_id(), user);
            this->table.insert(p);
        }

        bool has_user(int id) {
            map<int, UserInfo *>::iterator iter = this->table.find(id);
            return (iter != this->table.end());
        }

        void put_user_to_del_queue(int id) {
            this->del_queue.push_back(id);
        }

        void del_process(vector<UserPipe> &user_pipes) {
            for (auto uid: this->del_queue) {
                #if 0
                cerr << "Delete Process: uid = " << uid << endl;
                #endif

                // Clean unread message
                for (int x=0; x < user_pipes.size(); ++x) {
                    if (uid == user_pipes[x].dst_uid) {
                        user_pipes.erase(user_pipes.begin() + x);
                        --x;
                    }
                }
                // Delete user
                delete this->table[uid];
                this->table.erase(uid);
            }
            this->del_queue.clear();
        }

        UserInfo *get_user_by_id(int id) {
            return this->table[id];
        }

        UserInfo *get_user_by_sockfd(int sockfd) {
            int tid;
            for (auto &elem: this->table) {
                if (elem.second->get_sockfd() == sockfd) {
                    tid = elem.first;
                }
            }
            return this->table[tid];
        }

        void show_table() {
            for (auto &elem: this->table) {
                elem.second->show();
            }
        }
    };

    UserTable user_table;
    /* User Table End */
}

/* Function Prototype */
void child_handler(int sig);
void interrupt_handler(int sig);

void load_user_config(user_space::UserInfo *me);
int get_listen_socket(const char *port);

string read_msg(int sockfd);
void sendout_msg(int sockfd, string &msg);

void broadcast(string msg);
void login_prompt();
void logout_prompt();
void command_prompt(user_space::UserInfo *me);
void welcome(user_space::UserInfo *me);

// Built-in Command
void my_setenv(user_space::UserInfo *me, string var, string value);
void my_printenv(user_space::UserInfo *me, string var);
void my_exit(user_space::UserInfo *me);
void who(user_space::UserInfo *me);
void tell(int id, string msg);
void yell(string msg);
void name_cmd(string name);
int handle_builtin(user_space::UserInfo *me, string cmd);
// Built-in Command End

/* Function Prototype End */

/* Function Definition */
void load_user_config(user_space::UserInfo *me) {
    map<string, string> env = me->get_env();

    clearenv();
    for (auto &elem: env) {
        setenv(elem.first.c_str(), elem.second.c_str(), 1);
    }
}

void login_prompt(user_space::UserInfo *me) {
    ostringstream oss;
    string msg;

    // Create message
    oss << "*** User '" << me->get_name() << "' entered from " << me->get_ip_addr() << ":" << me->get_port() << ". ***" << endl;
    msg = oss.str();

    // Broadcast
    broadcast(msg);
}

void logout_prompt(user_space::UserInfo *me) {
    ostringstream oss;
    string msg;

    // Create message
    oss << "*** User '" << me->get_name() << "' left. ***" << endl;
    msg = oss.str();

    // Broadcast
    broadcast(msg);
}

void command_prompt(user_space::UserInfo *me) {
    string msg("% ");
    sendout_msg(me->get_sockfd(), msg);
}

void welcome(user_space::UserInfo *me) {
    ostringstream oss;
    string msg;

    oss << "****************************************" << endl
        << "** Welcome to the information server. **" << endl
        << "****************************************" << endl;
    msg = oss.str();

    sendout_msg(me->get_sockfd(), msg);
}

string read_msg(int sockfd) {
    static int cmd_counter = 0;
    char buf[MAX_BUF_SIZE];
    bzero(buf, MAX_BUF_SIZE);

    read(sockfd, buf, MAX_BUF_SIZE);

    string msg(buf);
    msg.erase(remove(msg.begin(), msg.end(), '\n'), msg.end());
    msg.erase(remove(msg.begin(), msg.end(), '\r'), msg.end());
    #if 1
    ++cmd_counter;
    printf("(%d) Recv (%ld): %s\n", cmd_counter, msg.length(), msg.c_str());
    #endif

    return msg;
}

void sendout_msg(int sockfd, string &msg) {
    int n = write(sockfd, msg.c_str(), msg.length());
    if (n < 0) {
        perror("Sendout Message");
        exit(0);
    }
}

void broadcast(string msg) {
    for (auto &elem: user_space::user_table.table) {
        sendout_msg(elem.second->get_sockfd(), msg);
    }
}

// Built-in Command
void my_setenv(user_space::UserInfo *me, string var, string value) {
    setenv(var.c_str(), value.c_str(), 1);
    me->set_env(var, value);
}

void my_printenv(user_space::UserInfo *me, string var) {
    char *value = getenv(var.c_str());
    
    if (value) {
        ostringstream oss;
        string msg;

        oss << value << endl;
        msg = oss.str();

        sendout_msg(me->get_sockfd(), msg);
    }
}

void my_exit(user_space::UserInfo *me) {
    user_space::user_table.put_user_to_del_queue(me->get_id());
    logout_prompt(me);
    close(me->get_sockfd());
}

void who(user_space::UserInfo *me) {
    ostringstream oss;
    string tab("\t"), is_me("<-me"), msg;

    // Create Message
    oss << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
    for (auto& elem: user_space::user_table.table) {
        oss << elem.second->get_id() << tab
            << elem.second->get_name() << tab
            << elem.second->get_ip_addr() << ":" << elem.second->get_port() << tab
            << ((elem.second->get_id() == me->get_id()) ? is_me : "")
            << endl;
    }
    msg = oss.str();

    // Send out Message
    sendout_msg(me->get_sockfd(), msg);
}

void tell(user_space::UserInfo *me, int id, string msg) {
    ostringstream oss;

    if (user_space::user_table.has_user(id)) {
        user_space::UserInfo *target_user = user_space::user_table.get_user_by_id(id);

        // Create message
        oss << "*** " << me->get_name() << " told you ***: " << msg << endl;
        msg = oss.str();

        // Send out message
        sendout_msg(target_user->get_sockfd(), msg);
    } else {
        // User is not exist
        oss << "*** Error: user #" << id << " does not exist yet. ***" << endl;
        msg = oss.str();

        sendout_msg(me->get_sockfd(), msg);
    }
}

void yell(user_space::UserInfo *me, string msg) {
    ostringstream oss;
    
    // Create message
    oss << "*** " << me->get_name() << " yelled ***: " << msg << endl;
    msg = oss.str();

    // Broadcast message
    broadcast(msg);
}

void name_cmd(user_space::UserInfo *me, string name) {
    ostringstream oss;
    string msg;

    // Check name
    for (auto &elem: user_space::user_table.table) {
        if (name.compare(elem.second->get_name()) == 0) {
            // The name is already exist
            oss << "*** User '" << name << "' already exists. ***" << endl;
            msg = oss.str();
            sendout_msg(me->get_sockfd(), msg);
            return;
        }
    }

    // Change name and broadcast message
    me->set_name(name);
    oss << "*** User from " << me->get_ip_addr() << ":" << me->get_port() << " is named '" << name << "'. ***" << endl;
    msg = oss.str();

    broadcast(msg);
}

int handle_builtin(user_space::UserInfo *me, string cmd) {
    istringstream iss(cmd);
    string prog;

    getline(iss, prog, ' ');

    if (prog == "setenv") {
        string var, value;

        getline(iss, var, ' ');
        getline(iss, value, ' ');
        my_setenv(me, var, value);

        return BUILT_IN_TRUE;
    } else if (prog == "printenv") {
        string var;

        getline(iss, var, ' ');
        my_printenv(me, var);

        return BUILT_IN_TRUE;
    } else if (prog == "exit") {
        my_exit(me);

        return BUILT_IN_EXIT;
    } else if (prog == "who") {
        who(me);

        return BUILT_IN_TRUE;
    } else if (prog == "tell") {
        string id, msg;

        getline(iss, id, ' ');
        getline(iss, msg);

        tell(me, atoi(id.c_str()), msg);

        return BUILT_IN_TRUE;
    } else if (prog == "yell") {
        string msg;

        getline(iss, msg);
        yell(me, msg);

        return BUILT_IN_TRUE;
    } else if (prog == "name") {
        string new_name;
        
        getline(iss, new_name, ' ');
        name_cmd(me, new_name);

        return BUILT_IN_TRUE;
    }

    return BUILT_IN_FALSE;
}
// Built-in Command End

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

    // Socket setting
    int optval = 1;
    status_code = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    if (status_code < 0) {
        perror("Set socket option");
        exit(0);
    }

    // Bind socket
    status_code = bind(listen_sock, (struct sockaddr *) &s_addr, sizeof(s_addr));
    if (status_code < 0) {
        perror("Server bind");
        exit(0);
    }

    // Listen socket
    status_code = listen(listen_sock, 5);
    if (status_code < 0) {
        perror("Server listen");
        exit(0);
    }

    return listen_sock;
}




#endif