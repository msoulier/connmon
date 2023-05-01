#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include "mlogger.h"
#include "mdebug.h"
#include "mnet.h"
#include "madt.h"

// 4 chars + \r\n
const size_t PING_SIZE = 6;
const size_t QUEUE_SIZE = 5;

typedef struct thread_info {
    pthread_t thread_id;
    int       thread_num;
    int       sockfd;
    int       running;
    struct thread_info *next;
} threadinfo_t;

int
accept_one(int sockfd) {
    struct sockaddr_in client;
    socklen_t len;
    char client_address[INET_ADDRSTRLEN];
    int new_sockfd = 0;

    len = sizeof(client);

    logmsg(MLOG_INFO, "Going into accept");
    new_sockfd = accept(sockfd, (struct sockaddr*)&client, &len);
    logmsg(MLOG_INFO, "Connection established");

    if (inet_ntop(AF_INET, &(client.sin_addr), client_address, INET_ADDRSTRLEN) == NULL) {
        logmsg(MLOG_ERROR, "Failed to resolve client address: %s", strerror(errno));
        strcpy(client_address, "Unknown");
    }
    logmsg(MLOG_INFO, "Client connection from %s:%d", client_address, htons(client.sin_port));

    // Immediately write the client ip to the client.
    ssize_t bytes = send(new_sockfd, client_address, strnlen(client_address, INET_ADDRSTRLEN), 0);
    logmsg(MLOG_DEBUG, "wrote %d bytes to client", bytes);
    if (bytes < 0) {
        logmsg(MLOG_ERROR, "write error: %s", strerror(errno));
        close(new_sockfd);
        new_sockfd = 0;
    } else {
        // Finish with a \r\n
        send(new_sockfd, "\r\n", 2, 0);
    }

    return new_sockfd;
}

int
handle_pingpong(int sockfd) {
    logmsg(MLOG_DEBUG, "in handle_pingpong on fd %d", sockfd);
    // Wait for a PING\r\n, and respond with PONG\r\n.
    char buffer[PING_SIZE];
    char msg[PING_SIZE];
    size_t remaining = PING_SIZE;
    // Initialize the two buffers
    bzero(buffer, PING_SIZE);
    bzero(msg, PING_SIZE);
    // Loop until the entire message arrives.
    while (remaining > 0) {
        ssize_t bytes = recv(sockfd, buffer, remaining, 0);
        remaining -= bytes;
        logmsg(MLOG_DEBUG, "read %d bytes, remaining is %d", bytes, remaining);
        assert( remaining >= 0 );
        if (bytes > 0) {
            strncat(msg, buffer, bytes);
        } else {
            logmsg(MLOG_ERROR, "read 0 bytes!");
            return 0;
        }
    }
    logmsg(MLOG_DEBUG, "composed final message: %s", msg);
    if (strncmp(msg, "PING\r\n", PING_SIZE) == 0) {
        logmsg(MLOG_DEBUG, "sending a PONG");
        ssize_t bytes = send(sockfd, "PONG\r\n", PING_SIZE, 0);
        logmsg(MLOG_DEBUG, "wrote %d bytes", bytes);
        assert( bytes == PING_SIZE );
        return 1;
    } else {
        logmsg(MLOG_ERROR, "Unknown message received: %s", msg);
        return 0;
    }
}

static void *
thread_start(void *arg) {

    threadinfo_t *tinfo = (struct thread_info*)arg;
    logmsg(MLOG_INFO, "handler for thread id %d starting", tinfo->thread_id);
    // Now, with this client, stay in a hopefully infinite loop sending
    // PING/PONG back and forth with waits in-between, to keep the
    // connection open.
    for (;;) {
        logmsg(MLOG_DEBUG, "calling handle_pingpong on fd %d", tinfo->sockfd);
        if (! handle_pingpong(tinfo->sockfd)) {
            logmsg(MLOG_ERROR, "handle_pingpong returned an error - tearing down");
            shutdown(tinfo->sockfd, SHUT_RDWR);
            close(tinfo->sockfd);
            break;
        }
    }
    logmsg(MLOG_INFO, "thread %d exiting", tinfo->thread_id);
    tinfo->running = 0;
    return arg;
}

int
check(threadinfo_t *handle, threadinfo_t *current) {
    if (handle->running == 0) {
        return 1;
    } else {
        return 0;
    }
}

void
housekeeping(threadinfo_t *tinfo, threadinfo_t *current) {
    threadinfo_t *previous, *freenode, *handle;
    previous = freenode = handle = NULL;
    logmsg(MLOG_DEBUG, "housekeeping running");
    for (;;) {
        freenode = NULL;
        mlinked_list_remove(tinfo, current, previous, freenode, check, handle);
        if (freenode != NULL) {
            logmsg(MLOG_INFO, "found a stopped thread: %d", freenode->thread_id);
            pthread_join(freenode->thread_id, NULL);
            free(freenode);
        }
    }
}

int
main(int argc, char *argv[]) {
    setloggertype(LOGGER_STDOUT, NULL);
    setloggersev(MLOG_INFO);

    char *listen_ip = NULL;
    int listen_port = 0;
    int opt;
    threadinfo_t *tinfo = NULL;
    threadinfo_t *current_thread = NULL;

    char *usage = "Usage: connmon_server <-i listen ip> <-p listen port> [-d]\n";
    if (argc < 3) {
        fprintf(stderr, usage);
        exit(1);
    }
    while ((opt = getopt(argc, argv, "i:p:d")) != -1) {
        switch (opt) {
            case 'i':
                listen_ip = optarg;
                break;
            case 'p':
                listen_port = atoi(optarg);
                break;
            case 'd':
                setloggersev(MLOG_DEBUG);
                break;
            default:
                logmsg(MLOG_ERROR, "Unknown option");
                exit(1);
        }
    }

    if (listen_port == 0) {
        logmsg(MLOG_ERROR, "listen_port must be > 0");
        exit(1);
    }

    logmsg(MLOG_INFO, "Listening on %s:%d", listen_ip, listen_port);
    int sockfd = setup_tcp_server(listen_ip, listen_port, QUEUE_SIZE);

    for (;;) {
        int new_sockfd = accept_one(sockfd);

        threadinfo_t *new_thread = (threadinfo_t*)malloc(sizeof(struct thread_info));
        assert( new_thread != NULL );

        new_thread->sockfd = new_sockfd;
        new_thread->running = 1;

        mlinked_list_add(tinfo, new_thread, current_thread);

        // Now start a handler thread for this client.
        int rv = pthread_create(&new_thread->thread_id, NULL,
                                &thread_start, new_thread);
        if (rv != 0) {
            perror("pthread_create");
            close(new_sockfd);
        }

        housekeeping(tinfo, current_thread);
    }

    return 0;
}
