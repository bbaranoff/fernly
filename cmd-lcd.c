#include <string.h>
#include "bionic.h"
#include "memio.h"
#include "printf.h"
#include "fernvale-lcd.h"
#include "lcd.h"

#ifdef LCD_DEBUG
static void lcd_dump(void)
{
	/* Dump registers */
//	printf("LCD_PAR0_CMD_PORT:      %04x\n", readw(LCD_PAR0_CMD_PORT_REG));
//	printf("LCD_PAR0_DAT_PORT:      %04x\n", readw(LCD_PAR0_DAT_PORT_REG));
//	printf("LCD_PAR1_CMD_PORT:      %04x\n", readw(LCD_PAR1_CMD_PORT_REG));
//	printf("LCD_PAR1_DAT_PORT:      %04x\n", readw(LCD_PAR1_DAT_PORT_REG));
//	printf("LCD_PAR0_CFG:           %08x\n", readl(LCD_PAR0_CFG_REG));
//	printf("LCD_PAR1_CFG:           %08x\n", readl(LCD_PAR1_CFG_REG));
	printf("LCD_STATUS:             %04x\n", readw(LCD_STATUS_REG));
	printf("LCD_INT_ENA:            %04x\n", readw(LCD_INT_ENA_REG));
	printf("LCD_INT_STAT:           %04x\n", readw(LCD_INT_STAT_REG));
	printf("LCD_RUN:                %04x\n", readw(LCD_RUN_REG));
	printf("LCD_RESET:              %04x\n", readw(LCD_RESET_REG));
	printf("LCD_PAR_DATA_WIDTH:     %08x\n", readl(LCD_PAR_DATA_WIDTH_REG));
	printf("LCD_TEARING:            %08x\n", readl(LCD_TEARING_REG));
	printf("LCD_AUTOCOPY_CTRL:      %08x\n", readl(LCD_AUTOCOPY_CTRL_REG));
	printf("LCD_AUTOCOPY_OFFSET:    %08x\n", readl(LCD_AUTOCOPY_OFFSET_REG));
	printf("LCD_AUTOCOPY_SIZE:      %08x\n", readl(LCD_AUTOCOPY_SIZE_REG));
	printf("LCD_AUTOCOPY_CMD_ADDR:  %04x\n", readw(LCD_AUTOCOPY_CMD_ADDR_REG));
	printf("LCD_AUTOCOPY_DATA_ADDR: %04x\n", readw(LCD_AUTOCOPY_DATA_ADDR_REG));
	printf("LCD_LAYER0_CTRL:        %08x\n", readl(LCD_LAYER0_CTRL_REG));
	printf("LCD_LAYER0_OFFSET:      %08x\n", readl(LCD_LAYER0_OFFSET_REG));
	printf("LCD_LAYER0_SIZE:        %08x\n", readl(LCD_LAYER0_SIZE_REG));
	printf("LCD_LAYER0_SRC_ADDR:    %08x\n", readl(LCD_LAYER0_SRC_ADDR_REG));
//	printf("LCD_LAYER0_MEM_PITCH:   %08x\n", readl(LCD_LAYER0_MEM_PITCH_REG));
//	printf("LCD_FRAME_COUNTER_CON:  %08x\n", readl(LCD_FRAME_COUNTER_CON_REG));
//	printf("LCD_FRAME_COUNTER:      %08x\n", readl(LCD_FRAME_COUNTER_REG));
}
#endif /* LCD_DEBUG */

static int is_command(int argc, char **argv, const char *cmd)
{
	return ((argc > 0) && !_strcasecmp(argv[0], cmd));
}

static pixel_t color_wheel(int step) {
	step &= 255;
	if (step < 85) {
		return rgb(step * 3, 255 - step * 3, 0);
	}
	else if (step < 170) {
		step -= 85;
		return rgb(255 - step * 3, 0, step * 3);
	}
	else {
		step -= 170;
		return rgb(0, step * 3, 255 - step * 3);
	}
}

