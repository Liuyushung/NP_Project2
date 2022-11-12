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
#include <sys/syscall.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

using namespace std;

#define MAX_BUF_SIZE    15000
#define CONTENT_SIZE    1025
#define BUILT_IN_EXIT   99
#define BUILT_IN_TRUE   1
#define BUILT_IN_FALSE  0
#define USER_LIMIT      30
#define USERSHMKEY ((key_t) 7890)
#define MSGSHMKEY  ((key_t) 7891)

typedef struct my_user {
    int uid;
    int sockfd;
    pid_t pid;
    bool is_active;
    char name[32];
    char ip_addr[INET6_ADDRSTRLEN];
} User;

typedef struct my_message {
    int length;
    int counter;
    bool is_active;
    char content[CONTENT_SIZE];
} Message;

/* Global Value */
int user_shm_id, msg_shm_id;
User *user_shm_ptr;
Message *msg_shm_ptr;
int listen_sock;
pthread_mutex_t user_mutex, msg_mutex;

/* Function Prototype */
int get_online_user_number();
void sendout_msg(int sockfd, string &msg);
/* Function Prototype End */

void init_shm() {
    void *tmp_ptr;

    // Get user shared memory ID
    user_shm_id = shmget(USERSHMKEY, sizeof(User) * USER_LIMIT, IPC_CREAT | IPC_EXCL | SHM_R | SHM_W);
    if (user_shm_id < 0) {
        perror("Get user shm");
        exit(0);
    }
    // Attach user shared memory
    tmp_ptr = shmat(user_shm_id, NULL, 0);
    if (*((int*) tmp_ptr) == -1) {
        perror("Map user shm");
        exit(0);
    }
    user_shm_ptr = static_cast<User *>(tmp_ptr);
    bzero((char *)user_shm_ptr, sizeof(User) * USER_LIMIT);

    #if 0
    for(int x=0; x < USER_LIMIT; ++x) {
        if (!user_shm_ptr[x].is_active) {
            cout << "UID: " << x+1 << " is not active" << endl;
        }
    }
    #endif

    // Get message shared memory ID
    msg_shm_id = shmget(MSGSHMKEY, sizeof(Message) * 1, IPC_CREAT | IPC_EXCL | SHM_R | SHM_W);
    if (msg_shm_id < 0) {
        perror("Get msg shm");
        exit(0);
    }
    // Attach message shared memory
    tmp_ptr = shmat(msg_shm_id, NULL, 0);
    if (*((int*) tmp_ptr) == -1) {
        perror("Map msg shm");
        exit(0);
    }
    msg_shm_ptr = static_cast<Message *>(tmp_ptr);
    bzero((char *)msg_shm_ptr, sizeof(Message) * 1);

    return;
}

void init_lock() {
    pthread_mutexattr_t user_mutex_attr;
    pthread_mutexattr_t msg_mutex_attr;

    pthread_mutexattr_init(&user_mutex_attr);
    pthread_mutexattr_setpshared(&user_mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&user_mutex, &user_mutex_attr);

    pthread_mutexattr_init(&msg_mutex_attr);
    pthread_mutexattr_setpshared(&msg_mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&msg_mutex, &msg_mutex_attr);
}

/* Server Related*/
void server_exit() {
    cout << "Wait all user leave..." << endl;
    while (true) {
        int counter = get_online_user_number();
        if (counter == 0) {
            break;
        } else {
            cout << "Remain " << counter << " users online" << endl;
            usleep(100);
        }
    }

    cout << "Detach and Remove Shared Memory" << endl;
    if (shmdt(user_shm_ptr) < 0) {
        perror("Detach user shm");
    }
    if (shmctl(user_shm_id, IPC_RMID, NULL) < 0) {
        perror("Remove user shared memory");
    }
    if (shmdt(msg_shm_ptr) < 0) {
        perror("Detach message shm");
    }
    if (shmctl(msg_shm_id, IPC_RMID, NULL) < 0) {
        perror("Remove message shared memory");
    }
    cout << "Close listen socket" << endl;
    close(listen_sock);


    exit(0);
}

void signal_server_handler(int sig) {
    if (sig == SIGCHLD) {
		while(waitpid (-1, NULL, WNOHANG) > 0);
	} else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM){
        server_exit();
	}
}

