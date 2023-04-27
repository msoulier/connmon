#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mlogger.h"
#include "mdebug.h"
#include "mnet.h"

int
main(int argc, char *argv[]) {
    setloggertype(LOGGER_STDOUT, NULL);
    setloggersev(MLOG_DEBUG);

    logmsg(MLOG_INFO, "connecting to localhost:5150");
    int sockfd = connect_tcp_client("localhost", "5150");

    if (sockfd > 0) {
        logmsg(MLOG_INFO, "connected");
        sleep(300);
    } else {
        logmsg(MLOG_ERROR, "failed to connect");
    }

    return 0;
}
