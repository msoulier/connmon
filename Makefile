CC = gcc
CFLAGS = -Wall -I../mikelibc
COBJS = connmon_client.o
SOBJS = connmon_server.o
LIBS = -lpthread -lmike -ltai
LDFLAGS = -L../mikelibc

ifeq ($(CPP),1)
	CFLAGS += -E
endif

ifeq ($(DEBUG),1)
	CFLAGS += -ggdb
endif

all: mikelibc connmon_client connmon_server

help:
	@echo "Targets: help all mikelibc connmon_client connmon_server"
	@echo "CFLAGS is $(CFLAGS)"
	@echo "LDFLAGS is $(LDFLAGS)"
	@echo "LIBS is $(LIBS)"

mikelibc:
	cd ../mikelibc&& make DEBUG=$(DEBUG)

connmon_client: $(COBJS)
	$(CC) -o connmon_client $(LDFLAGS) $(COBJS) $(LIBS)

connmon_server: $(SOBJS)
	$(CC) -o connmon_server $(LDFLAGS) $(SOBJS) $(LIBS)

connmon_client.o: connmon_client.c ../mikelibc/mnet.h ../mikelibc/libmike.a
	$(CC) $(CFLAGS) -c connmon_client.c

connmon_server.o: connmon_server.c ../mikelibc/mnet.h ../mikelibc/libmike.a
	$(CC) $(CFLAGS) -c connmon_server.c

clean:
	rm -f *.o connmon_client connmon_server make.err
	(cd ../mikelibc && make clean)
