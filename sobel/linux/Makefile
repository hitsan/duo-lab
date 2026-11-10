# `make` builds for the Duo with CC/CFLAGS/LDFLAGS from direnv; `make run IMG=in.png` builds and runs on the host
SRCS := main.c ../common/sobel.c
DEPS := $(SRCS) ../common/sobel.h
INC := -I../common -isystem third_party
WARN := -Wall -Wextra -Werror

sobel: $(DEPS)
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ $(SRCS) $(LDFLAGS) -lm

sobel-host: $(DEPS)
	gcc -std=c11 -O2 $(WARN) $(INC) -o $@ $(SRCS) -lm

run: sobel-host
	$(if $(IMG),,$(error usage: make run IMG=path/to/image.png))
	./sobel-host $(IMG) out.png

clean:
	rm -f sobel sobel-host out.png

.PHONY: run clean