bool is_user_up_to_limit() {
    for (int x=0; x < USER_LIMIT; ++x) {
        if (!user_shm_ptr[x].is_active) {
            return false;
        }
    }
    return true;
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

int get_online_user_number() {
    int counter = 0;

    pthread_mutex_lock(&user_mutex);
    for(int x=0; x < USER_LIMIT; ++x) {
        if (user_shm_ptr[x].is_active) {
            ++counter;
        }
    }
    pthread_mutex_unlock(&user_mutex);

    return counter;
}

int get_sockfd_by_pid(pid_t pid) {
    for(int x=0; x < USER_LIMIT; ++x) {
        if (user_shm_ptr[x].is_active && user_shm_ptr[x].pid == pid) {
            return user_shm_ptr[x].sockfd;
        }
    }
}

int get_uid_by_pid(pid_t pid) {
    for(int x=0; x < USER_LIMIT; ++x) {
        if (user_shm_ptr[x].is_active && user_shm_ptr[x].pid == pid) {
            return user_shm_ptr[x].uid;
        }
    }
}
/* Server Related End*/

/* User Related */
void debug_user() {
    cout << "***** Debug user start" << endl;
    for (size_t i = 0; i < USER_LIMIT; i++) {
        if (user_shm_ptr[i].is_active) {
            printf("User (%d):\tname: %s\n\tpid: %d\n\tsockfd: %d\n\tip: %s\n",
                user_shm_ptr[i].uid,
                user_shm_ptr[i].name,
                user_shm_ptr[i].pid,
                user_shm_ptr[i].sockfd,
                user_shm_ptr[i].ip_addr
            );
        }
    }
    cout << "***** Debug user end" << endl;
}

int create_user(int sock, sockaddr_in addr) {
    // Invoked in child process
    int uid;
    char ip[INET6_ADDRSTRLEN];
    sprintf(ip, "%s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    pthread_mutex_lock(&user_mutex);
    for (uid=1; uid <= USER_LIMIT; ++uid) {
        if(user_shm_ptr[uid-1].is_active == false) {
            user_shm_ptr[uid-1].uid = uid;
            user_shm_ptr[uid-1].pid = getpid();
            user_shm_ptr[uid-1].is_active = true;
            user_shm_ptr[uid-1].sockfd = sock;
            strcpy(user_shm_ptr[uid-1].name, "(no name)");
            strncpy(user_shm_ptr[uid-1].ip_addr, ip, INET6_ADDRSTRLEN);

            break;
        }
    }
    pthread_mutex_unlock(&user_mutex);

    return uid;
}

void user_exit_procedure(int uid) {
    close(user_shm_ptr[uid-1].sockfd);
    bzero(&(user_shm_ptr[uid-1]), sizeof(User));
    exit(0);
}

void signal_child_handler(int sig) {
    char buf[CONTENT_SIZE];
    bzero(buf, CONTENT_SIZE);

    if (sig == SIGUSR1) {
        // Receive boradcast message
        while (true) {
            pthread_mutex_lock(&msg_mutex);
            strncpy(buf, msg_shm_ptr->content, msg_shm_ptr->length);
            msg_shm_ptr->counter--;

            if (msg_shm_ptr->counter <= 0) {
                msg_shm_ptr->is_active = false;
            }
            pthread_mutex_unlock(&msg_mutex);
            break;
        }

        string msg(buf);
        sendout_msg(get_sockfd_by_pid(getpid()), msg);
    } else if (sig == SIGUSR2) {
        // Receive user pipe
	} else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM){
        user_exit_procedure(get_uid_by_pid(getpid()));
    }


}
/* User Related End*/

/* Network IO */
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
/* Network IO End */

void broadcast(string msg) {
    while (true) {
        pthread_mutex_lock(&msg_mutex);
        if (msg_shm_ptr->is_active) {
            pthread_mutex_unlock(&msg_mutex);
            usleep(100);
            continue;
        } else {
            msg_shm_ptr->is_active = true;
            msg_shm_ptr->counter = get_online_user_number();
            msg_shm_ptr->length = msg.length();
            strncpy(msg_shm_ptr->content, msg.c_str(), msg.length());
            pthread_mutex_unlock(&msg_mutex);
            break;
        }
    }

    for (int x=0; x < USER_LIMIT; ++x) {
        if (user_shm_ptr[x].is_active) {
            kill(user_shm_ptr[x].pid, SIGUSR1);
        }
    }
    
    return;
}

void login_prompt(int uid) {
    ostringstream oss;
    string msg;

    // Create message
    oss << "*** User '" << user_shm_ptr[uid-1].name << "' entered from " << user_shm_ptr[uid-1].ip_addr << ". ***" << endl;
    msg = oss.str();

    // Broadcast
    broadcast(msg);
}

void logout_prompt(int uid) {
    ostringstream oss;
    string msg;

    // Create message
    oss << "*** User '" << user_shm_ptr[uid-1].name << "' left. ***" << endl;
    msg = oss.str();

    // Broadcast
    broadcast(msg);
}

void command_prompt(int uid) {
    string msg("% ");
    sendout_msg(user_shm_ptr[uid-1].sockfd, msg);
}

void welcome(int uid) {
    ostringstream oss;
    string msg;

    oss << "****************************************" << endl
        << "** Welcome to the information server. **" << endl
        << "****************************************" << endl;
    msg = oss.str();

    sendout_msg(user_shm_ptr[uid-1].sockfd, msg);
}

void serve_client(int uid) {
    welcome(uid);
    login_prompt(uid);
    command_prompt(uid);

    while (true) {
        string input = read_msg(user_shm_ptr[uid-1].sockfd);
        sendout_msg(user_shm_ptr[uid-1].sockfd, input);
    }

}

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
            signal(SIGUSR2, signal_child_handler);
            signal(SIGINT, signal_child_handler);
            signal(SIGQUIT, signal_child_handler);
            signal(SIGTERM, signal_child_handler);
            // Create user
            int uid = create_user(client_sock, c_addr);
            serve_client(uid);
        } else {
            perror("Server fork");
            server_exit();
        }
        
        #if 1
        debug_user();
        #endif
    }
}