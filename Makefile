all:
	clang -Wall -O3 -D_GNU_SOURCE io_uring_echo_server.c -o ./io_uring_echo_server -luring -march=native
