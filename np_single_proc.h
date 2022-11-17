#ifndef NP_SINGLE_PROC
#define NP_SINGLE_PROC
#include <regex>
#include <fcntl.h>
#include <stdio.h>
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
#define DEFAULT_FD  -1

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
// Handler
void child_handler(int sig);
void interrupt_handler(int sig);

// Sockeet
int get_listen_socket(const char *port);

// User
void load_user_config(user_space::UserInfo *me);

// Network IO
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

bool is_white_char(string cmd);
void clean_user_pipe();
int search_user_pipe(int src_uid, int dst_uid, int *up_idx);
int create_user_pipe(user_space::UserInfo *me, int dst_uid);
void check_user_pipe(string cmd, bool *in, bool *out);
bool handle_input_user_pipe(user_space::UserInfo *me, string cmd, int *up_idx);
bool handle_output_user_pipe(user_space::UserInfo *me, string cmd, int *up_idx);
void decrement_number_pipes(vector<NumberPipe> &number_pipes);

vector<string> parse_normal_pipe(string input);
vector<Command> parse_number_pipe(string input);
void parse_user_pipe(user_space::UserInfo *me, vector<Command> &commands);

void execute_command(user_space::UserInfo *me, vector<string> args);
int main_executor(user_space::UserInfo *me, Command &command);
int handle_command(user_space::UserInfo *me, string input);

int run_shell(user_space::UserInfo *me, string msg);
int handle_client(int sockfd);

/* Global Variables */
vector<UserPipe> user_pipes;
regex up_in_pattern("[<][1-9]\\d?\\d?\\d?[0]?");
regex up_out_pattern("[>][1-9]\\d?\\d?\\d?[0]?");
string original_command;

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

    if (read(sockfd, buf, MAX_BUF_SIZE) < 0) {
        perror("Read Message.");
        return "";
    }

    string msg(buf);
    msg.erase(remove(msg.begin(), msg.end(), '\n'), msg.end());
    msg.erase(remove(msg.begin(), msg.end(), '\r'), msg.end());
    #if 0
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

