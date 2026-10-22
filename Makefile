CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -pthread

# Shared source files used by both the client and server.
COMMON_SRC = common/protocol.c common/util.c

# Server modules for connection handling, auth, RPC parsing/execution,
# handle validation, queueing, worker threads, generation, and cleanup.
SERVER_SRC = server/main.c \
             server/connection.c \
             server/auth.c \
             server/client_state.c \
             server/rpc_parse.c \
             server/rpc_execute.c \
             server/handles.c \
             server/threadpool.c \
             server/queue.c \
             server/generate.c \
             server/cleanup.c \
             $(COMMON_SRC)

# Client modules for connection setup, input handling, output formatting,
# and local login state.
CLIENT_SRC = client/main.c \
             client/connection.c \
             client/input.c \
             client/output.c \
             client/auth_state.c \
             $(COMMON_SRC)

# Build both assignment executables by default.
default: animate_server animate_client

# Build the server and link it with libanimate.
# Only the server uses libanimate, because the client communicates through RPCs.
animate_server: $(SERVER_SRC)
	$(CC) $(CFLAGS) $^ -I. -Ilibanimate/include -Llibanimate/lib -lanimate $(LDFLAGS) -o $@

# Build the client.
# The client does not link against libanimate.
animate_client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) $^ -I. $(LDFLAGS) -o $@

# Remove compiled binaries, object files, FIFOs, and generated output files.
clean:
	rm -f animate_server animate_client *.o server/*.o client/*.o common/*.o FIFO_* *.dat *.mp4 *.log

# Run the local test suite.
test:
	./tests/run_all_tests.sh

.PHONY: default clean test