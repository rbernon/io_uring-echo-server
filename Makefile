CCFLAGS ?= -g -Wall -O3 -D_GNU_SOURCE -luring -march=native -lpthread

all_targets = io_uring_echo_server epoll_echo_server io_uring_ipc_server epoll_ipc_server

all: $(all_targets)

clean:
	rm -f $(all_targets)

io_uring_echo_server: io_uring_echo_server.c init_server_socket.c global.h
	$(CC) io_uring_echo_server.c init_server_socket.c -o io_uring_echo_server $(CCFLAGS)

epoll_echo_server: epoll_echo_server.c init_server_socket.c global.h
	$(CC) epoll_echo_server.c init_server_socket.c -o epoll_echo_server $(CCFLAGS)

io_uring_ipc_server: io_uring_ipc_server.c ipc_client.c global.h
	$(CC) io_uring_ipc_server.c ipc_client.c -o io_uring_ipc_server $(CCFLAGS)

epoll_ipc_server: epoll_ipc_server.c ipc_client.c global.h
	$(CC) epoll_ipc_server.c ipc_client.c -o epoll_ipc_server $(CCFLAGS)
