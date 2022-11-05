#ifndef NP_SINGLE_PROC
#define NP_SINGLE_PROC
#include <vector>
#include <regex>
#include <fcntl.h>
#include <stdio.h>

#include "common.h"

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

bool is_white_char(string cmd);
void clean_user_pipe();
int search_user_pipe(int src_uid, int dst_uid, int *up_idx);
int create_user_pipe(user_space::UserInfo *me, int dst_uid);
void check_user_pipe(string cmd, bool *in, bool *out);
bool handle_input_user_pipe(user_space::UserInfo *me, string cmd, int *up_idx);
bool handle_output_user_pipe(user_space::UserInfo *me, string cmd, int *up_idx);
void decrement_and_cleanup_number_pipes();

vector<string> parse_normal_pipe(string input);
vector<Command> parse_number_pipe(string input);
void parse_user_pipe(user_space::UserInfo *me, vector<Command> &commands);

void execute_command(user_space::UserInfo *me, vector<string> args);
int main_executor(user_space::UserInfo *me, Command &command);
int handle_command(user_space::UserInfo *me, string input);

int run_shell(user_space::UserInfo *me, string msg);
int handle_client(int sockfd);

/* Global Variables */
vector<Pipe> pipes;
vector<NumberPipe> number_pipes;
vector<UserPipe> user_pipes;
regex up_in_pattern("[<][1-9]\\d?\\d?\\d?[0]?");
regex up_out_pattern("[>][1-9]\\d?\\d?\\d?[0]?");
string original_command;

void debug_number_pipes() {
    if (number_pipes.size() > 0) {    
        cerr << "Number Pipes" << endl;
        for (size_t i = 0; i < number_pipes.size(); i++) {
            cerr << "\tIndex: "  << i 
                 << "\tNumber: " << number_pipes[i].number
                 << "\tIn: "     << number_pipes[i].in
                 << "\tOut: "    << number_pipes[i].out << endl;
        }
    }
}

