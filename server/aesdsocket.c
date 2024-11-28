
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>


#define PORT_NUM "9000"

static char *logfile = "/var/tmp/aesdsocketdata";
static int sock_fd;
static int log_fd;


static void
sigHandler(int sig)
{

    // Delete log file
    if (log_fd >= 0)
        close(log_fd);
    unlink(logfile);

    syslog(LOG_DEBUG, "Caught signal, exiting");
    closelog();
    _exit(0);

}




int main(int argc, char**argv)
{

    // Open syslog
    openlog (NULL, 0, LOG_USER);


 
    // Gracefully exit when SIGINT or SIGTERM is received
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigHandler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog (LOG_DEBUG, "sigaction(SIGINT) failed");
        exit(1);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog (LOG_DEBUG, "sigaction(SIGTERM) failed");
        exit(1);
    }
   


    // 
    // Main loop: socket server
    //

    // Code from 'Linux Programming Interface' (Kerrisk)

    // Call getaddrinfo() to obtain a list of addresses that we can try binding to
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int lfd, cfd, optval, reqLen;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;                            /* Allows IPv4 or IPv6 */
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;           /* Wildcard IP address; service name is numeric */
    if (getaddrinfo(NULL, PORT_NUM, &hints, &result) != 0) {
            syslog(LOG_ERR, "getaddrinfo() failed, exiting");
            exit(-1);
    }


    /* Walk through returned list until we find an address structure
    that can be used to successfully create and bind a socket */
    optval = 1;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        lfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (lfd == -1)          // On error, try next address 
            continue;

        if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            syslog(LOG_ERR, "setsockopt() failed, exiting");
            exit(-1);
        }

        if (bind(lfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;          // Success

        // bind() failed: close this socket and try next address 
        close(lfd);
    }

    if (rp == NULL) {
        syslog(LOG_ERR, "Could not bind socket to any address, exiting");
        exit(-1);
    }

    // Mark as a listening socket
    #define BACKLOG 50                  // Maximum length of queue of pending connections
    if (listen(lfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen() failed, exiting");
        exit(-1);
    }
    freeaddrinfo(result);




    // Instructions specify that daemon not fork until after we ensure we can bind to port 9000
    pid_t pid;
    if (argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        
        // Make yourself a daemon...

        // Fork child process, parent exits
        pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork() failed, exiting");
            exit(1);
        }
        if (pid != 0)               // Parent, so exit
            exit(0);

        // Start a new session
        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid() failed, exiting");
            exit(1);
        }

        // Fork again -- ensure we're not session leader
        pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork() failed, exiting");
            exit(1);
        }
        if (pid != 0)               // Parent, so exit
            exit(0);

        umask(0);                   // Clear file mode creation mask 
        chdir("/");                 // Change to root directory 

        // Close all open files, reopen stdin, stdout, stderr as /dev/null
        int fd;
        for (fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++)
            close(fd);
        fd = open("/dev/null", O_RDWR);
        if (fd != STDIN_FILENO) {
            syslog(LOG_ERR, "open(/dev/null) failed, exiting");
            exit(1);
        }
        if (dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO) {
            syslog(LOG_ERR, "dup2() failed, exiting");
            exit(1);
        }
        if (dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO) {
            syslog(LOG_ERR, "dup2() failed, exiting");
            exit(1);
        }

        syslog (LOG_DEBUG, "Init: aesdsocket daemon");
    }       // daemon initialization complete
    else
        syslog (LOG_DEBUG, "Init: aesdsocket");











    // Handle connections
    struct sockaddr_storage claddr;
#define ADDRSTRLEN (NI_MAXHOST + NI_MAXSERV + 10)    
    char addrStr[ADDRSTRLEN];
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
#define DATABUFLEN 1024
    int datalen = 0;
    int nread;
    char databuf[DATABUFLEN];
    char readbuf[DATABUFLEN];
    for (;;) {

        socklen_t addrlen = sizeof(struct sockaddr_storage);          // Accept a client connection, obtaining client's address 
        cfd = accept(lfd, (struct sockaddr *) &claddr, &addrlen);
        if (cfd == -1) {
            syslog(LOG_WARNING, "accept() error");
            continue;
        }

        if (getnameinfo((struct sockaddr *) &claddr, addrlen, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
            syslog(LOG_INFO, "Accepted connection from (%s, %s)", host, service);
        else
            syslog(LOG_INFO, "Accepted connection from (?UNKNOWN?)");


        // Initially, log file not opened
        log_fd = -1;


        // Process data from client until connection closes
        while (1)
        {

            // Open output file for storing data if not already open
            if (log_fd < 0) {
                // log_fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                log_fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
                if (log_fd == -1) {
                    syslog(LOG_ERR, "open(logfile) failed, exiting");
                    exit(1);
                }
            }

            // Read client data
            nread = read(cfd, databuf + datalen, DATABUFLEN - datalen);
            if (nread <= 0) 
                break;

            // Does data contain a '\n' (end of packet?)
            char *eop = memchr(databuf, '\n', datalen+nread);
            if (!eop)
            {
                // No end-of-packet, so just write buffer to log file
                write(log_fd, databuf, datalen+nread);
                datalen = 0;                // Empty the buffer
            }
            else
            {
                // Data contains '\n' (end of packet)
                write(log_fd, databuf, eop-databuf+1);      // Write data up to and including '\n'
                close(log_fd);
                // Move remaining data to start of buffer
                // datalen = (databuf+DATABUFLEN)-eop-1;       // Length of remaining data
                datalen = (databuf+datalen+nread)-eop-1;       // Length of remaining data after '\n'
                if (datalen)                                // Move it to start of databuf
                    memmove(databuf, eop+1, datalen);


                // Now return all data in packet to sender
                log_fd = open(logfile, O_RDONLY);
                if (log_fd == -1) {
                    syslog(LOG_ERR, "open(logfile) for reading failed, exiting");
                    exit(1);
                }
                while (1)
                {
                    nread = read(log_fd, readbuf, DATABUFLEN);      // Read from log file
                    if (nread <= 0)
                        break;
                    write(cfd, readbuf, nread);                     // Write to socket
                }
                close(log_fd);
                log_fd = -1;                                // Indicate log file needs to be reopened for writing


            }






        }

        // Connection closed
        close(cfd);
        close(log_fd);
        syslog(LOG_INFO, "Closed connection from (%s, %s)", host, service);

    }


// if (write(cfd, seqNumStr, strlen(seqNumStr)) != strlen(seqNumStr))







    closelog();
    exit(0);

}



    // sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    // if (sock_fd == -1) {
    //     syslog(LOG_ERR, "socket() failed, exiting");
    //     exit(-1);
    // }
    // if (bind(sock_fd, ) == -1) {
    //     syslog(LOG_ERR, "bind() failed, exiting");
    //     exit(-1);
    // }