/* 5x7 column-major font (bit 0 = top row) for '0'-'9', 'A'-'Z' and '!' */
static const uint8_t font5x7[][5] = {
	{0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
	{0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
	{0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
	{0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
	{0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
	{0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
	{0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
	{0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
	{0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
	{0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
	{0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
	{0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
	{0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
	{0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
	{0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
	{0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
	{0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
	{0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
	{0x00,0x00,0x5f,0x00,0x00},
	{0x00,0x36,0x36,0x00,0x00}, {0x00,0x41,0x22,0x1c,0x00},
};

static const uint8_t *font_glyph(char c)
{
	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c >= '0' && c <= '9')
		return font5x7[c - '0'];
	if (c >= 'A' && c <= 'Z')
		return font5x7[10 + c - 'A'];
	if (c == '!')
		return font5x7[36];
	if (c == ':')
		return font5x7[37];
	if (c == ')')
		return font5x7[38];
	return NULL;
}

/* Draw one line of text, horizontally centered, at row y0, scaled up */
static void lcd_draw_text(const char *s, int y0, int scale, pixel_t fg)
{
	pixel_t *fb = lcd_fb();
	int w = lcd_width();
	int h = lcd_height();
	int len = 0;
	int cw = 6 * scale;		/* 5 columns + 1 gap */
	int x0;
	int i, col, row, sx, sy;

	while (s[len])
		len++;
	x0 = (w - len * cw) / 2;
	if (x0 < 0)
		x0 = 0;

	for (i = 0; i < len; i++, x0 += cw) {
		const uint8_t *g = font_glyph(s[i]);
		if (!g)
			continue;
		for (col = 0; col < 5; col++) {
			for (row = 0; row < 7; row++) {
				if (!(g[col] & (1 << row)))
					continue;
				for (sy = 0; sy < scale; sy++) {
					int y = y0 + row * scale + sy;
					if (y < 0 || y >= h)
						continue;
					for (sx = 0; sx < scale; sx++) {
						int x = x0 + col * scale + sx;
						if (x >= 0 && x < w)
							fb[y * w + x] = fg;
					}
				}
			}
		}
	}
}

/* Yellow smiley face of radius r centered on (cx, cy) */
static void lcd_draw_smiley(int cx, int cy, int r)
{
	pixel_t *fb = lcd_fb();
	int w = lcd_width();
	int h = lcd_height();
	pixel_t face = rgb(255, 210, 0);
	pixel_t ink = rgb(0, 0, 0);
	int x, y;

	for (y = cy - r; y <= cy + r; y++) {
		if (y < 0 || y >= h)
			continue;
		for (x = cx - r; x <= cx + r; x++) {
			int dx = x - cx, dy = y - cy;
			int d2 = dx * dx + dy * dy;
			int ex, ey;
			pixel_t px = face;

			if (x < 0 || x >= w || d2 > r * r)
				continue;

			/* Eyes at (+-0.35r, -0.3r), radius r/8 */
			ex = (dx < 0 ? dx + r * 7 / 20 : dx - r * 7 / 20);
			ey = dy + r * 6 / 20;
			if (ex * ex + ey * ey <= (r / 8) * (r / 8))
				px = ink;

			/* Smile: lower arc of a ring between 0.55r and 0.7r */
			if (dy > r * 2 / 20 &&
			    d2 >= (r * 11 / 20) * (r * 11 / 20) &&
			    d2 <= (r * 14 / 20) * (r * 14 / 20))
				px = ink;

			fb[y * w + x] = px;
		}
	}
}


/* 13x16 small plumber sprite, GitHub contribution-graph greens.
 * R = hat/shirt, S = skin, B = hair/overalls/shoes, . = background */
static const char *const mario_rows[16] = {
	"....RRRRR....",
	"...RRRRRRRRR.",
	"...BBBSSBS...",
	"..BSBSSSBSSS.",
	"..BSBBSSSBSSS",
	"..BBSSSSBBBB.",
	"....SSSSSSS..",
	"...RRBRRRB...",
	"..RRRBRRBRRR.",
	".RRRRBBBBRRRR",
	".SSRBSBBSBRSS",
	".SSSBBBBBBSSS",
	".SSBBBBBBBBSS",
	"...BBB..BBB..",
	"..BBB....BBB.",
	".BBBB....BBBB",
};

static void lcd_draw_mario(void)
{
	pixel_t *fb = lcd_fb();
	int w = lcd_width(), h = lcd_height();
	int scale = 12, cols = 13, rows = 16;
	int x0 = (w - cols * scale) / 2, y0 = (h - rows * scale) / 2;
	pixel_t bg = rgb(0x0d, 0x11, 0x17);	/* GitHub dark background */
	int x, y, r, c, sx, sy;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			fb[y * w + x] = bg;

	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			pixel_t px;
			switch (mario_rows[r][c]) {
			case 'R': px = rgb(0x39, 0xd3, 0x53); break;	/* brightest green */
			case 'S': px = rgb(0x9b, 0xe9, 0xa8); break;	/* pale green */
			case 'B': px = rgb(0x0e, 0x44, 0x29); break;	/* darkest green */
			default: continue;
			}
			for (sy = 0; sy < scale; sy++)
				for (sx = 0; sx < scale; sx++) {
					x = x0 + c * scale + sx;
					y = y0 + r * scale + sy;
					if (x >= 0 && x < w && y >= 0 && y < h)
						fb[y * w + x] = px;
				}
		}
	}
	lcd_run();
}

/* Clear the screen and show up to [count] lines of text */
static void lcd_show_lines(int count, char **lines)
{
	int scale = 6;			/* 30x42 px glyphs, 6 per line */
	int maxlen = 1;
	int lh, y0, i, n;

	/* Shrink the glyphs until the longest line fits the width */
	for (i = 0; i < count; i++) {
		for (n = 0; lines[i][n]; n++)
			;
		if (n > maxlen)
			maxlen = n;
	}
	while (scale > 1 && maxlen * 6 * scale > (int)lcd_width())
		scale--;
	lh = 9 * scale;			/* 7 rows + 2 rows of spacing */
	y0 = (lcd_height() - count * lh) / 2;

	memset(lcd_fb(), 0, lcd_width() * lcd_height() * lcd_bpp());
	for (i = 0; i < count; i++)
		lcd_draw_text(lines[i], y0 + i * lh, scale, rgb(255, 255, 255));
	lcd_run();
}


int cmd_lcd(int argc, char **argv)
{
	int i;
	int ret;

	if (is_command(argc, argv, "init")) {
		printf("Initializing LCD... ");
		ret = lcd_init();
		if (ret)
			printf("failed: %d\n", ret);
		else
			printf("Ok\n");
	}
#ifdef LCD_DEBUG
	else if (is_command(argc, argv, "dump")) {
		lcd_dump();
	}
#endif
	else if (is_command(argc, argv, "run")) {
		printf("Running LCD... ");
		ret = lcd_run();
		if (ret)
			printf("failed: %d\n", ret);
		else
			printf("Ok\n");
	}
	else if (is_command(argc, argv, "stop")) {
		printf("Stopping LCD... ");
		ret = lcd_stop();
		if (ret)
			printf("failed: %d\n", ret);
		else
			printf("Ok\n");
	}
	else if (is_command(argc, argv, "tpp1")) {
		int w = lcd_width();
		int h = lcd_height();
		int total = w * h;

		for (i = 0; i < total; i++)
			lcd_addpixel(i);
	}
	else if (is_command(argc, argv, "tpp2")) {
		int x, y;

		i = 0;
		for (y = 0; y < lcd_height(); y++)
			for (x = 0; x < lcd_width(); x++)
				lcd_addpixel(rgb(i++, 0, 0));
	}
	else if (is_command(argc, argv, "tpd")) {
		static int step = 0;
		pixel_t *fb;
		int x, y;
		int w, h;

		fb = lcd_fb();

		h = lcd_height();
		w = lcd_width();

		/* Stupid clear-screen */
		memset(fb, 0, w * h * lcd_bpp());

		printf("Width: %d  Height: %d\n", w, h);

		i = step++;
		fb = lcd_fb();
		for (y = 0; y < h; y++) {
			for (x = 0; x < w; x++) {
				/* Swap axes, to verify X and Y work */
				if (step & 1)
					*fb++ = color_wheel(y + step);
				else
					*fb++ = color_wheel(x + step);
			}
		}
		lcd_run();
	}
	else if (is_command(argc, argv, "hello")) {
		memset(lcd_fb(), 0, lcd_width() * lcd_height() * lcd_bpp());
		lcd_draw_text("HELLO", 40, 6, rgb(255, 255, 255));
		lcd_draw_smiley(lcd_width() / 2, 200, 60);
		lcd_run();
		printf("Hello :)\n");
	}
	else if (is_command(argc, argv, "mario")) {
		lcd_draw_mario();
		printf("It's-a me, in GitHub green\n");
	}
	else if (is_command(argc, argv, "text")) {
		if (argc < 2)
			printf("usage: lcd text WORD [WORD...] (one word per line)\n");
		else
			lcd_show_lines(argc - 1, argv + 1);
	}
	else {
		printf("lcd sub-commands (usage: lcd [subcmd]):\n");
		printf("\tinit    Initialize LCD registers\n");
		printf("\trun     Transfer one frame of the LCD\n");
		printf("\tstop    Stop and reset LCD auto-update\n");
#ifdef LCD_DEBUG
		printf("\tdump    Dump current register list\n");
#endif
		printf("\ttpp1    Display bitbanged, PIO 'test pattern 1'\n");
		printf("\ttpp2    Display bitbanged, PIO 'test pattern 2'\n");
		printf("\ttpd     DMA test pattern (flips on each iteration)\n");
		printf("\thello   Show HELLO and a smiley on the screen\n");
		printf("\ttext    Show up to a few words, one per line\n");
		printf("\tmario   Pixel plumber in GitHub contribution greens\n");
	}

	return 0;
}

