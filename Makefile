CCFLAGS ?= -g -Wall -O3 -D_GNU_SOURCE -luring -march=native

all_targets = io_uring_echo_server epoll_echo_server

all: $(all_targets)

clean:
	rm -f $(all_targets)

io_uring_echo_server: io_uring_echo_server.c init_server_socket.c
	$(CC) io_uring_echo_server.c init_server_socket.c -o ./io_uring_echo_server $(CCFLAGS)

epoll_echo_server: epoll_echo_server.c init_server_socket.c
	$(CC) epoll_echo_server.c init_server_socket.c -o epoll_echo_server $(CCFLAGS)
