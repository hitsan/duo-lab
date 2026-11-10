WARN := -Wall -Wextra -Werror
HOST_CFLAGS := -std=c11 -Og -g $(WARN)
INC := -isystem third_party

all: sobel-host sobel

test: test_sobel
	./test_sobel

test_sobel: test_sobel.c sobel.c sobel.h
	gcc $(HOST_CFLAGS) -o $@ test_sobel.c sobel.c

sobel-host: main.c sobel.c sobel.h
	gcc $(HOST_CFLAGS) $(INC) -o $@ main.c sobel.c -lm

sobel: main.c sobel.c sobel.h
	$(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ main.c sobel.c $(LDFLAGS) -lm

run: sobel-host
	$(if $(IMG),,$(error usage: make run IMG=path/to/image.png))
	./sobel-host $(IMG) out.png

clean:
	rm -f test_sobel sobel-host sobel out.png

.PHONY: all test run clean
