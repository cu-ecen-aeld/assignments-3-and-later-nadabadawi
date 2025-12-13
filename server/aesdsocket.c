#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <pthread.h>
#include <stdbool.h>

volatile sig_atomic_t running = 1;
static int sock_fd = -1;

typedef struct thread_node {
    pthread_t thread;
    int client_fd;
    bool completed;
    SLIST_ENTRY(thread_node) entries;
} thread_node_t;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int signo)
{
    running = 0;
    if (sock_fd != -1) {
        close(sock_fd);   // FORCE accept() to unblock
        sock_fd = -1;
    }
    // _exit(0); 
}


void *handle_connection(void *arg)
{
    thread_node_t *node = (thread_node_t *)arg;
    int client_fd = node->client_fd;

    char buffer[1024];
    char *partial = NULL;
    size_t partial_len = 0;
    size_t partial_cap = 0;

    while (1) {
        ssize_t recv_len = recv(client_fd, buffer, sizeof(buffer), 0);
        if (recv_len <= 0) {
            break;
        }

        if (partial_len + recv_len > partial_cap) {
            size_t new_cap = partial_cap ? partial_cap * 2 : 4096;
            while (new_cap < partial_len + recv_len)
                new_cap *= 2;

            char *tmp = realloc(partial, new_cap);
            if (!tmp) {
                syslog(LOG_ERR, "realloc failed");
                break;
            }
            partial = tmp;
            partial_cap = new_cap;
        }

        memcpy(partial + partial_len, buffer, recv_len);
        partial_len += recv_len;

        char *newline = memchr(partial, '\n', partial_len);
        if (newline) {
            size_t packet_len = newline - partial + 1;

            /* LOCK for write + read + send */
            pthread_mutex_lock(&file_mutex);

            FILE *wf = fopen("/var/tmp/aesdsocketdata", "a+");
            if (!wf) {
                syslog(LOG_ERR, "open failed: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                break;
            }

            fwrite(partial, 1, packet_len, wf);
            fflush(wf);
            rewind(wf);

            char sendbuf[1024];
            size_t r;
            while ((r = fread(sendbuf, 1, sizeof(sendbuf), wf)) > 0) {
                size_t s = send(client_fd, sendbuf, r, 0);
                if (s < 0) {
                    syslog(LOG_ERR, "send failed: %s", strerror(errno));
                    break;
                }
            }

            fclose(wf);
            pthread_mutex_unlock(&file_mutex);

            break;  // ONE request per connection
        }
    }

    free(partial);

    pthread_mutex_lock(&list_mutex);
    node->completed = true;
    pthread_mutex_unlock(&list_mutex);

    close(client_fd);
    return NULL;
}


void *timestamp_thread(void *arg)
{
    // printf("Timestamp thread started\n");
    while (running) {
        sleep(10);

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        char timebuf[128];

        // RFC 2822 format
        strftime(timebuf, sizeof(timebuf),
                 "timestamp:%a, %d %b %Y:%H:%M:%S\n",
                 tm_info);

        pthread_mutex_lock(&file_mutex);
        FILE *file = fopen("/var/tmp/aesdsocketdata", "a");
        if (file) {
            fwrite(timebuf, 1, strlen(timebuf), file);
            fclose(file);
            // printf("Wrote timestamp: %s", timebuf);   
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}



int main(int argc, char *argv[])
{
    int daemon_mode = 0;

    if (argc == 2) {
        if (strcmp(argv[1], "-d") == 0) {
            daemon_mode = 1;
        } else {
            syslog(LOG_ERR, "Usage: %s [-d]", argv[0]);
            return -1;
        }
    } else if (argc > 2) {
        syslog(LOG_ERR, "Usage: %s [-d]", argv[0]);
        return -1;
    }
    
    openlog(NULL, 0, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error registering SIGINT handler: %s", strerror(errno));
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error registering SIGTERM handler: %s", strerror(errno));
        return -1;
    }


    // create a socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        syslog(LOG_ERR, "Error creating socket. Error: %s", strerror(errno));
        return -1;
    }

    int optval = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
                &optval, sizeof(optval)) < 0) {
        syslog(LOG_ERR, "setsockopt SO_REUSEADDR failed: %s",
            strerror(errno));
        close(sock_fd);
        return -1;
    }

   
    struct addrinfo *servinfo;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;         // force IPv4 (simplest for AELD)
    hints.ai_socktype = SOCK_STREAM;   // TCP
    hints.ai_flags = AI_PASSIVE;       // for bind()

    int res_addr = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (res_addr != 0) {
        syslog(LOG_ERR, "Error in getaddrinfo. Error: %s", gai_strerror(res_addr));
        return -1;
    }

    int res_bind = bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (res_bind == -1) {
        syslog(LOG_ERR, "Error in bind. Error: %s", strerror(errno));
        freeaddrinfo(servinfo);  
        return -1;
    }
    freeaddrinfo(servinfo);

    FILE *temp_file = fopen("/var/tmp/aesdsocketdata", "w");
    if (!temp_file) {
        syslog(LOG_ERR, "Failed to create/truncate aesdsocketdata: %s",
            strerror(errno));
        close(sock_fd);
        return -1;
    }
    fclose(temp_file);


    int res_listen = listen(sock_fd, 2);
    if (res_listen == -1) {
        syslog(LOG_ERR, "Error in listen. Error: %s", strerror(errno));
        return -1;
    }


    if (daemon_mode) {
        pid_t pid = fork();

        if (pid < 0) {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            close(sock_fd);
            return -1;
        }

        if (pid > 0) {
            closelog();
            exit(0);
        }

        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            close(sock_fd);
            return -1;
        }

        umask(0);

        if (chdir("/") == -1) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            close(sock_fd);
            return -1;
        }

        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    pthread_t ts_thread;
    if (pthread_create(&ts_thread, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "Error creating timestamp thread. Error: %s", strerror(errno));
    }

    SLIST_HEAD(thread_list, thread_node) thread_head;
    SLIST_INIT(&thread_head);
    
    while(running)
    {

        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(sock_fd, &client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (!running && errno == EINTR) {
                // Signal interrupted accept → graceful exit
                break;
            }
            syslog(LOG_ERR, "Error in accept. Error: %s", strerror(errno));
            break;
        }

        // Log the client ip address
        syslog(LOG_INFO, "Accepted connection from client %s", inet_ntoa(((struct sockaddr_in *)&client_addr)->sin_addr) );
        // printf("Accepted connection from client %s\n", inet_ntoa(((struct sockaddr_in *)&client_addr)->sin_addr) );
        
        // create a new thread to handle the client connection
        thread_node_t *node = malloc(sizeof(thread_node_t));
        if (!node) break;

        node->client_fd = client_fd;
        node->completed = false;
        if (pthread_create(&node->thread, NULL, handle_connection, node) != 0) {
            syslog(LOG_ERR, "Error creating connection thread. Error: %s", strerror(errno));
            free(node);
            continue;
        }

        pthread_mutex_lock(&list_mutex);
        SLIST_INSERT_HEAD(&thread_head, node, entries);
        

        thread_node_t *iter, *tmp;
        // Check if any thread is completed and join it
        SLIST_FOREACH(iter, &thread_head, entries) 
        {
            if (iter->completed) {
                SLIST_REMOVE(&thread_head, iter, thread_node, entries);
                pthread_mutex_unlock(&list_mutex);
                pthread_join(iter->thread, NULL);
                free(iter);
                break; // Restart the iteration since the list has changed
            }
        }
        pthread_mutex_unlock(&list_mutex);
    }
    syslog(LOG_INFO, "Shutting down");
    if (sock_fd != -1) {
        close(sock_fd);
        sock_fd = -1;
    }
    syslog(LOG_INFO, "Caught signal, exiting");
    pthread_join(ts_thread, NULL);
    // printf("Caught signal, exiting\n");
    remove("/var/tmp/aesdsocketdata");
    closelog();
    // _exit(0);

    return 0;
}