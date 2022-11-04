#ifndef NP_SINGLE_PROC
#define NP_SINGLE_PROC
#include <vector>
#include <regex>
#include <fcntl.h>
#include <stdio.h>

#include "common.h"

#define DEFAULT_FD  -1

struct mypipe {
    int in;
    int out;
};

struct my_number_pipe {
    int in;
    int out;
    int number;
};

struct my_command {
    string ori_cmd;       // Reserve original command
    string cmd;           // Cut by number|error pipe
    vector<string> cmds;  // Split by pipe
    int number;           // For number pipe
    int in_fd, out_fd;    // For user pipe
};

typedef struct mypipe Pipe;
typedef struct my_number_pipe NumberPipe;
typedef struct my_command Command;

bool is_white_char(string cmd);
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

bool is_white_char(string cmd) {
    for (size_t i = 0; i < cmd.length(); i++) {
        if(isspace(cmd[i]) == 0) {
            return false;
        }
    }
    return true;
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

        command.ori_cmd = input;
        command.cmd = input.substr(0, result.position() + result.length());
        command.number = atoi(input.substr(result.position() + 1, result.length() - 1).c_str());
        input.erase(0, result.position() + result.length());

        lines.push_back(command);
    }

    if (input.length() != 0) {
        Command command{ori_cmd: input, cmd: input};
        
        lines.push_back(command);
    }

    if (lines.size() == 0) {
        // Normal Pipe
        Command command{ori_cmd: input, cmd: input};

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

    // Handle a command per loop
    for (size_t i = 0; i < command.cmds.size(); i++) {
        istringstream iss(command.cmds[i]);
        vector<string> args;
        pid_t pid;
        int pipefd[2];
        bool is_first_cmd = false, is_final_cmd = false;

        if (i == 0)                        is_first_cmd = true;
        if (i == command.cmds.size() - 1)  is_final_cmd = true;

        /* Parse Command to Args */
        while (getline(iss, arg, ' ')) {
            bool ignore_arg = false;

            if (is_white_char(arg)) continue;

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

        /* Create Normal Pipe */
        if (!is_error_pipe && !is_number_pipe) {
            if(!is_final_cmd && command.cmds.size() > 1) {
                pipe(pipefd);
                pipes.push_back(Pipe{in: pipefd[0], out: pipefd[1]});
                #if 0
                debug_pipes();
                #endif
            }
        }

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

            if (is_final_cmd && !(is_number_pipe || is_error_pipe)) {
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
            }

            if (is_final_cmd) {
                if (is_number_pipe) {
                    /* Number Pipe */
                    
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
                    /* Error Pipe */
                    for (size_t x = 0; x < number_pipes.size(); x++) {
                        if (number_pipes[x].number == command.number) {
                            dup2(number_pipes[x].out, STDOUT_FILENO);
                            dup2(number_pipes[x].out, STDERR_FILENO);
                            break;
                        }
                    }
                } else {
                    /* Normal Pipe*/
                    if (pipes.size() > 0) {
                        dup2(pipes[i-1].in, STDIN_FILENO);
                        #if 0
                        cout << "Final Pipe (out) " << pipes[i-1].in << " to " << STDIN_FILENO << endl;
                        #endif
                    }
                    
                    // Redirect to socket
                    dup2(me->get_sockfd(), STDOUT_FILENO);
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

    // Run shell
    int code = run_shell(client, input);
    if (code != BUILT_IN_EXIT) {
        command_prompt(client);
    }

    return code;
}

#endif