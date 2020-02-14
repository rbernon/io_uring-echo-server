CCFLAGS ?= -g -Wall -O3 -D_GNU_SOURCE -luring -march=native -DNDEBUG

all_targets = io_uring_echo_server link_recv link_read epoll_echo_server

all: $(all_targets)

clean:
	rm -f $(all_targets)

io_uring_echo_server: io_uring_echo_server.c init_server_socket.c global.h
	$(CC) io_uring_echo_server.c init_server_socket.c -o io_uring_echo_server $(CCFLAGS)

link_recv: link_recv.c init_server_socket.c global.h
	$(CC) link_recv.c init_server_socket.c -o link_recv $(CCFLAGS)

link_read: link_read.c init_server_socket.c global.h
	$(CC) link_read.c init_server_socket.c -o link_read $(CCFLAGS)

epoll_echo_server: epoll_echo_server.c init_server_socket.c global.h
	$(CC) epoll_echo_server.c init_server_socket.c -o epoll_echo_server $(CCFLAGS)
