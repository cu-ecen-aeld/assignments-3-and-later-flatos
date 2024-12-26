


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
#include <stdbool.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <poll.h>



#define PORT_NUM "9000"
#define TIME_INTERVAL_MS 10000

#ifdef USE_AESD_CHAR_DEVICE
static char *logfile = "/dev/aesdchar";
#else
static char *logfile = "/var/tmp/aesdsocketdata";
#endif
static int sock_fd;
static int terminate_flg;
static pthread_mutex_t logfile_mutex  = PTHREAD_MUTEX_INITIALIZER;


static void
sigHandler(int sig)
{
    // Handle termination cleanup in main loop
    terminate_flg = 1;
}



//
// Write to log file (thread-safe)
//
void write_log(char *data, int nbytes)
{
    int ret = pthread_mutex_lock(&logfile_mutex);
    if (ret != 0)
    {
        syslog(LOG_ERR, "pthread_mutex_lock() failed, exiting");
        exit(1);
    }

    // Append to log file
    int log_fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (log_fd == -1) {
        syslog(LOG_ERR, "open(logfile) failed, exiting");
        exit(1);
    }
    write(log_fd, data, nbytes);      // Write data up to and including '\n'
    close(log_fd);

    ret = pthread_mutex_unlock(&logfile_mutex);
    if (ret != 0)
    {
        syslog(LOG_ERR, "pthread_mutex_unlock() failed, exiting");
        exit(1);
    }

}


 


//
// Worker thread struct
//
struct wthread_struct {
    pthread_t thread;                           // This thread
    int socket_fd;                              // Socket we're listening to
    struct wthread_struct *next;                // Next struct in linked list
    volatile bool complete;                     // Thread done?

    char host[NI_MAXHOST];                      // Connection details
    char service[NI_MAXSERV];
};


// The linked list of worker thread structs
struct wthread_struct *wthread_list = NULL;


// Add a wthread_struct to the list
void wthread_list_add(struct wthread_struct *newstruct)
{
    newstruct->next = NULL;                 // Just in case...
    if (wthread_list == NULL)
    {
        wthread_list = newstruct;
    }
    else
    {
        struct wthread_struct *sptr = wthread_list;
        while (1) {
            if (sptr->next == NULL) {
                sptr->next = newstruct;
                break;
            }
            sptr = sptr->next;
        }
    }
}


// Remove a wthread_struct from the list
//  Note: struct's memory is not freed...
void wthread_list_remove(struct wthread_struct *oldstruct)
{
    if (wthread_list != NULL)
        if (wthread_list == oldstruct)
        {
            wthread_list = NULL;
        }
        else
        {
            struct wthread_struct *sptr = wthread_list;
            while (1) {
                if (sptr->next == oldstruct)
                {
                    sptr->next = sptr->next->next;
                    break;
                }
                sptr = sptr->next;
                if (sptr == NULL)
                    break;
            }
        }
}


// If a thread has completed,
//  pthread_join() and remove wthread_struct from the list
//  Note: struct's memory _is_ freed...

void wthread_list_join_remove()
{
    struct wthread_struct **pptr = &wthread_list;               // **Note: pointer to (pointer to wthread_struct)
    while (1) {
        if (*pptr == NULL)
            break;
        struct wthread_struct *nextptr = (*pptr)->next;
        if ((*pptr)->complete)
        {
            pthread_join((*pptr)->thread, NULL);
            free(*pptr);
            *pptr = nextptr;
        }
        else 
        {
            pptr = &((*pptr)->next);
        }

    }
}





