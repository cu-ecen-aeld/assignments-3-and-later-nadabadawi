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


volatile sig_atomic_t running = 1;

void signal_handler(int signo)
{
    running = 0;
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
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        syslog(LOG_ERR, "Error creating socket. Error: %s", strerror(errno));
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

    int res_bind = bind(sock_fd, servinfo->ai_addr, sizeof(struct sockaddr));
    if (res_bind == -1) {
        syslog(LOG_ERR, "Error in bind. Error: %s", strerror(errno));
        return -1;
    }
    freeaddrinfo(servinfo);

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
        printf("Accepted connection from client %s\n", inet_ntoa(((struct sockaddr_in *)&client_addr)->sin_addr) );
        
        // open file /var/tmp/aesdsocketdata
        FILE *file = fopen("/var/tmp/aesdsocketdata", "a+");
        if (file == NULL) {
            syslog(LOG_ERR, "Error opening the file. Error: %s", strerror(errno));       
            break;
        }

        char buffer[1024];
        char *partial = NULL; 
        size_t partial_len = 0;
        size_t partial_cap = 0;
        while(1)
        {
            ssize_t recv_len = recv(client_fd, buffer, sizeof(buffer), 0);
            if (recv_len == -1) {
                syslog(LOG_ERR, "Error in recv. Error: %s", strerror(errno));
                break;
            }
            if (recv_len == 0) {
                // connection closed by client
                break;
            }

            if(partial_len + recv_len > partial_cap) {
                size_t new_cap = partial_cap == 0 ? 4096 : partial_cap * 2;
                while (new_cap < partial_len + recv_len)
                    new_cap *= 2;

                char *new_buf = realloc(partial, new_cap);
                if (!new_buf) {
                    syslog(LOG_ERR, "malloc/realloc failed, discarding packet");
                    partial_len = 0; // discard everything
                    continue;
                }
                partial = new_buf;
                partial_cap = new_cap;
            }

            memcpy(partial + partial_len, buffer, recv_len);
            partial_len += recv_len;
            // partial[partial_len] = '\0';

            char *newline;
            newline = memchr(partial, '\n', partial_len);

            while ((newline = memchr(partial, '\n', partial_len)) != NULL) {
                int packet_len = (newline - partial) + 1;

                // Write packet to file
                fwrite(partial, 1, packet_len, file);
                fflush(file);
                // int sent_bytes = send(client_fd, partial, packet_len, 0);
                
                char sent_buf[1024];
                size_t read_bytes;
                read_bytes = fread(sent_buf, 1, sizeof(sent_buf), file);
                fseek(file, 0, SEEK_SET); // reset file pointer to beginning

                // Read file in small chunks and send each chunk
                while ((read_bytes = fread(sent_buf, 1, sizeof(sent_buf), file)) > 0) {
                    int sent_bytes = send(client_fd, sent_buf, read_bytes, 0);
                    if (sent_bytes == -1) {
                        syslog(LOG_ERR, "Error in send. Error: %s", strerror(errno));
                        break;
                    }
                }

                int remaining = partial_len - packet_len;
                memmove(partial, partial + packet_len, remaining);
                partial_len = remaining;
            }

        }
        fclose(file);
        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(((struct sockaddr_in *)&client_addr)->sin_addr));
        free(partial);
        close(client_fd);
    }
    syslog(LOG_INFO, "Caught signal, exiting");
    printf("Caught signal, exiting\n");
    close(sock_fd);     
    remove("/var/tmp/aesdsocketdata");    
    closelog();            


    return 0;
}