#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include "mlogger.h"
#include "mdebug.h"
#include "mnet.h"

const size_t MAX_MSG = 32;
const int SLEEP_TIME = 30;

#define MAX_PATH 1024

char *
read_msg(int sockfd, char *msg, char *leftover) {
    char buffer[MAX_MSG];
    char *rpt = NULL;

    bzero(buffer, MAX_MSG);

    // If there is anything in the leftover buffer, put it on the msg buffer.
    if (leftover[0] != '\0') {
        strncat(msg, leftover, MAX_MSG);
        leftover[0] = '\0';
    }

    ssize_t bytes_total = 0;
    int found = 0;
    while ((bytes_total < MAX_MSG) && (! found)) {
        ssize_t bytes = recv(sockfd, buffer, MAX_MSG-bytes_total, 0);
        bytes_total += bytes;
        logmsg(MLOG_DEBUG, "read %d bytes", bytes);
        logmsg(MLOG_DEBUG, "bytes_total is %d", bytes_total);
        if (bytes < 0) {
            logmsg(MLOG_ERROR, "read error - %s", strerror(errno));
            break;
        } else if (bytes == 0) {
            logmsg(MLOG_ERROR, "read 0 bytes - the server dropped me!");
            break;
        } else {
            strncat(msg, buffer, bytes);
        }
        logmsg(MLOG_DEBUG, "msg is '%s' - looking for delimeters", msg);
        // We are done when we find a \r\n.
        for (int i = 0; i < MAX_MSG-1; ++i) {
            if (msg[i] == '\0') {
                logmsg(MLOG_DEBUG, "found NULL at byte %d", i);
                break;
            } else if ((msg[i] == '\r') && (msg[i+1] == '\n')) {
                // Found it.
                logmsg(MLOG_DEBUG, "found message delimiter at byte %d", i);
                msg[i] = '\0';
                // Put remaining bytes into the leftover buffer.
                strncat(leftover, msg + i + 2, bytes_total - i - 2);
                logmsg(MLOG_DEBUG, "put %d bytes onto leftover buffer", bytes_total-i-2);
                logmsg(MLOG_DEBUG, "leftover buffer is now '%s'", leftover);
                bytes_total = 0;
                found = 1;
                rpt = msg;
                break;
            }
        }
    }
    return rpt;
}

int
ping_pong_loop(int sockfd) {
    char msg[MAX_MSG];
    char leftover[MAX_MSG];

    bzero(leftover, MAX_MSG);

    for (;;) {
        int bytes = send(sockfd, "PING\r\n", 6, 0);
        logmsg(MLOG_DEBUG, "sent %d bytes", bytes);
        assert( bytes == 6 );

        bzero(msg, MAX_MSG);
        if (read_msg(sockfd, msg, leftover) == NULL) {
            logmsg(MLOG_ERROR, "error reading next message");
            return 0;
        } else {
            logmsg(MLOG_INFO, "message received: %s", msg);
        }
        if (strncmp(msg, "PONG", 4) == 0) {
            logmsg(MLOG_DEBUG, "PONG received, all is well - sleeping for %ds", SLEEP_TIME);
            sleep(SLEEP_TIME);
        } else {
            logmsg(MLOG_ERROR, "Unknown error received: %s", msg);
            return 0;
        }
    }
}

char *
pubip_path(char *buffer) {
    // Defaults to $HOME/.connmon_pubip
    char *home = getenv("HOME");
    if (home == NULL) {
        // Default to null string so we use the current directory
        logmsg(MLOG_WARNING, "no HOME environment variable - please set");
        strcpy(buffer, "");
        strcat(buffer, ".connmon_pubip");
    } else {
        strncpy(buffer, home, MAX_PATH);
        strcat(buffer, "/.connmon_pubip");
    }
    return buffer;
}

int
load_pubip(char *addr) {
    char path[MAX_PATH];
    pubip_path(path);

    FILE *pfile = NULL;
    if ((pfile = fopen(path, "r")) == NULL) {
        logmsg(MLOG_ERROR, "fopen of %s failed: %s", path, strerror(errno));
        return 0;
    }
    fgets(addr, MAX_PATH, pfile);
    fclose(pfile);
    // Clear the newline.
    int length = strnlen(addr, MAX_PATH);
    if (addr[length-1] == '\n') {
        addr[length-1] = '\0';
    }
    return 1;
}

int
store_pubip(char *msg) {
    char path[MAX_PATH];
    pubip_path(path);

    FILE *pfile = NULL;
    if ((pfile = fopen(path, "w")) == NULL) {
        logmsg(MLOG_ERROR, "fopen of %s failed: %s", path, strerror(errno));
        return 0;
    }
    fprintf(pfile, "%s\n", msg);
    fclose(pfile);
    return 1;
}

void
ipchange(char *newip, char *oldip) {
    logmsg(MLOG_INFO, "****************** IP Change ********************");
    logmsg(MLOG_INFO, "old = %s", oldip);
    logmsg(MLOG_INFO, "new = %s", newip);
    // FIXME: what else? take some other action?
}

int
main(int argc, char *argv[]) {
    char leftover[MAX_MSG];
    char msg[MAX_MSG];
    char oldip[MAX_MSG];

    bzero(leftover, MAX_MSG);
    bzero(oldip, MAX_MSG);

    setloggertype(LOGGER_STDERR, NULL);
    setloggersev(MLOG_INFO);

    char *connect_ip = NULL;
    char *connect_port = NULL;
    int opt;
    int reconnect = 0;

    char *usage = "Usage: connmon_server <-i connect ip> <-p connect port> [-d]\n";
    if (argc < 3) {
        fprintf(stderr, usage);
        exit(1);
    }
    while ((opt = getopt(argc, argv, "i:p:dr")) != -1) {
        switch (opt) {
            case 'i':
                connect_ip = optarg;
                break;
            case 'p':
                connect_port = optarg;
                break;
            case 'd':
                setloggersev(MLOG_DEBUG);
                break;
            case 'r':
                reconnect = 1;
                break;
            default:
                logmsg(MLOG_ERROR, "Unknown option");
                exit(1);
        }
    }

    if (load_pubip(oldip)) {
        logmsg(MLOG_INFO, "loaded old IP: %s", oldip);
    } else {
        logmsg(MLOG_INFO, "no old IP found");
    }

    logmsg(MLOG_INFO, "connecting to %s:%s", connect_ip, connect_port);
    int sockfd = connect_tcp_client((const char*)connect_ip, (const char*)connect_port);

    if (sockfd > 0) {
        logmsg(MLOG_INFO, "connected");
    } else {
        logmsg(MLOG_ERROR, "failed to connect");
        exit(2);
    }

    bzero(msg, MAX_MSG);
    if (read_msg(sockfd, msg, leftover) == NULL) {
        logmsg(MLOG_ERROR, "error reading next message");
        exit(2);
    } else {
        logmsg(MLOG_INFO, "received first message: '%s'", msg);
        store_pubip(msg);
        if (strncmp(msg, oldip, MAX_MSG) != 0) {
            ipchange(msg, oldip);
        }
        fflush(stdout);
    }

    for (;;) {
        int rv = ping_pong_loop(sockfd);
        if (! rv) {
            logmsg(MLOG_ERROR, "ping_pong_loop returned an error");
            if (! reconnect) {
                exit(2);
            }
        }
    }

    exit(0);
}
