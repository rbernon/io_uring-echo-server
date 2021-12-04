#ifndef _GNU_SOURCE
#	define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>

#include <liburing.h>

#include "global.h"

enum { ACCEPT, POLL_ACCEPT, POLL_READ, READ, WRITE };

struct conn_info {
	__u32 fd;
	__u32 type;
};

typedef char buf_type[MAX_CONNECTIONS][MAX_MESSAGE_LEN];

static struct io_uring ring;
static unsigned cqe_count = 0;

static struct io_uring_sqe* get_sqe_safe() {
	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	if (__builtin_expect(!!sqe, 1)) {
		return sqe;
	} else {
		io_uring_cq_advance(&ring, cqe_count);
		cqe_count = 0;
		io_uring_submit(&ring);
		return io_uring_get_sqe(&ring);
	}
}

static void add_accept(int fd, struct sockaddr *client_addr, socklen_t *client_len, int flags) {
	struct io_uring_sqe *sqe = get_sqe_safe();
	struct conn_info conn_i = {
		.fd = fd,
		.type = ACCEPT,
	};

	io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
#if USE_RECV_SEND
	io_uring_sqe_set_flags(sqe, flags);
#else
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | flags);
#endif
}

#if USE_POLL
static void add_poll(int fd, int poll_mask, int type) {
	struct io_uring_sqe *sqe = get_sqe_safe();
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

static void add_socket_read(int fd, size_t size, buf_type *bufs, unsigned flags) {
	struct io_uring_sqe *sqe = get_sqe_safe();
	struct conn_info conn_i = {
		.fd = fd,
		.type = READ,
	};

#if USE_RECV_SEND
	io_uring_prep_recv(sqe, fd, (*bufs)[fd], size, MSG_NOSIGNAL);
	io_uring_sqe_set_flags(sqe, flags);
#else
	io_uring_prep_read_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | flags);
#endif

	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static void add_socket_write(int fd, size_t size, buf_type *bufs, unsigned flags) {
	struct io_uring_sqe *sqe = get_sqe_safe();
	struct conn_info conn_i = {
		.fd = fd,
		.type = WRITE,
	};

#if USE_RECV_SEND
	io_uring_prep_send(sqe, fd, (*bufs)[fd], size, MSG_NOSIGNAL);
	io_uring_sqe_set_flags(sqe, flags);
#else
	io_uring_prep_write_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | flags);
#endif

	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Please give a port number: %s [port]\n", argv[0]);
		return 1;
	}

	int portno = strtol(argv[1], NULL, 10);
	int sock_listen_fd = init_socket(portno);
	if (sock_listen_fd < 0) return -1;
	printf("io_uring echo server listening for connections on port: %d\n", portno);


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
	add_poll(sock_listen_fd, POLLIN, POLL_ACCEPT);
#else
	add_accept(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, IOSQE_ASYNC);
#endif

	while (1) {
		io_uring_submit_and_wait(&ring, 1);

		struct io_uring_cqe *cqe;
		unsigned head;

		io_uring_for_each_cqe(&ring, head, cqe) {
			++cqe_count;

			struct conn_info conn_i;
			memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));
			int result = cqe->res;

			switch (conn_i.type) {
			case ACCEPT:
#if !USE_RECV_SEND
				io_uring_register_files_update(&ring, result, &result, 1);
#endif
#if USE_POLL
				add_poll(result, POLLIN, POLL_READ);
				add_poll(sock_listen_fd, POLLIN, POLL_ACCEPT);
#else
				add_socket_read(result, MAX_MESSAGE_LEN, bufs, IOSQE_ASYNC);
				add_accept(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, IOSQE_ASYNC);
#endif
				break;

#if USE_POLL
			case POLL_ACCEPT:
				add_accept(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
				break;

			case POLL_READ:
				add_socket_read(conn_i.fd, MAX_MESSAGE_LEN, bufs, 0);
				break;
#endif

			case READ:
				if (__builtin_expect(result <= 0, 0)) {
					shutdown(conn_i.fd, SHUT_RDWR);
				} else {
					add_socket_write(conn_i.fd, result, bufs, 0);
				}
				break;

			case WRITE:
#if USE_POLL
				add_poll(conn_i.fd, POLLIN, POLL_READ);
#else
				add_socket_read(conn_i.fd, MAX_MESSAGE_LEN, bufs, IOSQE_ASYNC);
#endif
				break;
			}
		}

		io_uring_cq_advance(&ring, cqe_count);
		cqe_count = 0;
	}


	close(sock_listen_fd);
	free(bufs);
}
