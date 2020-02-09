#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>

#include <liburing.h>

#include "global.h"

#define MAX_CONNECTIONS 1024

enum
{
	ACCEPT,
	POLL_ACCEPT,
	POLL_READ,
	READ,
	WRITE,
};

struct conn_info
{
	__u32 fd;
	__u32 type;
};

typedef char buf_type[MAX_CONNECTIONS][MAX_MESSAGE_LEN];

static void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, int flags) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	struct conn_info conn_i = {
		.fd = fd,
		.type = ACCEPT,
	};

	io_uring_prep_accept(sqe, fd, client_addr, client_len, flags);
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
#if !USE_RECV_SEND
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
#endif
}

#if USE_POLL
static void add_poll(struct io_uring *ring, int fd, int poll_mask, int type) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	struct conn_info conn_i = {
		.fd = fd,
		.type = type,
	};

	io_uring_prep_poll_add(sqe, fd, poll_mask);
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
#if !USE_RECV_SEND
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
#endif
}
#endif

static void add_socket_read(struct io_uring *ring, int fd, size_t size, buf_type *bufs) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	struct conn_info conn_i = {
		.fd = fd,
		.type = READ,
	};

#if USE_RECV_SEND
	io_uring_prep_recv(sqe, fd, (*bufs)[fd], size, MSG_NOSIGNAL);
#else
	io_uring_prep_read_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
#endif

	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static void add_socket_write(struct io_uring *ring, int fd, size_t size, buf_type *bufs) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	struct conn_info conn_i = {
		.fd = fd,
		.type = WRITE,
	};

#if USE_RECV_SEND
	io_uring_prep_send(sqe, fd, (*bufs)[fd], size, MSG_NOSIGNAL);
#else
	io_uring_prep_write_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
#endif

	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Please give a port number: ./io_uring_echo_server [port]\n");
		return 1;
	}

	int portno = strtol(argv[1], NULL, 10);
	int sock_listen_fd = init_socket(portno);
	printf("io_uring echo server listening for connections on port: %d\n", portno);


	struct io_uring ring;
	int ret = io_uring_queue_init(BACKLOG, &ring, 0);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return -1;
	}

	buf_type *bufs = (buf_type *)malloc(sizeof(*bufs));

#if !USE_RECV_SEND
	{
		int fds[MAX_CONNECTIONS];
		memset(fds, -1, sizeof(fds));
		fds[sock_listen_fd] = sock_listen_fd;
		io_uring_register_files(&ring, fds, MAX_CONNECTIONS);

		struct iovec iovecs[MAX_CONNECTIONS];
		for (int i = 0; i < MAX_CONNECTIONS; ++i) {
			iovecs[i] = (struct iovec) {
				.iov_base = (*bufs)[i],
				.iov_len = MAX_MESSAGE_LEN,
			};
		}
		io_uring_register_buffers(&ring, iovecs, MAX_CONNECTIONS);
	}
#endif

	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
#if USE_POLL
	add_poll(&ring, sock_listen_fd, POLLIN, POLL_ACCEPT);
#else
	add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
#endif

	while (1) {
		io_uring_submit(&ring);

		struct io_uring_cqe *cqe;
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
			return -1;
		}

		struct conn_info conn_i;
		memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));
		int result = cqe->res;
		io_uring_cqe_seen(&ring, cqe);

		switch (conn_i.type) {
		case ACCEPT:
#if !USE_RECV_SEND
			io_uring_register_files_update(&ring, result, &result, 1);
#endif
#if USE_POLL
			add_poll(&ring, result, POLLIN, POLL_READ);
			add_poll(&ring, sock_listen_fd, POLLIN, POLL_ACCEPT);
#else
			add_socket_read(&ring, result, MAX_MESSAGE_LEN, bufs);
			add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
#endif
			break;

#if USE_POLL
		case POLL_ACCEPT:
			add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
			break;

		case POLL_READ:
			add_socket_read(&ring, conn_i.fd, MAX_MESSAGE_LEN, bufs);
			break;
#endif

		case READ:
			if (__builtin_expect(result <= 0, 0)) {
				shutdown(conn_i.fd, SHUT_RDWR);
			} else {
				add_socket_write(&ring, conn_i.fd, result, bufs);
			}
			break;

		case WRITE:
#if USE_POLL
			add_poll(&ring, conn_i.fd, POLLIN, POLL_READ);
#else
			add_socket_read(&ring, conn_i.fd, MAX_MESSAGE_LEN, bufs);
#endif
			break;
		}
	}

	close(sock_listen_fd);
	free(bufs);
}
