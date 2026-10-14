OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY { psram (rwx) : ORIGIN = 0x00000000, LENGTH = 0x40000 } /* below the LCD framebuffer at 0x40000 */
SECTIONS
{
	.text : { KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) } > psram
	.data : { *(.data .data.*) } > psram
	.bss  : { __bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(4); __bss_end = .; } > psram
}
