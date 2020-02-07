#include "liburing.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/poll.h>

#ifndef POLL_BEFORE_READ
#   define POLL_BEFORE_READ 0
#endif
#ifndef USE_RECVMSG_SENDMSG
#   define USE_RECVMSG_SENDMSG 0
#endif
#ifndef USE_VECTORED_OP
#   define USE_VECTORED_OP 1
#endif

#define MAX_CONNECTIONS 1024
#define BACKLOG 128
#define MAX_MESSAGE_LEN 1024

void add_accept(struct io_uring* ring, int sockfd, struct sockaddr* client_addr, socklen_t* client_len, int flags);
void add_poll(struct io_uring* ring, int fd, int type);
void add_socket_read(struct io_uring* ring, int fd, size_t size);
void add_socket_write(struct io_uring* ring, int fd, size_t size);

enum {
    ACCEPT,
    POLL,
    READ,
    WRITE,
};

typedef struct conn_info
{
    int fd;
    int type;
} conn_info;

conn_info conns[MAX_CONNECTIONS];
char bufs[MAX_CONNECTIONS][MAX_MESSAGE_LEN];

#if USE_VECTORED_OP
struct iovec iovecs[MAX_CONNECTIONS];
struct msghdr msgs[MAX_CONNECTIONS];
#endif

int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        printf("Please give a port number: ./io_uring_echo_server [port]\n");
        exit(0);
    }

    // some variables we need
    int portno = strtol(argv[1], NULL, 10);
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

#if USE_VECTORED_OP
    // create conn_info structs
    for (int i = 0; i < MAX_CONNECTIONS - 1; i++)
    {
        // global variables are initialized to zero by default
        iovecs[i].iov_base = bufs[i];
        msgs[i].msg_iov = &iovecs[i];
        msgs[i].msg_iovlen = 1;
    }
#endif

    // setup socket
    int sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    const int val = 1;
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;


    // bind and listen
    if (bind(sock_listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Error binding socket..\n");
        exit(1);
    }
    if (listen(sock_listen_fd, BACKLOG) < 0)
    {
        perror("Error listening..\n");
        exit(1);
    }
    printf("io_uring echo server listening for connections on port: %d\n", portno);


    // initialize io_uring
    struct io_uring ring;
    io_uring_queue_init(BACKLOG, &ring, 0);


    // add first io_uring accept sqe, to begin accepting new connections
    add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_len, 0);

    while (1)
    {
        struct io_uring_cqe *cqe;
        int ret;

        // tell kernel we have put a sqe on the submission ring
        io_uring_submit(&ring);

        // wait for new cqe to become available
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret != 0)
        {
            perror("Error io_uring_wait_cqe\n");
            exit(1);
        }

        conn_info *user_data = (struct conn_info *)io_uring_cqe_get_data(cqe);
        int type = user_data->type;

        switch (type)
        {
        case ACCEPT:
        {
            int sock_conn_fd = cqe->res;
            io_uring_cqe_seen(&ring, cqe);

#if POLL_BEFORE_READ
            // add poll sqe for newly connected socket
            add_poll(&ring, sock_conn_fd, POLLIN);
#else
            add_socket_read(&ring, sock_conn_fd, MAX_MESSAGE_LEN);
#endif

            // continue accepting other connections
            add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_len, 0);
            break;
        }

        case POLL:
        {
            io_uring_cqe_seen(&ring, cqe);

            // bytes available on connected socket, add read sqe
            add_socket_read(&ring, user_data->fd, MAX_MESSAGE_LEN);
            break;
        }

        case READ:
        {
            int bytes_read = cqe->res;
            if (bytes_read <= 0)
            {
                // no bytes available on socket, client must be disconnected
                io_uring_cqe_seen(&ring, cqe);
                shutdown(user_data->fd, SHUT_RDWR);
            }
            else
            {
                // bytes have been read into iovec, add write to socket sqe
                io_uring_cqe_seen(&ring, cqe);
                add_socket_write(&ring, user_data->fd, bytes_read);
            }
            break;
        }

        case WRITE:
        {
            io_uring_cqe_seen(&ring, cqe);

#if POLL_BEFORE_READ
            // write to socket completed, re-add poll sqe
            add_poll(&ring, user_data->fd, POLLIN);
#else
            add_socket_read(&ring, user_data->fd, MAX_MESSAGE_LEN);
#endif

            break;
        }
        }
    }

}

void add_accept(struct io_uring* ring, int fd, struct sockaddr* client_addr, socklen_t* client_len, int flags)
{

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_len, flags);

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = ACCEPT;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_poll(struct io_uring* ring, int fd, int type)
{

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_poll_add(sqe, fd, type);

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = POLL;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_socket_read(struct io_uring* ring, int fd, size_t size) {

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
#if USE_VECTORED_OP
    iovecs[fd].iov_len = size;
#   if USE_RECVMSG_SENDMSG
    io_uring_prep_recvmsg(sqe, fd, &msgs[fd], MSG_NOSIGNAL);
#   else
    io_uring_prep_readv(sqe, fd, &iovecs[fd], 1, 0);
#   endif
#else
#   if USE_RECVMSG_SENDMSG
    io_uring_prep_recv(sqe, fd, bufs[fd], size, MSG_NOSIGNAL);
#   else
    io_uring_prep_read(sqe, fd, bufs[fd], size, 0);
#   endif
#endif

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = READ;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_socket_write(struct io_uring* ring, int fd, size_t size) {

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
#if USE_VECTORED_OP
    iovecs[fd].iov_len = size;
#   if USE_RECVMSG_SENDMSG
    io_uring_prep_sendmsg(sqe, fd, &msgs[fd], MSG_NOSIGNAL);
#   else
    io_uring_prep_writev(sqe, fd, &iovecs[fd], 1, 0);
#   endif
#else
#   if USE_RECVMSG_SENDMSG
    io_uring_prep_send(sqe, fd, bufs[fd], size, MSG_NOSIGNAL);
#   else
    io_uring_prep_write(sqe, fd, bufs[fd], size, 0);
#   endif
#endif

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = WRITE;

    io_uring_sqe_set_data(sqe, conn_i);
}