bool fd_is_valid(int fd) {
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

void debug_number_pipes(vector<NumberPipe> number_pipes) {
    if (number_pipes.size() > 0) {    
        cerr << "Number Pipes" << endl;
        for (size_t i = 0; i < number_pipes.size(); i++) {
            cerr << "\tIndex: "  << i 
                 << "\tNumber: " << number_pipes[i].number
                 << "\tIn: "     << number_pipes[i].in << "\tValid: " << (fd_is_valid(number_pipes[i].in) ? "True" : "False")
                 << "\tOut: "    << number_pipes[i].out << "\tValid: " << (fd_is_valid(number_pipes[i].out) ? "True" : "False") << endl;
        }
    }
}

void debug_pipes(vector<Pipe> pipes) {
    if (pipes.size() > 0) {
        cerr << "Pipes" << endl;
        for (size_t i = 0; i < pipes.size(); i++) {
            cerr << "\tIndex: " << i 
                 << "\tIn: "    << pipes[i].in 
                 << "\tOut: "   << pipes[i].out << endl;
        }
    }
}

void debug_user_pipes() {
    if (user_pipes.size() > 0) {
        cerr << "User Pipe:" << endl;
        for (int x=0; x < user_pipes.size(); ++x) {
            cerr << "\tSrc: "     << user_pipes[x].src_uid
                 << "\tDst: "     << user_pipes[x].dst_uid
                 << "\tIn: "      << user_pipes[x].pipe.in
                 << "\tOut: "     << user_pipes[x].pipe.out
                 << "\tIs Done: " << (user_pipes[x].is_done ? "True" : "False") << endl;
        }
    }
}

bool is_white_char(string cmd) {
    for (size_t i = 0; i < cmd.length(); i++) {
        if(isspace(cmd[i]) == 0) {
            return false;
        }
    }
    return true;
}

void clean_user_pipe() {
    vector<int> index;

    for (size_t i = 0; i < user_pipes.size(); i++) {
        if( user_pipes[i].is_done ) index.push_back(i);
    }
    for (int i = index.size()-1; i >= 0; --i) {
        user_pipes.erase(user_pipes.begin() + index[i]);
    }
}

int search_user_pipe(int src_uid, int dst_uid, int *up_idx){
    int result = -1;

    for (int x=0; x < user_pipes.size(); ++x) {
        if (user_pipes[x].src_uid == src_uid && user_pipes[x].dst_uid == dst_uid) {
            result = x;
            break;
        }
    }
    
    *up_idx = result;
    return result;
}

int create_user_pipe(user_space::UserInfo *me, int dst_uid) {
    int pipefd[2];
    pipe(pipefd);

    UserPipe up;
    up.src_uid  = me->get_id();
    up.dst_uid  = dst_uid;
    up.pipe.in  = pipefd[0];
    up.pipe.out = pipefd[1];
    up.is_done  = false;

    user_pipes.push_back(up);

    return user_pipes.size()-1;
}

void handle_user_pipe(user_space::UserInfo *me, string cmd, bool *in, bool *out, bool *in_err, bool *out_err, int *in_idx, int *out_idx) {
    smatch result;

    *in  = regex_search(cmd, result, up_in_pattern);
    *out = regex_search(cmd, result, up_out_pattern);

    if (*in) {
        *in_err = handle_input_user_pipe(me, cmd, in_idx);
    }

    if (*out) {
        *out_err = handle_output_user_pipe(me, cmd, out_idx);
    }
}

void check_user_pipe(string cmd, bool *in, bool *out) {
    smatch result;

    *in  = regex_search(cmd, result, up_in_pattern);
    *out = regex_search(cmd, result, up_out_pattern);
}

bool handle_input_user_pipe(user_space::UserInfo *me, string cmd, int *up_idx) {
    int src_uid;
    bool error = false;
    ostringstream oss;
    string msg;
    user_space::UserInfo *src_user;
    smatch result;

    regex_search(cmd, result, up_in_pattern);
    src_uid = atoi(cmd.substr(result.position()+1, result.length()-1).c_str());

    if (!error) {
        // Check user is exist
        if (!user_space::user_table.has_user(src_uid)) {
            oss << "*** Error: user #" << src_uid << " does not exist yet. ***" << endl;
            msg = oss.str();
            sendout_msg(me->get_sockfd(), msg);
            
            error = true;
        }
    }

    if (!error) {
        // Search user pipe
        int result = search_user_pipe(src_uid, me->get_id(), up_idx);

        if (result == -1) {
            // Not found
            oss.clear();
            oss << "*** Error: the pipe #" << src_uid << "->#" << me->get_id() << " does not exist yet. ***" << endl;
            msg = oss.str();

            sendout_msg(me->get_sockfd(), msg);

            error = true;
        }
    }

    if (!error) {
        // Broadcast Message
        src_user = user_space::user_table.get_user_by_id(src_uid);

        oss << "*** " << me->get_name() << " (#" << me->get_id() << ") just received from "
            << src_user->get_name() << " (#" << src_uid << ") by '" << original_command << "' ***" << endl;
        msg = oss.str();
        broadcast(msg);
    }

    return error;
}

bool handle_output_user_pipe(user_space::UserInfo *me, string cmd, int *up_idx) {
    int dst_uid;
    bool error = false;
    ostringstream oss;
    string msg;
    user_space::UserInfo *dst_user;
    smatch result;

    regex_search(cmd, result, up_out_pattern);
    dst_uid = atoi(cmd.substr(result.position()+1, result.length()-1).c_str());

    if (!error) {
        // Check user is exist
        if (!user_space::user_table.has_user(dst_uid)) {
            oss << "*** Error: user #" << dst_uid << " does not exist yet. ***" << endl;
            msg = oss.str();
            sendout_msg(me->get_sockfd(), msg);

            error = true;
        }
    }

    if (!error) {
        // Search pipe
        int tmp;
        int result = search_user_pipe(me->get_id(), dst_uid, &tmp);

        if (result != -1) {
            // User pipe is exist
            oss.clear();
            oss << "*** Error: the pipe #" << me->get_id() << "->#" << dst_uid << " already exists. ***" << endl;
            msg = oss.str();
            sendout_msg(me->get_sockfd(), msg);

            error = true;
        }
    }

    if (!error) {
        // Broadcast Message
        dst_user = user_space::user_table.get_user_by_id(dst_uid);

        oss << "*** " << me->get_name() << " (#" << me->get_id() << ") just piped '" << original_command << "' to "
            << dst_user->get_name() << " (#" << dst_user->get_id() << ") ***" << endl;
        msg = oss.str();

        broadcast(msg);

        // Create user pipe
        *up_idx = create_user_pipe(me, dst_uid);
    }

    return error;
}

void decrement_number_pipes(vector<NumberPipe> &number_pipes) {
    for (size_t i = 0; i < number_pipes.size(); i++) {
        --number_pipes[i].number;
    }
}

vector<string> parse_normal_pipe(string input) {
    vector<string> cmds;
    string pipe_symbol("| ");
    int pos;

    // Parse pipe
    while ((pos = input.find(pipe_symbol)) != string::npos) {
        cmds.push_back(input.substr(0, pos));
        input.erase(0, pos + pipe_symbol.length());
    }
    cmds.push_back(input);

    // Remove space
    for (size_t i = 0; i < cmds.size(); i++) {
        int head = 0, tail = cmds[i].length() - 1;

        while(cmds[i][head] == ' ') ++head;
        while(cmds[i][tail] == ' ') --tail;

        if (head != 0 || tail != cmds[i].length() - 1) {
            cmds[i] = cmds[i].substr(head, tail + 1);
        }
    }

    return cmds;
}

vector<Command> parse_number_pipe(string input) {
    /*
     *    "removetag test.html |2 ls | number |1"
     * -> ["removetag test.html |2", "ls | number |1"]
     */
    vector<Command> lines;
    regex pattern("[|!][1-9]\\d?\\d?[0]?");
    smatch result;

    while(regex_search(input, result, pattern)) {
        Command command;
        string tmp;
        tmp = input.substr(0, result.position() + result.length());

        command.cmd = input.substr(0, result.position() + result.length());
        command.number = atoi(input.substr(result.position() + 1, result.length() - 1).c_str());
        input.erase(0, result.position() + result.length());

        lines.push_back(command);
    }

    if (input.length() != 0) {
        Command command{cmd: input};
        
        lines.push_back(command);
    }

    if (lines.size() == 0) {
        // Normal Pipe
        Command command{cmd: input};

        lines.push_back(command);
    }

    // Split by pipe
    for (size_t i = 0; i < lines.size(); i++) {
        lines[i].cmds = parse_normal_pipe(lines[i].cmd); 
    }

    // Debug
    #if 0
    for (size_t i = 0; i < lines.size(); i++) {
        cerr << "Line " << i << ": " << lines[i].cmd << endl;
        cerr << "\tCommand: " << lines[i].cmd << endl;
        cerr << "\tCommand Size: " << lines[i].cmds.size() << endl;
        cerr << "\tNumber: " << lines[i].number << endl;
    }
    #endif
        
    return lines;
}

void execute_command(user_space::UserInfo *me, vector<string> args) {
    int fd;
    bool need_file_redirection = false;
    const char *prog = args[0].c_str();
    const char **c_args = new const char* [args.size()+1];  // Reserve one location for NULL

    #if 0
    cerr << "Execute Command Args: ";
    for (size_t i = 0; i < args.size(); i++) {
        cerr << args[i] << " ";
    }
    cerr << endl;
    #endif

    /* Parse Arguments */
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == ">") {
            need_file_redirection = true;
            fd = open(args[i+1].c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            args.pop_back();  // Remove file name
            args.pop_back();  // Remove redirect symbol
            break;
        }
        c_args[i] = args[i].c_str();
    }
    c_args[args.size()] = NULL;

    // Check if need file redirection
    if (need_file_redirection) {
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    // Execute Command
    if (execvp(prog, (char **)c_args) == -1) {
        ostringstream oss;
        string msg;

        oss << "Unknown command: [" << args[0] << "]." << endl;
        msg = oss.str();
        sendout_msg(me->get_sockfd(), msg);
        exit(1);
    }
}

int main_executor(user_space::UserInfo *me, Command &command) {
    /* Pre-Process */
    decrement_number_pipes(me->number_pipes);
    if (command.cmds.size() == 1) {
        int code = handle_builtin(me, command.cmds[0]);
        if (code != BUILT_IN_FALSE) {
            return code;
        }
    }
    #if 0
    cerr << "Handle " << command.cmd << endl;
    #endif

    vector<string> args;
    string error_pipe_symbol = "!", pipe_symbol = "|";
    string arg;
    bool is_error_pipe = false, is_number_pipe = false;
    bool is_input_user_pipe = false, is_output_user_pipe = false;
    bool is_input_user_pipe_error = false, is_output_user_pipe_error = false;
    
    // Handle a command per loop
    for (size_t i = 0; i < command.cmds.size(); i++) {
        istringstream iss(command.cmds[i]);
        vector<string> args;
        pid_t pid;
        int pipefd[2];
        int input_user_pipe_idx  = -1;
        int output_user_pipe_idx = -1;
        bool is_first_cmd = false, is_final_cmd = false;
        smatch in_result, out_result;

        if (i == 0)                        is_first_cmd = true;
        if (i == command.cmds.size() - 1)  is_final_cmd = true;

        // Check if there are user pipes
        // check_user_pipe(command.cmds[i], &is_input_user_pipe, &is_output_user_pipe);
        handle_user_pipe(me, command.cmds[i],
            &is_input_user_pipe, &is_output_user_pipe,
            &is_input_user_pipe_error, &is_output_user_pipe_error,
            &input_user_pipe_idx, &output_user_pipe_idx);

        /* Parse Command to Args */
        #if 0
        cerr << "*** Parse Command: " << command.cmds[i] << endl;
        #endif

        while (getline(iss, arg, ' ')) {
            bool ignore_arg = false;

            if (is_white_char(arg)) continue;

            if (regex_search(arg, in_result, up_in_pattern)) {
                ignore_arg = true;
                // is_input_user_pipe_error = handle_input_user_pipe(me, arg, &input_user_pipe_idx);
            }

            if (regex_search(arg, out_result, up_out_pattern)) {
                ignore_arg = true;
                // is_output_user_pipe_error = handle_output_user_pipe(me, arg, &output_user_pipe_idx);
                #if 0
                debug_user_pipes();
                #endif
            }

            // Handle number and error pipe
            if (is_final_cmd) {
                if ((is_number_pipe = (arg.find("|") != string::npos)) ||
                    (is_error_pipe  = (arg.find("!") != string::npos))) {
                    bool is_add = false;
                    ignore_arg = true;

                    for (int x=0; x < me->number_pipes.size(); ++x) {
                        // Same number means that using the same pipe
                        if (me->number_pipes[x].number == command.number) {
                            me->number_pipes.push_back(NumberPipe{
                                in:     me->number_pipes[x].in,
                                out:    me->number_pipes[x].out,
                                number: command.number
                            });
                            is_add = true;
                            break;
                        }
                    }
                    // No match, Create a new pipe
                    if (!is_add) {
                        pipe(pipefd);
                        me->number_pipes.push_back(NumberPipe{
                            in: pipefd[0],
                            out: pipefd[1],
                            number: command.number});
                    }
                    #if 0
                    debug_number_pipes(me->number_pipes);
                    #endif
                }
            }

            // Ignore |N or !N
            if (!ignore_arg) {
                args.push_back(arg);
            }
        }
        /* Parse Command to Args End */

        /* Create Normal Pipe */
        if (!is_error_pipe && !is_number_pipe && !is_output_user_pipe) {
            if(!is_final_cmd && command.cmds.size() > 1) {
                pipe(pipefd);
                me->pipes.push_back(Pipe{in: pipefd[0], out: pipefd[1]});
                #if 0
                debug_pipes(me->pipes);
                #endif
            }
        }

        #if 0
        cerr << "User Pipe Flag:" << endl;
        cerr << "\tin: "      << (is_input_user_pipe ? "True" : "False") << endl
             << "\tout: "     << (is_output_user_pipe ? "True" : "False") << endl
             << "\tin err: "  << (is_input_user_pipe_error ? "True" : "False") << endl
             << "\tout err: " << (is_output_user_pipe_error ? "True" : "False") << endl;
        cerr << "User Pipe index" << endl;
        cerr << "\tin: " << input_user_pipe_idx << endl
             << "\tout:" << output_user_pipe_idx << endl;
        #endif

        // cerr << "Start Fork" << endl;
        do {
            pid = fork();
            usleep(5000);
        } while (pid < 0);

        if (pid > 0) {
            /* Parent Process */
            #if 0
                cerr << "Parent PID: " << getpid() << endl;
                cerr << "\tNumber of Pipes: " << pipes.size() << endl;
                cerr << "\tNumber of N Pipes: " << number_pipes.size() << endl;
            #endif
            /* Close Pipe */
            // Normal Pipe
            if (i != 0) {
                // cerr << "Parent Close pipe: " << i-1 << endl;
                close(me->pipes[i-1].in);
                close(me->pipes[i-1].out);
            }

            // Number Pipe
            for (int x=0; x < me->number_pipes.size(); ++x) {
                if (me->number_pipes[x].number == 0) {
                    #if 0
                    cerr << "Parent Close number pipe: " << x << endl;
                    #endif
                    close(me->number_pipes[x].in);
                    close(me->number_pipes[x].out);

                    // Remove number pipe
                    me->number_pipes.erase(me->number_pipes.begin() + x);
                    --x;
                }
            }

            // User Pipe
            if (input_user_pipe_idx != -1) {
                close(user_pipes[input_user_pipe_idx].pipe.in);
                close(user_pipes[input_user_pipe_idx].pipe.out);
                user_pipes[input_user_pipe_idx].is_done = true;
                clean_user_pipe();
            }

            if (is_final_cmd && !(is_number_pipe || is_error_pipe) && !is_output_user_pipe) {
                // Final process, wait
                #if 0
                cerr << "Parent Wait Start" << endl;
                #endif
                int st;
                waitpid(pid, &st, 0);
                #if 0
                cerr << "Parent Wait End: " << st << endl;
                #endif
            }
        } else {
            /* Child Process */
            #if 0
            usleep(2000);
            cerr << "Child PID: " << getpid() << endl;
            cerr << "\tFirst? " << (is_first_cmd ? "True" : "False") << endl;
            cerr << "\tFinal? " << (is_final_cmd ? "True" : "False") << endl;
            cerr << "\tNumber? " << (is_number_pipe ? "True" : "False") << endl;
            cerr << "\tError? " << (is_error_pipe ? "True" : "False") << endl;
            #endif
            #if 0
            cerr << "Child Execute: " << args[0] << endl;
            usleep(5000);
            #endif

            /* Duplicate pipe */
            // STDERR -> socket
            dup2(me->get_sockfd(), STDERR_FILENO);

            if (is_first_cmd) {
                // Receive input from number pipe
                for (size_t x = 0; x < me->number_pipes.size(); x++) {
                    if (me->number_pipes[x].number == 0) {
                        dup2(me->number_pipes[x].in, STDIN_FILENO);
                        #if 0
                        cerr << "First Number Pipe (in) " << me->number_pipes[x].in << " to stdin" << endl;
                        #endif
                        break;
                    }
                }

                // Setup output of normal pipe
                if (me->pipes.size() > 0) {
                    dup2(me->pipes[i].out, STDOUT_FILENO);
                    #if 0
                    cerr << "First Normal Pipe (out) " << me->pipes[i].out << " to stdout" << endl;
                    #endif
                }

                // Recv from user pipe
                if (is_input_user_pipe) {
                    if (is_input_user_pipe_error) {
                        int dev_null = open("/dev/null", O_RDWR);
                        dup2(dev_null, STDIN_FILENO);
                        close(dev_null);
                        #if 0
                        cerr << "Set up user pipe input to null" << endl;
                        #endif
                    } else {
                        dup2(user_pipes[input_user_pipe_idx].pipe.in, STDIN_FILENO);
                        #if 0
                        cerr << "Set up user pipe input to " << user_pipes[input_user_pipe_idx].pipe.in << endl;
                        #endif
                    }

                }
            }

            // Setup input and output of normal pipe
            if (!is_first_cmd && !is_final_cmd) {
                if (me->pipes.size() > 0) {
                    dup2(me->pipes[i-1].in, STDIN_FILENO);
                    dup2(me->pipes[i].out, STDOUT_FILENO);
                }
                #if 0
                cerr << "Internal (in) " << me->pipes[i-1].in << " to stdin"  << endl;
                cerr << "Internal (out) " << me->pipes[i].out << " to stdout" << endl;
                #endif
                // TODO: user pipe in the middle ??
            }

            if (is_final_cmd) {
                if (is_number_pipe) {
                    /* Number Pipe */
                    #if 0
                    cerr << "Final Number Pipe" << endl;
                    #endif
                    
                    // Setup Input
                    if (me->pipes.size() > 0) {
                        // Receive from previous command via normal pipe
                        dup2(me->pipes[i-1].in, STDIN_FILENO);
                        #if 0
                        cerr << "Final number Pipe (in) (from normal pip) " << me->pipes[i-1].in << " to stdin" << endl;
                        #endif
                    }
                    
                    // Setup Output
                    for (size_t x = 0; x < me->number_pipes.size(); x++) {
                        if (me->number_pipes[x].number == command.number) {
                            dup2(me->number_pipes[x].out, STDOUT_FILENO);
                            close(me->number_pipes[x].out);
                            #if 0
                            cerr << "Final Number Pipe (out) " << me->number_pipes[x].out << " to stdout" << endl;
                            #endif
                            break;
                        }
                    }
                } else if (is_error_pipe) {
                    #if 0
                    cerr << "Final Error Pipe" << endl;
                    #endif
                    /* Error Pipe */
                    for (size_t x = 0; x < me->number_pipes.size(); x++) {
                        if (me->number_pipes[x].number == command.number) {
                            dup2(me->number_pipes[x].out, STDOUT_FILENO);
                            dup2(me->number_pipes[x].out, STDERR_FILENO);
                            break;
                        }
                    }
                } else if (is_output_user_pipe) {
                    #if 0
                    cerr << "Final User Pipe" << endl;
                    #endif

                    if (me->pipes.size() > 0) {
                        // Set input
                        dup2(me->pipes[i-1].in, STDIN_FILENO);
                    }

                    // Set output
                    if (is_output_user_pipe_error) {
                        int dev_null = open("/dev/null", O_RDWR);
                        dup2(dev_null, STDOUT_FILENO);
                        close(dev_null);
                    } else {
                        dup2(user_pipes[output_user_pipe_idx].pipe.out, STDOUT_FILENO);
                    }

                } else {
                    #if 0
                    cerr << "Final Normal Pipe" << endl;
                    #endif
                    /* Normal Pipe*/
                    if (me->pipes.size() > 0) {
                        dup2(me->pipes[i-1].in, STDIN_FILENO);
                        #if 0
                        cerr << "Set input from " << me->pipes[i-1].in << " to stdin" << endl;
                        #endif
                    }

                    // Redirect to socket
                    dup2(me->get_sockfd(), STDOUT_FILENO);
                    #if 0
                    cerr << "Set output to socket " << me->get_sockfd() << endl;
                    #endif
                }
            }

            /* Close pipe */
            for (int ci = 0; ci < me->pipes.size(); ci++) {
                close(me->pipes[ci].in);
                close(me->pipes[ci].out);
            }
            for (int ci = 0; ci < me->number_pipes.size(); ci++) {
                close(me->number_pipes[ci].in);
                close(me->number_pipes[ci].out);
            }
            for (int x=0; x < user_pipes.size(); ++x) {
                close(user_pipes[x].pipe.in);
                close(user_pipes[x].pipe.out);
            }

            execute_command(me, args);
        }
    }
    me->pipes.clear();
    return 0;
}

int handle_command(user_space::UserInfo *me, string input) {
    vector<Command> lines;
    int code;

    lines = parse_number_pipe(input);

    for (size_t i = 0; i < lines.size(); i++) {
        code = main_executor(me, lines[i]);
    }

    return code;
}

int run_shell(user_space::UserInfo *me, string input) {
    if (input.size() == 0) {
        return 0;
    }
    // Load user config
    load_user_config(me);

    // Hanld input command
    return handle_command(me, input);
}

int handle_client(int sockfd) {
    // Get user
    user_space::UserInfo *client = user_space::user_table.get_user_by_sockfd(sockfd);

    // Get input message
    string input = read_msg(sockfd);
    original_command = input;

    // Run shell
    int code = run_shell(client, input);
    if (code != BUILT_IN_EXIT) {
        command_prompt(client);
    }

    return code;
}

#endif