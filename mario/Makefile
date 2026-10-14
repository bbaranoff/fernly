CROSS ?= arm-none-eabi-
MTK = /opt/GSM/osmocom-bb-mtk-layer1/src/target/firmware
CFLAGS = -march=armv5te -mfloat-abi=soft -marm -Os -Wall -fno-stack-protector \
	 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -I../include -I.
OBJS = start.o main.o lcd.o utils.o bionic.o vsprintf.o usb_serial.o

all: mario.bin

mario.bin: mario.elf
	$(CROSS)objcopy -O binary $< $@
	$(CROSS)size $<

mario.elf: $(OBJS) game.ld
	$(CROSS)ld --nostdlib -T game.ld -o $@ $(OBJS) ../lib/libgcc-armv5.a

usb_serial.o: $(MTK)/mediatek/uart.c
	$(CROSS)gcc -march=armv5te -mfloat-abi=soft -marm -Os -ffreestanding -fno-builtin -I$(MTK)/include -I$(MTK)/include/mtk -c -o $@ $<
%.o: %.c
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<
%.o: ../%.c
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<
%.o: %.S
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<

assets.h: mkassets.py
	python3 mkassets.py /tmp/claude-0/-opt-GSM-fernly/0bc00aac-3514-4174-ab71-18405aa1605b/scratchpad/ext/super-mario/GDC

clean:
	rm -f *.o mario.elf mario.bin
