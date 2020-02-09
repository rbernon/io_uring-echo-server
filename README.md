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

USE_POLL | RECV_SEND | EV MODEL |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |     -:
1        | 0         | io_uring |  POLL-READ_F-WRITE_F |  138703 |  138243 |  106670 |  138243 | 101.8%
1        | 1         | io_uring |       POLL-RECV-SEND |  130602 |  118856 |  121214 |  121214 |  89.3%
0        | 0         | io_uring |       READ_F-WRITE_F |  100700 |   98616 |  100765 |  100700 |  74.2%
0        | 1         | io_uring |            SEND-RECV |   99820 |  101221 |   97617 |   99820 |  73.5%
N/A      | 0         | epoll    |      POLL-READ-WRITE |  134392 |  133448 |  129621 |  133448 |  98.3%
N/A      | 1         | epoll    |       POLL-RECV-SEND |  146344 |  135762 |  132244 |  135762 | 100.0%

### command: `cargo run --release -- -c 200`

USE_POLL | RECV_SEND | EV MODEL |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |     -:
1        | 0         | io_uring |  POLL-READ_F-WRITE_F |  119847 |  125897 |  125195 |  125195 |  95.3%
1        | 1         | io_uring |       POLL-RECV-SEND |  110159 |  116585 |  110357 |  110357 |  84.0%
0        | 0         | io_uring |       READ_F-WRITE_F |   94746 |   80826 |   86860 |   86860 |  66.1%
0        | 1         | io_uring |            SEND-RECV |   80476 |   69946 |   67670 |   69946 |  53.2%
N/A      | 0         | epoll    |      POLL-READ-WRITE |  139460 |  138487 |  137974 |  138487 | 105.4%
N/A      | 1         | epoll    |       POLL-RECV-SEND |  127384 |  135416 |  131435 |  131435 | 100.0%

### command: `cargo run --release -- -c 1`

USE_POLL | RECV_SEND | EV MODEL |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |     -:
1        | 0         | io_uring |  POLL-READ_F-WRITE_F |   15623 |   17776 |   18675 |   17776 | 118.9%
1        | 1         | io_uring |       POLL-RECV-SEND |   19056 |   14811 |   14145 |   14811 |  99.1%
0        | 0         | io_uring |       READ_F-WRITE_F |   40785 |   38171 |   43963 |   40785 | 272.9%
0        | 1         | io_uring |            SEND-RECV |   20377 |   25152 |   15089 |   20377 | 136.3%
N/A      | 0         | epoll    |      POLL-READ-WRITE |   16105 |   14227 |   15672 |   15672 | 104.9%
N/A      | 1         | epoll    |       POLL-RECV-SEND |   18596 |   14076 |   14946 |   14946 | 100.0%

## Summary

For servers designed for high concurrency:

1. io_uring won't improve much performance over epoll, at least for networking
1. Always `POLL` before `READ`/`RECV`
