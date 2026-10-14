CROSS ?= arm-none-eabi-
CFLAGS = -march=armv5te -mfloat-abi=soft -marm -Os -Wall -fno-stack-protector \
	 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -I../include -I.
OBJS = start.o game.o lcd.o serial.o utils.o bionic.o vsprintf.o

all: game.bin

game.bin: game.elf
	$(CROSS)objcopy -O binary $< $@
	$(CROSS)size $<

game.elf: $(OBJS) game.ld
	$(CROSS)ld --nostdlib -T game.ld -o $@ $(OBJS) ../lib/libgcc-armv5.a

%.o: %.c
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<
%.o: ../%.c
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<
%.o: %.S
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o game.elf game.bin
