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
    char      address[128];
    struct thread_info *next;
} threadinfo_t;

int
accept_one(int sockfd, threadinfo_t *thread) {
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

    strcpy(thread->address, client_address);
    thread->sockfd = new_sockfd;
    thread->running = 1;

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
    char buffer[PING_SIZE+1];
    char msg[PING_SIZE+1];
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
    threadinfo_t *tinfo = (threadinfo_t*)arg;
    logmsg(MLOG_INFO, "handler for thread id %u starting", tinfo->thread_id);
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
    logmsg(MLOG_INFO, "thread %u exiting", tinfo->thread_id);
    tinfo->running = 0;
    return arg;
}

int
check(threadinfo_t *handle, threadinfo_t *current) {
    assert( handle == NULL );
    assert( current != NULL );
    logmsg(MLOG_DEBUG, "in check: thread_id and running: %u %d",
            current->thread_id, current->running);
    if (current->running == 0) {
        return 1;
    } else {
        return 0;
    }
}

void
housekeeping(threadinfo_t *tinfo, threadinfo_t *current) {
    assert( tinfo != NULL );
    threadinfo_t *previous, *freenode, *handle;
    previous = freenode = handle = NULL;
    logmsg(MLOG_DEBUG, "housekeeping running");
    int found = 0;
    for (;;) {
        freenode = NULL;
        mlinked_list_remove(tinfo, current, previous, freenode, check, handle);
        if (freenode == NULL) {
            break;
        } else {
            logmsg(MLOG_DEBUG, "found a stopped thread: %u", freenode->thread_id);
            int length = 0;
            mlinked_list_length(tinfo, current, length);
            logmsg(MLOG_DEBUG, "list length is %d after remove", length);
            pthread_join(freenode->thread_id, NULL);
            free(freenode);
            found++;
        }
    }
    char plural = 's';
    if (found == 1) {
        plural = 0;
    }
    logmsg(MLOG_DEBUG, "cleaned %d thread%c", found, plural);
}

void
connection_report(threadinfo_t *tinfo) {
    logmsg(MLOG_INFO, "**************** Connection Report ****************");
    if (tinfo == NULL) {
        logmsg(MLOG_INFO, "No connected clients");
    } else {
        for (;;) {
            logmsg(MLOG_INFO, "Connection from %s", tinfo->address);
            if (tinfo->running) {
                logmsg(MLOG_INFO, "Thread is running");
            } else {
                logmsg(MLOG_INFO, "Thread is not running");
            }
            tinfo = tinfo->next;
            if (tinfo == NULL) {
                break;
            }
        }
    }
    logmsg(MLOG_INFO, "**************** Connection Report ****************");
}

int
main(int argc, char *argv[]) {
    setloggertype(LOGGER_STDERR, NULL);
    setloggersev(MLOG_INFO);

    char *listen_ip = NULL;
    int listen_port = 0;
    int opt;
    threadinfo_t *tinfo = NULL;
    threadinfo_t *current = NULL;

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
        threadinfo_t *new_thread = (threadinfo_t*)malloc(sizeof(struct thread_info));
        assert( new_thread != NULL );
        bzero(new_thread, sizeof(threadinfo_t));

        int new_sockfd = accept_one(sockfd, new_thread);

        mlinked_list_add(tinfo, new_thread, current);
        assert( tinfo != NULL );
        assert( current != NULL );
        logmsg(MLOG_DEBUG, "new_thread->running is %d", new_thread->running);
        int length = 0;
        mlinked_list_length(tinfo, current, length);
        logmsg(MLOG_DEBUG, "list length is %d after add", length);

        // Now start a handler thread for this client.
        int rv = pthread_create(&new_thread->thread_id, NULL,
                                &thread_start, new_thread);
        if (rv != 0) {
            perror("pthread_create");
            close(new_sockfd);
        }
        if (tinfo != NULL) {
            housekeeping(tinfo, current);
        }
        connection_report(tinfo);
    }

    return 0;
}
