/* Standalone payload entry: loaded by fernly at PSRAM address 0 (loadjmp 0). */
.section .text.start
.global _start
_start:
	ldr	sp, =0x007ffff0		/* top of the 8 MiB PSRAM */
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b
	bl	game_main
2:	b	2b
