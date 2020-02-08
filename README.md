# io_uring bare minimum echo server

* uses an event loop created with io_uring
* uses liburing git HEAD
* Linux 5.5 or higHer needed. (For IORING_OP_ACCEPT)

## Install and run

`make`

`./io_uring_echo_server [port_number]`

## Why fork a new repo

Because the original repo [tries to improve performance by not doing the right thing](https://github.com/frevib/io_uring-echo-server/commit/99409e6dcddc1815e35b78a27bb630b20f0eecd7#diff-3bd7f4165bd0d48db65a29389c0eb6c3R202)

## compare with epoll echo server
https://github.com/frevib/epoll-echo-server


## Benchmarks

* VMWare Ubuntu Focal Fossa 20.04 (development branch)
* Linux carter-virtual-machine 5.5.0-999-generic #202002070204 SMP Fri Feb 7 02:09:27 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux
* 4 virtual cores
* Macbook pro i7 2.5GHz/16GB
* Compiler: clang version 9.0.1-8build1
* ./io_uring_echo_server 12345

with `rust_echo_bench`: https://github.com/haraldh/rust_echo_bench  
unit: request/sec

### command: `cargo run --release -- -c 50`

USE_POLL | RECV_SEND | UNVEC_OP |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |     -:
0        | 0         | 0        |         READV-WRITEV |   96891 |   88881 |   92068 |   92068 | 100.0%
0        | 1         | 0        |      RECVMSG-SENDMSG |   97500 |   84620 |   89185 |   89185 |  96.9%
0        | 0         | 1        |           READ-WRITE |   91115 |  100020 |   90731 |   91115 |  99.0%
0        | 1         | 1        |            SEND-RECV |  102389 |   91870 |   98037 |   98037 | 106.5%
1        | 0         | 0        |    POLL-READV-WRITEV |  121915 |  128243 |  124675 |  124675 | 135.4%
1        | 1         | 0        | POLL-RECVMSG-SENDMSG |  137656 |  133897 |  140219 |  133897 | 145.4%
1        | 0         | 0        |      POLL-READ-WRITE |  120866 |  127578 |  138525 |  127578 | 139.6%
1        | 1         | 1        |       POLL-RECV-SEND |  148153 |  135844 |  121911 |  135844 | 147.5%

### command: `cargo run --release -- -c 200`

USE_POLL | RECV_SEND | UNVEC_OP |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |     -:
0        | 0         | 0        |         READV-WRITEV |   74994 |   73732 |   78733 |   73732 | 100.0%
0        | 1         | 0        |      RECVMSG-SENDMSG |   71978 |   70576 |   60764 |   70576 |  95.7%
0        | 0         | 1        |           READ-WRITE |   79649 |   86166 |   71532 |   79649 | 108.0%
0        | 1         | 1        |            SEND-RECV |   70925 |   71723 |   70954 |   70954 |  96.2%
1        | 0         | 0        |    POLL-READV-WRITEV |  118864 |  116717 |  111802 |  111802 | 151.6%
1        | 1         | 0        | POLL-RECVMSG-SENDMSG |  117015 |  116070 |  116075 |  116075 | 157.4%
1        | 0         | 0        |      POLL-READ-WRITE |  112418 |  110809 |  115982 |  112418 | 152.4%
1        | 1         | 1        |       POLL-RECV-SEND |  125556 |  116233 |  109289 |  116233 | 157.6%

### command: `cargo run --release -- -c 1`

USE_POLL | RECV_SEND | UNVEC_OP |           operations |     1st |     2nd |     3rd |     mid |   rate
:-:      | :-:       | :-:      |                   -: |      -: |      -: |      -: |      -: |     -:
0        | 0         | 0        |         READV-WRITEV |   30829 |   23976 |   30819 |   30819 | 100.0%
0        | 1         | 0        |      RECVMSG-SENDMSG |   21234 |   24287 |   20465 |   21234 |  68.9%
0        | 0         | 1        |           READ-WRITE |   46763 |   29363 |   16340 |   29363 |  95.3%
0        | 1         | 1        |            SEND-RECV |   18550 |   11415 |   16741 |   16741 |  54.3%
1        | 0         | 0        |    POLL-READV-WRITEV |   14608 |   13044 |   12942 |   13044 |  42.3%
1        | 1         | 0        | POLL-RECVMSG-SENDMSG |   16637 |   14697 |   13854 |   14697 |  47.7%
1        | 0         | 0        |      POLL-READ-WRITE |   13328 |   13445 |   15478 |   13445 |  43.6%
1        | 1         | 1        |       POLL-RECV-SEND |   21349 |   15147 |   14043 |   15147 |  49.1%

## Summary

For servers designed for high concurrency:

1. Use `POLL` before `READ`/`RECV`
1. Use `RECV`/`RECVMSG` instead of `READ`/`READV`
1. Use `READ`/`RECV` instead of `READV`/`RECVMSG`
