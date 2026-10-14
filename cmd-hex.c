#include <string.h>
#include "bionic.h"
#include "memio.h"
#include "printf.h"
#include "serial.h"
#include "utils.h"

int cmd_hex(int argc, char **argv)
{
	uint32_t offset;
	int count = 0x200;
	if (argc < 1) {
		printf("Usage: hex [offset] [[count]]\n");
		return -1;
	}

	offset = strtoul(argv[0], NULL, 0);

	if (argc > 1)
		count = strtoul(argv[1], NULL, 0);

	serial_print_hex((const void *)offset, count);
	return 0;
}

/* Dump memory as 16-bit words (ldrh), 16 words per line, no ASCII column.
 * Needed for the DSP memory windows at 0x821xxxxx-0x823xxxxx, where byte
 * reads do not return consistent data but 16-bit reads do. */
int cmd_hexw(int argc, char **argv)
{
	uint32_t offset;
	int count = 0x100;	/* in 16-bit words */
	int i;
	const volatile uint16_t *p;

	if (argc < 1) {
		printf("Usage: hexw [offset] [[count]]  (count in 16-bit words)\n");
		return -1;
	}

	offset = strtoul(argv[0], NULL, 0) & ~1;
	if (argc > 1)
		count = strtoul(argv[1], NULL, 0);

	p = (const volatile uint16_t *)offset;
	for (i = 0; i < count; i++) {
		if ((i % 16) == 0) {
			if (i)
				serial_puts("\n");
			serial_puth(offset + i * 2, 8);
			serial_putc(':');
		}
		serial_putc(' ');
		serial_puth(p[i], 4);
	}
	serial_puts("\n");
	return 0;
}
