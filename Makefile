CC = gcc
CFLAGS = -Wall -I../mikelibc
COBJS = connmon_client.o
SOBJS = connmon_server.o
LDFLAGS = -L../mikelibc
LIBS=

ifeq ($(CPP),1)
	CFLAGS += -E
endif

ifeq ($(MDEBUG),1)
	CFLAGS += -ggdb -fsanitize=address
	LIBS = -lasan
endif

LIBS += -lpthread -lmike

all: mikelibc cclient cserver

help:
	@echo "Targets: help all mikelibc connmon_client connmon_server"
	@echo "CFLAGS is $(CFLAGS)"
	@echo "LDFLAGS is $(LDFLAGS)"
	@echo "LIBS is $(LIBS)"

mikelibc:
	cd ../mikelibc&& make MDEBUG=$(MDEBUG) MTHREADS=1

cclient: $(COBJS)
	$(CC) -o cclient $(LDFLAGS) $(COBJS) $(LIBS)

cserver: $(SOBJS)
	$(CC) -o cserver $(LDFLAGS) $(SOBJS) $(LIBS)

connmon_client.o: connmon_client.c ../mikelibc/mnet.h ../mikelibc/libmike.a
	$(CC) $(CFLAGS) -c connmon_client.c

connmon_server.o: connmon_server.c ../mikelibc/mnet.h ../mikelibc/libmike.a
	$(CC) $(CFLAGS) -c connmon_server.c

clean:
	rm -f *.o cclient cserver make.err
	(cd ../mikelibc && make clean)
