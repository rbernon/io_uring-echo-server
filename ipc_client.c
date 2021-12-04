#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include <unistd.h>

#include <pthread.h>

#include "global.h"

static void *ipc_client_thread(void *arg)
{
	static volatile int count = 0;
	size_t ns, ns_per_s = 1000000000;
	char buffer[MAX_MESSAGE_LEN];
	struct timespec ts0, ts1;
	int i, *fds = arg;

	sprintf(buffer, "ipc client %u", count++);
	pthread_setname_np(pthread_self(), buffer);

	while (1)
	{
		clock_gettime(CLOCK_MONOTONIC, &ts0);
		for (i = 0; i < 100000; ++i)
		{
			*((size_t *)buffer + 0) = ts0.tv_nsec + i;
			write(fds[1], buffer, MAX_MESSAGE_LEN);
			read(fds[0], buffer, MAX_MESSAGE_LEN);
			assert(*((size_t *)buffer + 1) == ts0.tv_nsec + i);
		}
		clock_gettime(CLOCK_MONOTONIC, &ts1);
		ns = ts1.tv_nsec - ts0.tv_nsec + ns_per_s * (ts1.tv_sec - ts0.tv_sec);
		fprintf(stderr, "Speed: %zu requests/s, %zu ns/request\n", i * ns_per_s / ns, ns / i);
	}

	free(fds);
	return NULL;
}

void start_ipc_client(int num, int *server_fds)
{
	int tmp, *client_fds = calloc(2, sizeof(int));
	pthread_t thread;

	if (pipe(server_fds) < 0 || pipe(client_fds) < 0) {
		perror("pipe");
		abort();
	}

	tmp = client_fds[0];
	client_fds[0] = server_fds[0];
	server_fds[0] = tmp;

	pthread_create(&thread, NULL, &ipc_client_thread, client_fds);

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(num + 2, &cpuset);
	int ret = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
	if (ret) perror("pthread_setaffinity_np");
}
