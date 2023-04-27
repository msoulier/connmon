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

    logmsg(MLOG_INFO, "Listening on 0.0.0.0:5150");
    int sockfd = setup_tcp_server("0.0.0.0", 5150, 10);

    sleep(300);

    return 0;
}
