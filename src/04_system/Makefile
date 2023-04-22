objects = main

TOOLCHAIN_PATH=/buildroot/output/host/usr/bin/
TOOLCHAIN=$(TOOLCHAIN_PATH)aarch64-linux-
CFLAGS+=-Wall -Wextra -g -O0 -std=gnu11
CFLAGS+=-mcpu=cortex-a53 -funwind-tables

CC=$(TOOLCHAIN)gcc


all: $(objects)

$(objects): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f *.txt
	rm -f *~
	rm $(objects)
