WARN := -Wall -Wextra -Werror
HOST_BASE := -std=c11 -Og -g
HOST_CFLAGS := $(HOST_BASE) $(WARN)
INC := -isystem third_party
check-duo = $(if $(TOOLCHAIN_PREFIX),,$(error run 'source /PATH/TO/duo-examples/envsetup.sh' first))

host: sobel-host

duo: sobel

test: test_sobel
	./test_sobel

test_sobel: test_sobel.c sobel.c sobel.h
	gcc $(HOST_CFLAGS) -o $@ test_sobel.c sobel.c

stb_impl.host.o: stb_impl.c
	gcc $(HOST_BASE) $(INC) -c -o $@ $<

stb_impl.duo.o: stb_impl.c
	$(check-duo)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

sobel-host: main.c sobel.c sobel.h stb_impl.host.o
	gcc $(HOST_CFLAGS) $(INC) -o $@ main.c sobel.c stb_impl.host.o -lm

sobel: main.c sobel.c sobel.h stb_impl.duo.o
	$(check-duo)
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ main.c sobel.c stb_impl.duo.o $(LDFLAGS) -lm

run: sobel-host
	$(if $(IMG),,$(error usage: make run IMG=path/to/image.png))
	./sobel-host $(IMG) out.png

clean:
	rm -f test_sobel sobel-host sobel out.png *.o

.PHONY: host duo test run clean
