#pragma once

/* adjust these macros to benchmark various operations */
#define USE_POLL 1
#define USE_SPLICE 0
#define USE_RECV_SEND 1
#define USE_WRITE_READ 1

#define BACKLOG 128
#define MAX_MESSAGE_LEN 64
#define MAX_CONNECTIONS 512

int init_socket(int portno);
void start_ipc_client(int num, int *server_fds);
