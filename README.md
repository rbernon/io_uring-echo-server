# io_uring bare minimum echo server

* uses an event loop created with io_uring
* uses liburing git HEAD
* Linux 5.5 or higHer needed. (For IORING_OP_ACCEPT)

## Build

`make`

## Why fork a new repo

Because the original repo [tries to improve performance by not doing the right thing](https://github.com/frevib/io_uring-echo-server/commit/99409e6dcddc1815e35b78a27bb630b20f0eecd7#diff-3bd7f4165bd0d48db65a29389c0eb6c3R202)

## Benchmarks

* VMWare Ubuntu Focal Fossa 20.04 (development branch)
* Linux carter-virtual-machine 5.5.0-999-generic #202002070204 SMP Fri Feb 7 02:09:27 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux
* 4 virtual cores
* Macbook pro i7 2.5GHz/16GB
* Compiler: clang version 9.0.1-8build1
* ./io_uring_echo_server 12345 ( for io_uring version )
* ./epoll_echo_server 12345 ( for epoll version )

with `rust_echo_bench`: https://github.com/haraldh/rust_echo_bench  
unit: request/sec

### command: `cargo run --release -- -c 50`

USE_POLL | RECV_SEND | EV MODEL |           operations |     1st |     2nd |     3rd |     mid |    rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |      -:
1        | 0         | io_uring |  POLL-READ_F-WRITE_F |  150637 |  149408 |  153421 |  150637 | 101.40%
1        | 1         | io_uring |       POLL-RECV-SEND |  158517 |  163899 |  156310 |  158517 | 106.70%
0        | 0         | io_uring |       READ_F-WRITE_F |   87740 |   83988 |   91266 |   87740 |  59.06%
0        | 1         | io_uring |            SEND-RECV |   88967 |   85187 |   80674 |   85187 |  57.34%
N/A      | 0         | epoll    |      POLL-READ-WRITE |  152407 |  148379 |  150991 |  150991 | 101.64%
N/A      | 1         | epoll    |       POLL-RECV-SEND |  148557 |  148124 |  148606 |  148557 | 100.00%
IO_LINK  | 1         | io_uring |       POLL-RECV-SEND |  105464 |  109249 |  106085 |  106085 |  71.41%
IO_LINK  | 0         | io_uring |  POLL-READ_F-WRITE_F |  108518 |  108715 |  105832 |  108518 |  73.05%

### command: `cargo run --release -- -c 200`

USE_POLL | RECV_SEND | EV MODEL |           operations |     1st |     2nd |     3rd |     mid |    rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |      -:
1        | 0         | io_uring |  POLL-READ_F-WRITE_F |  146183 |  143251 |  146514 |  146183 |  98.75%
1        | 1         | io_uring |       POLL-RECV-SEND |  150691 |  151855 |  147474 |  150691 | 101.79%
0        | 0         | io_uring |       READ_F-WRITE_F |   78388 |   76215 |   77005 |   77005 |  52.02%
0        | 1         | io_uring |            SEND-RECV |   76152 |   69351 |   76097 |   76097 |  51.40%
N/A      | 0         | epoll    |      POLL-READ-WRITE |  148472 |  145701 |  147827 |  147827 |  99.86%
N/A      | 1         | epoll    |       POLL-RECV-SEND |  155998 |  156516 |  155453 |  156516 | 100.00%

### command: `cargo run --release -- -c 1`

USE_POLL | RECV_SEND | EV MODEL |           operations |     1st |     2nd |     3rd |     mid |    rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |      -:
1        | 0         | io_uring |  POLL-READ_F-WRITE_F |   15623 |   17776 |   18675 |   17776 | 118.93%
1        | 1         | io_uring |       POLL-RECV-SEND |   19056 |   14811 |   14145 |   14811 |  99.10%
0        | 0         | io_uring |       READ_F-WRITE_F |   48117 |   37423 |   46210 |   46210 | 309.18%
0        | 1         | io_uring |            SEND-RECV |   22971 |   21146 |   20152 |   21146 | 141.48%
N/A      | 0         | epoll    |      POLL-READ-WRITE |   16105 |   14227 |   15672 |   15672 | 104.86%
N/A      | 1         | epoll    |       POLL-RECV-SEND |   18596 |   14076 |   14946 |   14946 | 100.00%
