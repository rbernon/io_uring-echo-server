#ifndef _GNU_SOURCE
#	define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#include "global.h"

#define MAX_EVENTS BACKLOG

static void add_poll(int epfd, int fd, int poll_mask) {
	struct epoll_event ev = {
		.events = poll_mask,
		.data = { .fd = fd },
	};

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl");
		exit(-1);
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Please give a thread count: %s [count]\n", argv[0]);
		return 1;
	}

	int epfd = epoll_create(MAX_EVENTS);
	if (epfd < 0) {
		perror("epoll_create");
		return -1;
	}

	int count = argc < 2 ? 1 : strtol(argv[1], NULL, 10);
	printf("epoll echo server starting %u ipc threads\n", count);

	int write_fds[MAX_CONNECTIONS];
	memset(write_fds, -1, sizeof(write_fds));

	while (count--) {
		int server_fds[2];
		start_ipc_client(count, server_fds);

		assert(server_fds[0] < MAX_CONNECTIONS);
		write_fds[server_fds[0]] = server_fds[1];
		add_poll(epfd, server_fds[0], EPOLLIN);
	}

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
	if (ret) perror("pthread_setaffinity_np");

	while (1) {
		struct epoll_event events[MAX_EVENTS];
		int new_events = epoll_wait(epfd, events, MAX_EVENTS, -1);
		if (new_events < 0) {
			perror("epoll_wait");
			return -1;
		}

		char buffer[MAX_MESSAGE_LEN];
		for (int i = 0; i < new_events; ++i) {
			int fd = events[i].data.fd;

			read(fd, buffer, MAX_MESSAGE_LEN);
			*((size_t *)buffer + 1) = *((size_t *)buffer + 0);
			write(write_fds[fd], buffer, MAX_MESSAGE_LEN);
		}
	}
}
