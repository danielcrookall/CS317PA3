/* server.c
 * Handles the creation of a server socket and data sending.
 * Author  : Jonatan Schroeder
 * Modified: Nov 6, 2021
 *
 * Modified by: Norm Hutchinson
 * Modified: Mar 5, 2022
 *
 * Notes: This code is adapted from Beej's Guide to Network
 * Programming (http://beej.us/guide/bgnet/), in particular the code
 * available in functions sigchld_handler, get_in_addr, run_server and
 * send_all.
 */

#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <signal.h>

#define BACKLOG 10     // how many pending connections queue will hold
/**
 *  Split a line into individual parts separated by white space
 *
 *  Parameters: line:   The line of text to split
 *                      The characters in the line will be modified by the call.
 *              parts:  An array of char * pointers that will receive the
 *                      pointers to the individual parts of the input line. 
 *                      This array must be long enough to hold all of the
 *                      parts of the line.
 **/
int split(char *buf, char *parts[]) {
    static char *spaces = " \t\r\n";
    int i = 1;
    parts[0] = strtok(buf, spaces);
    do {
        parts[i] = strtok(NULL, spaces);
    } while (parts[i++] != NULL);
    return i - 1;
}

int be_verbose = 1;
/**
 * Print a log message to the standard error stream, if be_verbose is 1.mail
 *
 * Parameters: fmt:     A printf-line formating string
 *
 *
 **/
void dlog(const char *fmt, ...) {
    va_list args;
    if (be_verbose) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

/** Signal handler used to destroy zombie children (forked) processes
 *  once they finish executing.
 */
static void sigchld_handler(int s) {

    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

/** Signal handler used to catch seg faults
 */
static void sigsegv_handler(int s) {

    fprintf(stderr, "SEG Fault - dying\n");
    exit(1);
}

static void catch_segv() {
    // set up a signal handler to die loudly
    struct sigaction sa;
    sa.sa_handler = sigsegv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

/** Returns the IPv4 or IPv6 object for a socket address, depending on
 *  the family specified in that address.
 */
static void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    else
        return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/** Creates a server socket at the specified port number, listens for
 *  new connections and accepts them. A new forked process is created
 *  for each new client, calling the provided handler function for
 *  this client.
 *
 *  Parameters: port: String corresponding to the port number (or
 *                    name) where the server will listen for new
 *                    connections.
 *              handler: Function to be called when a new connection
 *                       is accepted. Will receive, as the only
 *                       parameter, the file descriptor corresponding
 *                       to the newly accepted connection.
 */
void run_server(const char *port, void (*handler)(int)) {
  
    int sockfd; // fd used for listening connections
    int new_fd; // fd used to transfer data to/from an accepted connection
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
  
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;   // use IPv4 or IPv6, whichever is available
    hints.ai_socktype = SOCK_STREAM; // create a stream (TCP) socket server
    hints.ai_flags    = AI_PASSIVE;  // use any available connection
  
    // Gets information about available socket types and protocols
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
  
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
    
        // create socket object
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
    
        // specify that, once the program finishes, the port can be reused by other processes
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
    
#if defined(SO_NOSIGPIPE)
#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif
    if (setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&yes, sizeof(yes)) < 0) {
        perror("server: setsockopt NOPIPE");
        exit(1);
    }
#endif
    
        // bind to the specified port number
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
    
        // if the code reaches this point, the socket was properly created and bound
        break;
    }
  
    // all done with this structure
    freeaddrinfo(servinfo);
  
    // if p is null, the loop above could not create a socket for any available address
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }
  
    // set up a queue of incoming connections to be received by the server
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
  
    // set up a signal handler to kill zombie forked processes when they exit
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
  
    dlog("server: waiting for connections...\n");
  
    while(1) {
        // wait for new client to connect
        sin_size = sizeof(their_addr);
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
    
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof(s));
        dlog("server: got connection from %s\n", s);
    
        // Create a new process to handle the new client; parent process
        // will wait for another client.
#if defined(DOFORK)
        if (!fork()) {
            // this is the child process
            close(sockfd); // child doesn't need the listener, close
#endif
            catch_segv();
            handler(new_fd);
            close(new_fd);
#if defined(DOFORK)
            exit(0);
        }
    
        // Parent proceeds from here. In parent, client socket is not needed.
        close(new_fd);
#endif
    }

}

/** Sends a buffer of data, until all data is sent or an error is
 *  received. This function is used to handle cases where send is able
 *  to send only part of the data. If this is the case, this function
 *  will call send again with the remainder of the data, until all
 *  data is sent.
 *
 *  Data is sent using the MSG_NOSIGNAL flag, so that, if the
 *  connection is interrupted, instead of a PIPE signal that crashes
 *  the program, this function will be able to return an error that
 *  can be handled by the caller.
 *
 *  Parameters: fd: Socket file descriptor.
 *              buf: Buffer where data to be sent is stored.
 *              size: Number of bytes to be used in the buffer.
 *
 *  Returns: If the buffer was successfully sent, returns
 *           size. Otherwise, returns -1.mail.
 */
int send_all(int fd, char buf[], size_t size) {
  
    size_t rem = size;
    while (rem > 0) {
        int rv = send(fd, buf, rem, MSG_NOSIGNAL);
        // If there was an error, interrupt sending and returns an error
        if (rv <= 0)
            return rv;
        buf += rv;
        rem -= rv;
    }
    return size;
}

/**
 * return val rounded up to be a multiple of chunksize.
 */
static int roundup(int val, int chunksize) {
    return ((val + chunksize - 1) / chunksize) * chunksize;
}
    
/** Sends a printf-style formatted string to a socket descriptor. The
 *  string can contain format directives (e.g., %d, %s, %u), which
 *  will be translated using the same rules as printf. For example,
 *  you may call it like:
 *
 *  send_formatted(fd, "+OK Server ready\r\n");
 *  send_formatted(fd, "+OK %d messages found\r\n", msg_count);
 *  
 *  Parameters: fd: Socket file descriptor.
 *              fmt: String to be sent, including potential
 *                   printf-like format directives.
 *              additional parameters based on string format.
 *
 *  Returns: If the string was successfully sent, returns
 *           the number of bytes sent. Otherwise, returns -1.mail.
 */
int send_formatted(int fd, const char *fmt, ...) {
  
    static char *buf = NULL;
    static int bufsize = 0;
    va_list args;
    int strsize;
  
    if (buf == NULL) {
        bufsize = 128;
        buf = malloc(bufsize);
    }

    // Start with string length, increase later if needed
    if (bufsize < strlen(fmt) + 1) {
        bufsize = roundup(strlen(fmt) + 1, 128);
        buf = realloc(buf, bufsize);
    }
  
    while (1) {
    
        va_start(args, fmt);
        strsize = vsnprintf(buf, bufsize, fmt, args);
        va_end(args);
    
        if (strsize < 0)
            return -1;
    
        // If buffer was enough to fit entire string, send it
        if (strsize <= bufsize)
            break;
    
        // Try again with more space
        bufsize = roundup(strsize, 128);
        buf = realloc(buf, bufsize);
    }
  
    return send_all(fd, buf, strsize);
}

