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

enum { ACCEPT, POLL, READ, WRITE };

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

	io_uring_prep_accept(sqe, fd, client_addr, client_len, flags);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static void add_poll(int fd, int poll_mask) {
	struct io_uring_sqe *sqe = get_sqe_safe();
	struct conn_info conn_i = {
		.fd = fd,
		.type = POLL,
	};

	io_uring_prep_poll_add(sqe, fd, poll_mask);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_IO_LINK);
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static void add_socket_read(int fd, size_t size, buf_type *bufs) {
	struct io_uring_sqe *sqe = get_sqe_safe();
	struct conn_info conn_i = {
		.fd = fd,
		.type = READ,
	};

	io_uring_prep_read_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_IO_LINK);
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static void add_socket_write(int fd, size_t size, buf_type *bufs) {
	struct io_uring_sqe *sqe = get_sqe_safe();
	struct conn_info conn_i = {
		.fd = fd,
		.type = WRITE,
	};

	io_uring_prep_write_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
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

	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	add_accept(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);

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
				io_uring_register_files_update(&ring, result, &result, 1);
				add_accept(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
				add_poll(result, POLLIN);
				add_socket_read(result, MAX_MESSAGE_LEN, bufs);
				add_socket_write(result, MAX_MESSAGE_LEN, bufs);
				break;

			case POLL:
				// Do nothing
				break;

			case READ:
				// A short read happened, op write will be canceled
				if (__builtin_expect(result <= 0, 0)) {
					shutdown(conn_i.fd, SHUT_RDWR);
				} else if (__builtin_expect(result < MAX_MESSAGE_LEN, 0)) {
					add_socket_write(conn_i.fd, result, bufs);
				}
				break;

			case WRITE:
				if (__builtin_expect(result >= 0, 1)) {
					add_poll(conn_i.fd, POLLIN);
					add_socket_read(conn_i.fd, MAX_MESSAGE_LEN, bufs);
					add_socket_write(conn_i.fd, MAX_MESSAGE_LEN, bufs);
				}
				break;
			}
		}

		io_uring_cq_advance(&ring, cqe_count);
		cqe_count = 0;
	}


	close(sock_listen_fd);
	free(bufs);
}
