#ifndef _GNU_SOURCE
#	define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>

#include <liburing.h>

#include "global.h"

enum { NONE, READ, WRITE };

struct iou_op {
	__u32 fd;
	__u32 type;
};

typedef char buf_type[MAX_CONNECTIONS][MAX_MESSAGE_LEN];

static struct io_uring ring;

static void add_read_sqe(int fd, size_t size, buf_type *bufs, unsigned flags) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	struct iou_op op = {
		.fd = fd,
		.type = READ,
	};

	io_uring_prep_read_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | flags);
	memcpy(&sqe->user_data, &op, sizeof(op));
}

#if !USE_WRITE_READ
static void add_write_sqe(int fd, size_t size, buf_type *bufs, unsigned flags) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	struct iou_op op = {
		.fd = fd,
		.type = WRITE,
	};

	io_uring_prep_write_fixed(sqe, fd, (*bufs)[fd], size, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | flags);
	memcpy(&sqe->user_data, &op, sizeof(op));
}
#else
static void add_write_read_sqes(int write_fd, size_t write, int read_fd, size_t read, buf_type *bufs) {
	struct iou_op op = {0};
	struct io_uring_sqe *sqe;
	int fd;

	op.type = NONE;
	op.fd = fd = write_fd;
	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_write_fixed(sqe, fd, (*bufs)[fd], write, 0, fd);
#if USE_SKIP_SUCCESS
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS);
#else
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_IO_LINK);
#endif
	memcpy(&sqe->user_data, &op, sizeof(op));

	op.type = READ;
	op.fd = fd = read_fd;
	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_read_fixed(sqe, fd, (*bufs)[fd], read, 0, fd);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
	memcpy(&sqe->user_data, &op, sizeof(op));
}
#endif

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Please give a thread count: %s [count]\n", argv[0]);
		return 1;
	}

	int ret = io_uring_queue_init(BACKLOG, &ring, 0);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return -1;
	}

	int count = argc < 2 ? 1 : strtol(argv[1], NULL, 10);
	printf("io_uring echo server starting %u ipc threads\n", count);

	int write_fds[MAX_CONNECTIONS];
	memset(write_fds, -1, sizeof(write_fds));

	int read_fds[MAX_CONNECTIONS];
	memset(read_fds, -1, sizeof(read_fds));

	int fixed_fds[MAX_CONNECTIONS];
	memset(fixed_fds, -1, sizeof(fixed_fds));

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
	if (ret) perror("pthread_setaffinity_np");

	while (count--) {
		int server_fds[2];
		start_ipc_client(count, server_fds);

		assert(server_fds[1] < MAX_CONNECTIONS);
		read_fds[server_fds[1]] = server_fds[0];
		assert(server_fds[0] < MAX_CONNECTIONS);
		write_fds[server_fds[0]] = server_fds[1];

		fixed_fds[server_fds[0]] = server_fds[0];
		fixed_fds[server_fds[1]] = server_fds[1];
	}

	buf_type *bufs = (buf_type *)malloc(sizeof(*bufs));
	io_uring_register_files(&ring, fixed_fds, MAX_CONNECTIONS);

	struct iovec iovecs[MAX_CONNECTIONS];
	for (int i = 0; i < MAX_CONNECTIONS; ++i) {
		iovecs[i] = (struct iovec) {
			.iov_base = (*bufs)[i],
			.iov_len = MAX_MESSAGE_LEN,
		};
	}
	io_uring_register_buffers(&ring, iovecs, MAX_CONNECTIONS);

	for (int fd = 0; fd < MAX_CONNECTIONS; ++fd)
	{
		if (write_fds[fd] < 0) continue;
		add_read_sqe(fd, MAX_MESSAGE_LEN, bufs, 0);
	}

	io_uring_submit(&ring);
	while (1) {
		unsigned int head, cqe_count = 0, sqe_count = 0;
		struct io_uring_cqe *cqe;
		int ret = io_uring_submit_and_wait(&ring, 1);
		assert(ret >= 0 && ret <= MAX_CONNECTIONS * 2);

		io_uring_for_each_cqe(&ring, head, cqe) {
			struct iou_op op;
			memcpy(&op, &cqe->user_data, sizeof(op));
			assert(cqe->res == MAX_MESSAGE_LEN);

			switch (op.type) {
			case READ:
				*((size_t *)(*bufs)[write_fds[op.fd]] + 1) = *((size_t *)(*bufs)[op.fd] + 0);
#if USE_WRITE_READ
				add_write_read_sqes(write_fds[op.fd], MAX_MESSAGE_LEN, op.fd, MAX_MESSAGE_LEN, bufs);
#else
				add_write_sqe(write_fds[op.fd], MAX_MESSAGE_LEN, bufs, 0);
#endif
				sqe_count++;
				break;

#if !USE_WRITE_READ
			case WRITE:
				assert(0);
				add_read_sqe(read_fds[op.fd], MAX_MESSAGE_LEN, bufs, 0);
				sqe_count++;
				break;
#endif

			case NONE:
#if USE_SKIP_SUCCESS
				assert(0);
#endif
				break;
			}

			cqe_count++;
		}
		io_uring_cq_advance(&ring, cqe_count);
	}

	free(bufs);
}
