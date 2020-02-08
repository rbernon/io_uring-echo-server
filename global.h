/* adjust these macros to benchmark various operations */
#define USE_POLL 0
#define USE_RECV_SEND 1
#define USE_UNVEC_OP 0

#define BACKLOG 128
#define MAX_MESSAGE_LEN 1024

int init_socket(int portno, int type);