void debug_pipes() {
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
        cerr << "Clean User Pipe: " << i << endl;
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

void decrement_and_cleanup_number_pipes() {
    vector<int> index;

    for (size_t i = 0; i < number_pipes.size(); i++) {
        if( --number_pipes[i].number < -1 ) index.push_back(i);
    }
    for (int i = index.size()-1; i >= 0; --i) {
        number_pipes.erase(number_pipes.begin() + index[i]);
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
        cout << "Line " << i << ": " << lines[i].cmd << endl;
        cout << "\tCommand: " << lines[i].cmd << endl;
        cout << "\tCommand Size: " << lines[i].cmds.size() << endl;
        cout << "\tNumber: " << lines[i].number << endl;
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
    decrement_and_cleanup_number_pipes();
    if (command.cmds.size() == 1) {
        int code = handle_builtin(me, command.cmds[0]);
        if (code != BUILT_IN_FALSE) {
            return code;
        }
    }
    #if 0
    cout << "Handle " << command.cmd << endl;
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
        check_user_pipe(command.cmds[i], &is_input_user_pipe, &is_output_user_pipe);

        /* Parse Command to Args */
        while (getline(iss, arg, ' ')) {
            bool ignore_arg = false;

            if (is_white_char(arg)) continue;

            // TODO: show message ordering
            if (regex_search(arg, in_result, up_in_pattern)) {
                ignore_arg = true;
                is_input_user_pipe_error = handle_input_user_pipe(me, arg, &input_user_pipe_idx);
            }

            if (regex_search(arg, out_result, up_out_pattern)) {
                ignore_arg = true;
                is_output_user_pipe_error = handle_output_user_pipe(me, arg, &output_user_pipe_idx);
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

                    for (int x=0; x < number_pipes.size(); ++x) {
                        // Same number means that using the same pipe
                        if (number_pipes[x].number == command.number) {
                            number_pipes.push_back(NumberPipe{
                                in:     number_pipes[x].in,
                                out:    number_pipes[x].out,
                                number: command.number
                            });
                            is_add = true;
                            break;
                        }
                    }
                    // No match, Create a new pipe
                    if (!is_add) {
                        pipe(pipefd);
                        number_pipes.push_back(NumberPipe{
                            in: pipefd[0],
                            out: pipefd[1],
                            number: command.number});
                    }
                    #if 0
                    debug_number_pipes();
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
                pipes.push_back(Pipe{in: pipefd[0], out: pipefd[1]});
                #if 0
                debug_pipes();
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

        // cout << "Start Fork" << endl;
        do {
            pid = fork();
            usleep(5000);
        } while (pid < 0);

        if (pid > 0) {
            /* Parent Process */
            #if 0
                cout << "Parent PID: " << getpid() << endl;
                cout << "\tNumber of Pipes: " << pipes.size() << endl;
                cout << "\tNumber of N Pipes: " << number_pipes.size() << endl;
            #endif
            /* Close Pipe */
            // Normal Pipe
            if (i != 0) {
                // cout << "Parent Close pipe: " << i-1 << endl;
                close(pipes[i-1].in);
                close(pipes[i-1].out);
            }

            // Number Pipe
            for (int x=0; x < number_pipes.size(); ++x) {
                if (number_pipes[x].number == 0) {
                    #if 0
                    cout << "Parent Close number pipe: " << x << endl;
                    #endif
                    close(number_pipes[x].in);
                    close(number_pipes[x].out);
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
                cout << "Parent Wait Start" << endl;
                #endif
                int st;
                waitpid(pid, &st, 0);
                #if 0
                cout << "Parent Wait End: " << st << endl;
                #endif
            }
        } else {
            /* Child Process */
            #if 0
            usleep(2000);
            cout << "Child PID: " << getpid() << endl;
            cout << "\tFirst? " << (is_first_cmd ? "True" : "False") << endl;
            cout << "\tFinal? " << (is_final_cmd ? "True" : "False") << endl;
            cout << "\tNumber? " << (is_number_pipe ? "True" : "False") << endl;
            cout << "\tError? " << (is_error_pipe ? "True" : "False") << endl;
            #endif
            #if 0
            cerr << "Child Execute: " << args[0] << endl;
            usleep(5000);
            #endif

            /* Duplicate pipe */ 
            if (is_first_cmd) {
                // Receive input from number pipe
                for (size_t x = 0; x < number_pipes.size(); x++) {
                    if (number_pipes[x].number == 0) {
                        dup2(number_pipes[x].in, STDIN_FILENO);
                        #if 0
                        cerr << "First Number Pipe (in) " << number_pipes[x].in << " to " << STDIN_FILENO << endl;
                        #endif
                        break;
                    }
                }

                // Setup output of normal pipe
                if (pipes.size() > 0) {
                    dup2(pipes[i].out, STDOUT_FILENO);
                    #if 0
                    cout << "First Normal Pipe (out) " << pipes[i].out << " to " << STDOUT_FILENO << endl;
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
                if (pipes.size() > 0) {
                    dup2(pipes[i-1].in, STDIN_FILENO);
                    dup2(pipes[i].out, STDOUT_FILENO);
                }
                #if 0
                cout << "Internal (in) " << pipes[i-1].in << " to " << STDIN_FILENO << endl;
                cout << "Internal (out) " << pipes[i].out << " to " << STDOUT_FILENO << endl;
                #endif
                // TODO: user pipe in the middle ??
            }

            if (is_final_cmd) {
                if (is_number_pipe) {
                    /* Number Pipe */
                    cerr << "Final Number Pipe" << endl;
                    
                    // Setup Input
                    if (pipes.size() > 0) {
                        // Receive from previous command via normal pipe
                        dup2(pipes[i-1].in, STDIN_FILENO);
                        #if 0
                        cerr << "Final number Pipe (in) (from normal pip) " << pipes[i-1].in << " to " <<STDIN_FILENO << endl;
                        #endif
                    }
                    
                    // Setup Output
                    for (size_t x = 0; x < number_pipes.size(); x++) {
                        if (number_pipes[x].number == command.number) {
                            dup2(number_pipes[x].out, STDOUT_FILENO);
                            close(number_pipes[x].out);
                            #if 0
                            cerr << "Final Number Pipe (out) " << number_pipes[x].out << " to " << STDOUT_FILENO << endl;
                            #endif
                            break;
                        }
                    }
                } else if (is_error_pipe) {
                    cerr << "Final Error Pipe" << endl;
                    /* Error Pipe */
                    for (size_t x = 0; x < number_pipes.size(); x++) {
                        if (number_pipes[x].number == command.number) {
                            dup2(number_pipes[x].out, STDOUT_FILENO);
                            dup2(number_pipes[x].out, STDERR_FILENO);
                            break;
                        }
                    }
                } else if (is_output_user_pipe) {
                    cerr << "Final User Pipe" << endl;

                    if (pipes.size() > 0) {
                        // Set input
                        dup2(pipes[i-1].in, STDIN_FILENO);
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
                    cerr << "Final Normal Pipe" << endl;
                    /* Normal Pipe*/
                    if (pipes.size() > 0) {
                        dup2(pipes[i-1].in, STDIN_FILENO);
                        #if 0
                        cerr << "Set input from " << pipes[i-1].in << " to stdin" << endl;
                        #endif
                    }
                    
                    // Redirect to socket
                    dup2(me->get_sockfd(), STDOUT_FILENO);
                    #if 0
                    cerr << "Set output to " << me->get_sockfd() << endl;
                    #endif
                }
            }

            /* Close pipe */
            for (int ci = 0; ci < pipes.size(); ci++) {
                close(pipes[ci].in);
                close(pipes[ci].out);
            }
            for (int ci = 0; ci < number_pipes.size(); ci++) {
                close(number_pipes[ci].in);
                close(number_pipes[ci].out);
            }
            for (int x=0; x < user_pipes.size(); ++x) {
                close(user_pipes[x].pipe.in);
                close(user_pipes[x].pipe.out);
            }

            execute_command(me, args);
        }
    }
    pipes.clear();
    return 0;
}

int handle_command(user_space::UserInfo *me, string input) {
    vector<Command> lines;
    int code;

    lines = parse_number_pipe(input);

    for (size_t i = 0; i < lines.size(); i++) {
        // cout << "CMD " << i << ": " << lines[i].cmd << "X" << endl;
        code = main_executor(me, lines[i]);
    }

    return code;
}

int run_shell(user_space::UserInfo *me, string input) {
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