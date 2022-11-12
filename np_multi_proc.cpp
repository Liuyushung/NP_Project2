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
    char content[1025];
} Message;

/* Global Value */
int user_shm_id, msg_shm_id;
User *user_shm_ptr;
Message *msg_shm_ptr;
int listen_sock;

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
    msg_shm_id = shmget(MSGSHMKEY, sizeof(Message) * 10, IPC_CREAT | IPC_EXCL | SHM_R | SHM_W);
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
    bzero((char *)msg_shm_ptr, sizeof(Message) * 10);

    return;
}

void server_exit() {
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

    return uid;
}

void user_exit_procedure(int uid) {
    close(user_shm_ptr[uid-1].sockfd);
    bzero(&(user_shm_ptr[uid-1]), sizeof(User));
    exit(0);
}

void signal_child_handler(int sig) {

}
/* User Related End*/

int main(int argc,char const *argv[]) {
    if (argc != 2) {
        cout << "Usage: prog port" << endl;
        exit(0);
    }

    /* Initialize shared memory */
    init_shm();

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
            int uid = create_user(client_sock, c_addr);
            cout << "Pid: " << getpid() << " Uid: " << uid << endl;
            sleep(10);
            user_exit_procedure(uid);
        } else {
            perror("Server fork");
            server_exit();
        }
        
        #if 1
        debug_user();
        #endif
    }
}