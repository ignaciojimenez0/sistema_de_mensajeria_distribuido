CC = gcc

CFLAGS = -g -fPIC -D_REENTRANT -I/usr/include/tirpc
LDFLAGS = -L. -Wl,-rpath,.
LDLIBS = -lpthread -ltirpc

SERVER_RPC = logs_server
SERVER_P1 = server
LIB_PROXY = libproxylogs.so

RPC_INTERFACE = servidor_logs.x
RPC_FILES = servidor_logs.h servidor_logs_xdr.c servidor_logs_clnt.c servidor_logs_svc.c

OBJS_RPC_SVC = servidor_logs_xdr.o servidor_logs_svc.o servidor_logs_server.o
OBJS_PROXY = servidor_logs_proxy.o servidor_logs_clnt.o servidor_logs_xdr.o
OBJS_SERVER_P1 = server.o

all: $(SERVER_RPC) $(LIB_PROXY) $(SERVER_P1)

$(RPC_FILES): $(RPC_INTERFACE)
	rpcgen -MN $(RPC_INTERFACE)

$(LIB_PROXY): $(OBJS_PROXY)
	$(CC) -shared -o $@ $(OBJS_PROXY) $(LDLIBS)

$(OBJS_RPC_SVC) $(OBJS_PROXY): $(RPC_FILES)

$(SERVER_RPC): $(OBJS_RPC_SVC)
	$(CC) $(CFLAGS) -o $(SERVER_RPC) $(OBJS_RPC_SVC) $(LDLIBS)

$(SERVER_P1): $(OBJS_SERVER_P1) $(LIB_PROXY)
	$(CC) $(CFLAGS) -o $(SERVER_P1) $(OBJS_SERVER_P1) $(LDFLAGS) -lproxylogs $(LDLIBS)

clean:
	rm -f *.o *.so $(SERVER_RPC) $(SERVER_P1) $(RPC_FILES)