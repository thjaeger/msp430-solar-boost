CC=msp430-gcc
CFLAGS=-O3 -std=c99 -Wall -g -mmcu=msp430g2452

OBJS=main.o

.PHONY: all flash clean

all: main.elf

main.elf: $(OBJS)
	$(CC) $(CFLAGS) -o main.elf $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.s: %.c
	$(CC) -S $(CFLAGS) -c $<

main.hex: main.elf
	msp430-objcopy -O ihex main.elf main.hex

main.cycles: main.hex
	naken_util -disasm main.hex > main.cycles

flash:	all
	mspdebug rf2500 "prog main.elf"

clean:
	rm -fr main.elf $(OBJS)
