# direnv sets CC/CFLAGS for the Duo cross toolchain; tests run on the host
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Og -g

test: test_sobel
	./test_sobel

test_sobel: test_sobel.c sobel.c sobel.h
	$(CC) $(CFLAGS) -o $@ test_sobel.c sobel.c

clean:
	rm -f test_sobel

.PHONY: test clean
