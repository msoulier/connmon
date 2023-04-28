#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>

#include "mlogger.h"
#include "mdebug.h"
#include "mnet.h"

// 4 chars + \r\n
const size_t PING_SIZE = 6;

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
    ssize_t bytes = write(new_sockfd, client_address, len);
    logmsg(MLOG_DEBUG, "wrote %d bytes to client", bytes);
    if (bytes < 0) {
        logmsg(MLOG_ERROR, "write error: %s", strerror(errno));
        close(new_sockfd);
        new_sockfd = 0;
    } else if (bytes != len) {
        logmsg(MLOG_ERROR, "expected to write %d bytes but wrote %d instead - closing", len, bytes);
        close(new_sockfd);
        new_sockfd = 0;
    } else {
        // Finish with a \r\n
        write(new_sockfd, "\r\n", 2);
    }

    return new_sockfd;
}

int
handle_pingpong(int sockfd) {
    // Wait for a PING\r\n, and respond with PONG\r\n.
    char buffer[PING_SIZE];
    char msg[PING_SIZE];
    size_t remaining = PING_SIZE;
    // Initialize the two buffers
    bzero(buffer, PING_SIZE);
    bzero(msg, PING_SIZE);
    // Loop until the entire message arrives.
    while (remaining > 0) {
        ssize_t bytes = read(sockfd, buffer, remaining);
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
        ssize_t bytes = write(sockfd, "PONG\r\n", PING_SIZE);
        logmsg(MLOG_DEBUG, "wrote %d bytes", bytes);
        assert( bytes == PING_SIZE );
        return 1;
    } else {
        logmsg(MLOG_ERROR, "Unknown message received: %s", msg);
        return 0;
    }
}

int
main(int argc, char *argv[]) {
    setloggertype(LOGGER_STDOUT, NULL);
    setloggersev(MLOG_DEBUG);

    logmsg(MLOG_INFO, "Listening on 0.0.0.0:5150");
    int sockfd = setup_tcp_server("0.0.0.0", 5150, 10);

    for (;;) {
        int new_sockfd = accept_one(sockfd);
        // Now, with this client, stay in a hopefully infinite loop sending
        // PING/PONG back and forth with waits in-between, to keep the connection
        // open.
        for (;;) {
            if (! handle_pingpong(new_sockfd)) {
                logmsg(MLOG_ERROR, "handle_pingpong returned an error - tearing down");
                shutdown(new_sockfd, SHUT_RDWR);
                close(new_sockfd);
                break;
            }
        }
    }

    return 0;
}