//
//  Worker thread function
//
void* wthread_func(void* _tdata)
{
    // Input data:
    //  tdata->socket_fd        socket returned from accept()
    //  tdata->complete         set to True when thread has completed
    struct wthread_struct *tdata = (struct wthread_struct *)_tdata;

    const int PKT_BUF_INCR = 1024*64;               // Resizing increment for packet buffer
    int packet_buf_len = PKT_BUF_INCR;              // Initial length
    char *packet_buf = (char*)malloc(PKT_BUF_INCR);        // Allocate initial packet buffer
    int pb_offset = 0;                              // Next available memory in buffer

    const int READ_BUF_LEN = 1024*8;                // Buffer for log file reads
    char *read_buf[READ_BUF_LEN];


    // Process data from client until connection closes
    while (1)
    {
        // If buffer is full, allocate a larger one
        if (pb_offset == packet_buf_len)
        {
            packet_buf_len += PKT_BUF_INCR;
            packet_buf = (char*)realloc(packet_buf, packet_buf_len);
        }

        // Read client data
        int nread = read(tdata->socket_fd, packet_buf + pb_offset, packet_buf_len - pb_offset);
        if (nread <= 0) 
            break;
        pb_offset += nread;

        // Does data contain a '\n' (end of packet?) (inefficient..searches full buffer...good enough for now...)
        // If not, continue filling buffer
        char *eop = memchr(packet_buf, '\n', pb_offset);
        if (!eop)
            continue;

        // We've received the end-of-packet character (newline)
        // Write completed packet to log file
        // Clear packet buffer
        // Return entire logfile to client
        int packet_len = (eop - packet_buf)+1;
        write_log(packet_buf, packet_len);                              // eop points to newline

        // Move remaining data to start of buffer
        memmove(packet_buf, eop+1, pb_offset - packet_len);
        pb_offset = pb_offset - packet_len;                             // Length of remaining data after '\n'


        // Now return all data in log file to client
        int ret = pthread_mutex_lock(&logfile_mutex);

        int log_fd = open(logfile, O_RDONLY);
        if (log_fd == -1) {
            syslog(LOG_ERR, "open(logfile) for reading failed, exiting");
            exit(1);
        }
        while (1)
        {
            nread = read(log_fd, read_buf, READ_BUF_LEN);      // Read from log file
            if (nread <= 0)
                break;
            write(tdata->socket_fd, read_buf, nread);                     // Write to socket
        }
        close(log_fd);

        ret = pthread_mutex_unlock(&logfile_mutex);

    }       // while(1)


    // Disconnected, so clean up and terminate thread
    close(tdata->socket_fd);
    syslog(LOG_INFO, "Closed connection from (%s, %s)", tdata->host, tdata->service);
    free(packet_buf);

    tdata->complete = true;
    return 0;

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









    //
    // Main loop:
    //      Listen for client connections
    //      Spin handler threads
    //      Periodically add time stamp to log file
    //

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

    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd < 0) {
        printf("timerfd_create\n");
        exit(1);
    }
    struct itimerspec interval;
    interval.it_interval.tv_sec = TIME_INTERVAL_MS / 1000;
    interval.it_interval.tv_nsec = (TIME_INTERVAL_MS % 1000) * 1000000;
    interval.it_value.tv_sec = TIME_INTERVAL_MS / 1000;;
    interval.it_value.tv_nsec = (TIME_INTERVAL_MS % 1000) * 1000000;

    int ret = timerfd_settime(timer_fd, 0, &interval, NULL);
    if (ret < 0) {
        printf("timerfd_settime\n");
        exit(1);
    }


#define N_POLL_FD 2
    struct pollfd poll_fds[N_POLL_FD];
    memset(poll_fds, 0, sizeof(struct pollfd)*N_POLL_FD);
    poll_fds[0].fd = timer_fd;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = lfd;
    poll_fds[1].events = POLLIN;


 

    int n = 0;
    for (;;) {

        poll(poll_fds, N_POLL_FD, 20000);

        //
        // Timer expired -- write timestamp to log file
        //
        if (poll_fds[0].revents & POLLIN) {
            uint64_t timersElapsed = 0;
            (void) read(timer_fd, &timersElapsed, sizeof(timersElapsed));
            // printf("-----> timer %lx\n", (long)timersElapsed);

            time_t t = time(NULL);
            struct tm *tmp;
            tmp = localtime(&t);

            char buf[128];
            strftime(buf, 128, "%Y %m %d %H %M %S", tmp);
            char sbuf[256];
            sprintf(sbuf, "timestamp:%s\n", buf);
#ifndef USE_AESD_CHAR_DEVICE            
            write_log(sbuf, strlen(sbuf));
#endif            


        }


        //
        // Socket connection initiated - start a thread to handle it
        //
        else if (poll_fds[1].revents & POLLIN) {
            socklen_t addrlen = sizeof(struct sockaddr_storage);          // Accept a client connection, obtaining client's address 
            cfd = accept(lfd, (struct sockaddr *) &claddr, &addrlen);
            if (cfd == -1) {
                syslog(LOG_WARNING, "accept() error");
            }
            else {

                if (getnameinfo((struct sockaddr *) &claddr, addrlen, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
                    syslog(LOG_INFO, "Accepted connection from (%s, %s)", host, service);
                else
                    syslog(LOG_INFO, "Accepted connection from (?UNKNOWN?)");


                // Create the thread's data struct
                struct wthread_struct *newthread = calloc(1, sizeof(struct wthread_struct));
                wthread_list_add(newthread);
                newthread->socket_fd = cfd;                 // The socket to listen to
                strcpy(newthread->host, host);              // Thread needs its own copy of connection details
                strcpy(newthread->service, service);
                pthread_create(&(newthread->thread), NULL, wthread_func, newthread);









            }
        }




        // 
        // accept() returned for some other reason -- signal?
        //
        else {
        }



        //
        // Clean up terminated threads
        //


        //
        // If terminate_flg set, clean up and exit
        //
        if (terminate_flg) {



#ifndef USE_AESD_CHAR_DEVICE                
            unlink(logfile);                    // Delete log file
#endif            
            syslog(LOG_DEBUG, "Caught signal, exiting");
            closelog();
            _exit(0);

        }


    }
}




