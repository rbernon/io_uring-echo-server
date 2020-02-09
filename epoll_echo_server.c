#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
		printf("Please give a port number: ./epoll_echo_server [port]\n");
		return 1;
	}

	int portno = strtol(argv[1], NULL, 10);
	int sock_listen_fd = init_socket(portno);
	printf("epoll echo server listening for connections on port: %d\n", portno);


	int epfd = epoll_create(MAX_EVENTS);
	if (epfd < 0) {
		perror("epoll_create");
		return -1;
	}

	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	add_poll(epfd, sock_listen_fd, EPOLLIN);

	while (1) {
		struct epoll_event events[MAX_EVENTS];
		int new_events = epoll_wait(epfd, events, MAX_EVENTS, -1);
		if (new_events < 0) {
			perror("epoll_wait");
			return -1;
		}

		for (int i = 0; i < new_events; ++i) {
			int fd = events[i].data.fd;
			if (__builtin_expect(fd == sock_listen_fd, 0)) {
				int sock_conn_fd = accept(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len);
				if (__builtin_expect(sock_conn_fd < 0, 0)) {
					perror("accept4");
					return -1;
				}
				add_poll(epfd, sock_conn_fd, EPOLLIN);
			} else {
				char buffer[MAX_MESSAGE_LEN];

#if USE_RECV_SEND
				int bytes_read = recv(fd, buffer, MAX_MESSAGE_LEN, MSG_NOSIGNAL);
#else
				int bytes_read = read(fd, buffer, MAX_MESSAGE_LEN);
#endif
				if (__builtin_expect(bytes_read <= 0, 0)) {
					epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
					shutdown(fd, SHUT_RDWR);
				} else {
#if USE_RECV_SEND
					send(fd, buffer, bytes_read, MSG_NOSIGNAL);
#else
					write(fd, buffer, bytes_read);
#endif
				}
			}
		}
	}

	close(sock_listen_fd);
}
