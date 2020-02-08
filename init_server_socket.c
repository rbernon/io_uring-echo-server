#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>

#include "global.h"

int init_socket(int portno, int type) {
	int sock_listen_fd = socket(AF_INET, SOCK_STREAM | type, 0);
	if (sock_listen_fd < 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(portno),
		.sin_addr = {
			.s_addr = INADDR_ANY,
		},
	};

	if (bind(sock_listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		return -1;
	}

	if (listen(sock_listen_fd, BACKLOG) < 0) {
		perror("listen");
		return -1;
	}

	return sock_listen_fd;
}
