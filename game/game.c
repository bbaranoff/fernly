/* A one-level platformer for the Fernvale LCD, loaded through fernly as a
 * payload at PSRAM address 0.  Uses fernly's LCD framebuffer driver (the LCD
 * is already initialised by fernly), the Fernvale keypad and the USB serial
 * port.
 *
 * Controls: keypad L/R move, U or A jump, '#' restarts.
 *           serial  a/d move, w or space jump, r restart, q reboots to BROM.
 */

#include <stdint.h>
#include <string.h>
#include "bionic.h"
#include "memio.h"
#include "printf.h"
#include "serial.h"
#include "lcd.h"
#include "fernvale-kbd.h"

#define TILE	16
#define VIEW_W	15			/* 240 / 16 */
#define VIEW_H	20			/* 320 / 16 */
#define LEVEL_W	64
#define LEVEL_H	20

/* '#' ground, '=' brick, '?' coin block, '|' pipe, 'o' coin, 'g' walker,
 * 'F' flag pole, 'P' player start, '.' sky */
static const char level_src[LEVEL_H][LEVEL_W + 1] = {
	"................................................................",
	"................................................................",
	"................................................................",
	"................................................................",
	"................................................................",
	"................................................................",
	".........................o.o....................................",
	"........................=?=?=.........oooo......................",
	"..............................................=====.............",
	"................?...............o.........................oo....",
	"...........................|..........?......===.........FF.....",
	"..................==?==....|...............................F....",
	"......o.o..................|.........|.....................F....",
	"...P.......g........g......|.....g...|.......g.............F....",
	"################.######.########################.##.###########",
	"################.######.########################.##.###########",
	"################.######.########################.##.###########",
	"################.######.########################.##.###########",
	"################.######.########################.##.###########",
	"################.######.########################.##.###########",
};

static char level[LEVEL_H][LEVEL_W];

/* 13x16 sprite, classic colours: R hat/shirt, S skin, B hair/overalls */
static const char *const mario_rows[16] = {
	"....RRRRR....", "...RRRRRRRRR.", "...BBBSSBS...", "..BSBSSSBSSS.",
	"..BSBBSSSBSSS", "..BBSSSSBBBB.", "....SSSSSSS..", "...RRBRRRB...",
	"..RRRBRRBRRR.", ".RRRRBBBBRRRR", ".SSRBSBBSBRSS", ".SSSBBBBBBSSS",
	".SSBBBBBBBBSS", "...BBB..BBB..", "..BBB....BBB.", ".BBBB....BBBB",
};

#define MAX_WALKERS 8
struct walker { int x, y, dir, alive; };
static struct walker walkers[MAX_WALKERS];
static int nwalkers;

static int px, py;			/* player position in pixels, top-left */
static int vx, vy;			/* velocity, 1/4 pixel units */
static int on_ground, facing;
static int score, lives, won, dead_timer;
static int cam_x;
static int frame;

static pixel_t *fb;
static int fbw, fbh;

/* ---- keypad (from cmd-keypad.c) ---- */
static const char key_vals[] = "LRUDAB123456789*0#";
static const uint32_t key_addr[] = {
	KBD_MEM2, KBD_MEM2, KBD_MEM1, KBD_MEM1, KBD_MEM3, KBD_MEM3, KBD_MEM2, KBD_MEM1, KBD_MEM3,
	KBD_MEM2, KBD_MEM1, KBD_MEM3, KBD_MEM2, KBD_MEM1, KBD_MEM3, KBD_MEM2, KBD_MEM2, KBD_MEM2,
};
static const uint32_t key_mask[] = {
	0x0000fffb, 0x0000fff7, 0x0000fdff, 0x0000fbff, 0x0000ffef, 0x0000ffdf, 0x0000ffef,
	0x0000f7ff, 0x0000ffbf, 0x0000ffdf, 0x0000efff, 0x0000ff7f, 0x0000ffbf, 0x0000dfff,
	0x0000feff, 0x0000dfff, 0x0000bfff, 0x00007fff,
};

/* The scanner alternates the key code with 0xffff, so a key counts as held
 * when it was seen in any of the last few scans. */
static uint8_t key_hold[18];

static void scan_keys(void)
{
	int i;
	for (i = 0; i < 18; i++) {
		if (readl(key_addr[i]) == key_mask[i])
			key_hold[i] = 4;
		else if (key_hold[i])
			key_hold[i]--;
	}
}

static int key_down(char k)
{
	int i;
	for (i = 0; key_vals[i]; i++)
		if (key_vals[i] == k)
			return key_hold[i] != 0;
	return 0;
}

/* serial keys act as "held" for a few frames */
static int ser_left, ser_right, ser_jump, ser_restart;

static void poll_serial(void)
{
	while (serial_available()) {
		uint8_t c = serial_getc();
		switch (c) {
		case 'a': ser_left = 6; break;
		case 'd': ser_right = 6; break;
		case 'w': case ' ': ser_jump = 3; break;
		case 'r': ser_restart = 1; break;
		case 'q':
			serial_puts("rebooting into BROM\n");
			writel(0x1209, 0xa003001c);
			while (1)
				;
		}
	}
	if (ser_left) ser_left--;
	if (ser_right) ser_right--;
	if (ser_jump) ser_jump--;
}

/* ---- drawing ---- */
static inline void put(int x, int y, pixel_t c)
{
	if (x >= 0 && x < fbw && y >= 0 && y < fbh)
		fb[y * fbw + x] = c;
}

static void fill_rect(int x, int y, int w, int h, pixel_t c)
{
	int i, j;
	for (j = y; j < y + h; j++) {
		if (j < 0 || j >= fbh) continue;
		for (i = x; i < x + w; i++)
			if (i >= 0 && i < fbw)
				fb[j * fbw + i] = c;
	}
}

static void draw_tile(int sx, int sy, char t)
{
	pixel_t a, b;
	int i, j;
	switch (t) {
	case '#':
		fill_rect(sx, sy, TILE, TILE, rgb(0xc8, 0x4c, 0x0c));
		for (j = 0; j < TILE; j += 8)
			for (i = 0; i < TILE; i += 8)
				fill_rect(sx + i + ((j / 8) & 1) * 4, sy + j, 3, 3, rgb(0xfc, 0x98, 0x38));
		break;
	case '=':
		fill_rect(sx, sy, TILE, TILE, rgb(0x9c, 0x4a, 0x00));
		fill_rect(sx, sy + 7, TILE, 1, rgb(0x40, 0x20, 0x00));
		fill_rect(sx + 7, sy, 1, 8, rgb(0x40, 0x20, 0x00));
		fill_rect(sx + 3, sy + 8, 1, 8, rgb(0x40, 0x20, 0x00));
		fill_rect(sx + 11, sy + 8, 1, 8, rgb(0x40, 0x20, 0x00));
		break;
	case '?':
		fill_rect(sx, sy, TILE, TILE, rgb(0xf8, 0xb0, 0x00));
		fill_rect(sx + 6, sy + 3, 4, 2, rgb(0x60, 0x30, 0x00));
		fill_rect(sx + 9, sy + 5, 2, 3, rgb(0x60, 0x30, 0x00));
		fill_rect(sx + 7, sy + 8, 2, 3, rgb(0x60, 0x30, 0x00));
		fill_rect(sx + 7, sy + 12, 2, 2, rgb(0x60, 0x30, 0x00));
		break;
	case 'x':	/* used block */
		fill_rect(sx, sy, TILE, TILE, rgb(0x80, 0x60, 0x30));
		break;
	case '|':
		fill_rect(sx, sy, TILE, TILE, rgb(0x00, 0xa8, 0x00));
		fill_rect(sx + 3, sy, 2, TILE, rgb(0x80, 0xf8, 0x80));
		break;
	case 'o':
		a = rgb(0xf8, 0xd0, 0x30); b = rgb(0xa0, 0x70, 0x00);
		for (j = 2; j < 14; j++)
			for (i = 4; i < 12; i++) {
				int dx = i - 8, dy = j - 8;
				if (dx * dx * 2 + dy * dy <= 36)
					put(sx + i, sy + j, (i == 4 || j == 2) ? b : a);
			}
		break;
	case 'F':
		fill_rect(sx + 7, sy, 2, TILE, rgb(0xd0, 0xd0, 0xd0));
		break;
	}
}

static void draw_sprite(int sx, int sy, int flip)
{
	int r, c;
	for (r = 0; r < 16; r++)
		for (c = 0; c < 13; c++) {
			pixel_t px_;
			switch (mario_rows[r][c]) {
			case 'R': px_ = rgb(0xe8, 0x28, 0x18); break;
			case 'S': px_ = rgb(0xf8, 0xb8, 0x88); break;
			case 'B': px_ = rgb(0x6b, 0x2f, 0x0c); break;
			default: continue;
			}
			put(sx + (flip ? 12 - c : c), sy + r, px_);
		}
}

static void draw_walker(int sx, int sy)
{
	pixel_t body = rgb(0x9c, 0x50, 0x1c), feet = rgb(0x30, 0x10, 0x00), eye = rgb(0xff, 0xff, 0xff);
	int i, j;
	for (j = 2; j < 13; j++)
		for (i = 1; i < 15; i++) {
			int dx = i - 8, dy = j - 7;
			if (dx * dx + dy * dy * 2 <= 44)
				put(sx + i, sy + j, body);
		}
	fill_rect(sx + 2, sy + 13, 5, 3, feet);
	fill_rect(sx + 9, sy + 13, 5, 3, feet);
	fill_rect(sx + 5, sy + 6, 2, 3, eye);
	fill_rect(sx + 9, sy + 6, 2, 3, eye);
	put(sx + 5, sy + 7, feet); put(sx + 10, sy + 7, feet);
}

static void draw_digits(int x, int y, int v)
{
	static const uint8_t font[10][5] = {
		{0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},{0x7,0x1,0x7,0x1,0x7},
		{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},{0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x1,0x1,0x1},
		{0x7,0x5,0x7,0x5,0x7},{0x7,0x5,0x7,0x1,0x7}};
	char buf[8];
	int n = 0, i, r, c;
	do { buf[n++] = v % 10; v /= 10; } while (v && n < 7);
	for (i = n - 1; i >= 0; i--, x += 8)
		for (r = 0; r < 5; r++)
			for (c = 0; c < 3; c++)
				if (font[buf[i]][r] & (4 >> c))
					fill_rect(x + c * 2, y + r * 2, 2, 2, rgb(0xff, 0xff, 0xff));
}

static void render(void)
{
	int tx, ty, i;
	int x0 = cam_x / TILE, ox = cam_x % TILE;
	pixel_t sky = rgb(0x5c, 0x94, 0xfc);

	fill_rect(0, 0, fbw, fbh, sky);
	for (ty = 0; ty < VIEW_H; ty++)
		for (tx = 0; tx <= VIEW_W; tx++) {
			int lx = x0 + tx;
			if (lx >= LEVEL_W) break;
			draw_tile(tx * TILE - ox, ty * TILE, level[ty][lx]);
		}
	for (i = 0; i < nwalkers; i++)
		if (walkers[i].alive)
			draw_walker(walkers[i].x - cam_x, walkers[i].y);
	if (!(dead_timer & 2))
		draw_sprite(px - cam_x, py, facing < 0);
	draw_digits(4, 4, score);
	for (i = 0; i < lives; i++)
		draw_sprite(200 + i * 14, 2, 0);
	if (won) {
		fill_rect(40, 140, 160, 40, rgb(0, 0, 0));
		draw_digits(60, 155, score);
	}
	lcd_run();
}

/* ---- game logic ---- */
static int solid(char t) { return t == '#' || t == '=' || t == '?' || t == 'x' || t == '|'; }

static char tile_at(int x, int y)
{
	int tx = x / TILE, ty = y / TILE;
	if (x < 0 || tx >= LEVEL_W || y < 0 || ty >= LEVEL_H) return '#';
	return level[ty][tx];
}

static void reset_level(void)
{
	int x, y;
	memcpy(level, level_src, sizeof(level));
	nwalkers = 0;
	for (y = 0; y < LEVEL_H; y++)
		for (x = 0; x < LEVEL_W; x++) {
			char c = level_src[y][x];
			if (c == 'P') { px = x * TILE; py = y * TILE; level[y][x] = '.'; }
			if (c == 'g' && nwalkers < MAX_WALKERS) {
				walkers[nwalkers].x = x * TILE; walkers[nwalkers].y = y * TILE;
				walkers[nwalkers].dir = -1; walkers[nwalkers].alive = 1; nwalkers++;
				level[y][x] = '.';
			}
		}
	vx = vy = 0; on_ground = 0; facing = 1; won = 0; dead_timer = 0; cam_x = 0;
}

static void die(void)
{
	lives--;
	serial_puts("ouch\n");
	if (lives <= 0) { lives = 3; score = 0; }
	reset_level();
	dead_timer = 30;
}

static void bump_block(int tx, int ty)
{
	if (level[ty][tx] == '?') { level[ty][tx] = 'x'; score += 10; }
	else if (level[ty][tx] == '=') { level[ty][tx] = '.'; score += 1; }
}

static void step(void)
{
	int left = key_down('L') || ser_left, right = key_down('R') || ser_right;
	int jump = key_down('U') || key_down('A') || ser_jump;
	int nx, ny, i;

	if (key_down('#') || ser_restart) { ser_restart = 0; lives = 3; score = 0; reset_level(); }
	if (dead_timer) { dead_timer--; return; }
	if (won) return;

	/* horizontal */
	if (left) { vx -= 2; facing = -1; } else if (right) { vx += 2; facing = 1; } else vx = vx * 3 / 4;
	if (vx > 10) vx = 10; if (vx < -10) vx = -10;
	nx = px + vx / 4;
	if (vx > 0 && (solid(tile_at(nx + 12, py)) || solid(tile_at(nx + 12, py + 15)))) { nx = ((nx + 12) / TILE) * TILE - 13; vx = 0; }
	if (vx < 0 && (solid(tile_at(nx, py)) || solid(tile_at(nx, py + 15)))) { nx = (nx / TILE + 1) * TILE; vx = 0; }
	if (nx < 0) nx = 0;
	px = nx;

	/* vertical */
	if (jump && on_ground) { vy = -34; on_ground = 0; }
	vy += 2;
	if (vy > 28) vy = 28;
	ny = py + vy / 4;
	if (vy >= 0) {
		if (solid(tile_at(px, ny + 16)) || solid(tile_at(px + 12, ny + 16))) {
			ny = ((ny + 16) / TILE) * TILE - 16; vy = 0; on_ground = 1;
		} else on_ground = 0;
	} else {
		if (solid(tile_at(px + 6, ny))) {
			bump_block((px + 6) / TILE, ny / TILE);
			ny = (ny / TILE + 1) * TILE; vy = 0;
		}
	}
	py = ny;
	if (py > fbh) die();

	/* coins and flag */
	for (i = 0; i < 4; i++) {
		int cx = px + (i & 1) * 12, cy = py + (i >> 1) * 15;
		int tx = cx / TILE, ty = cy / TILE;
		if (tx < LEVEL_W && ty < LEVEL_H) {
			if (level[ty][tx] == 'o') { level[ty][tx] = '.'; score += 5; }
			if (level[ty][tx] == 'F') { won = 1; score += 100; serial_puts("you win!\n"); }
		}
	}

	/* walkers */
	for (i = 0; i < nwalkers; i++) {
		struct walker *w = &walkers[i];
		int wx;
		if (!w->alive) continue;
		wx = w->x + w->dir;
		if (solid(tile_at(wx + (w->dir > 0 ? 15 : 0), w->y + 8)) || !solid(tile_at(wx + (w->dir > 0 ? 15 : 0), w->y + 16)))
			w->dir = -w->dir;
		else
			w->x = wx;
		if (px + 12 > w->x && px < w->x + 15 && py + 16 > w->y && py < w->y + 16) {
			if (vy > 0 && py + 16 < w->y + 10) { w->alive = 0; vy = -20; score += 20; }
			else die();
		}
	}

	/* camera */
	if (px - cam_x > 140) cam_x = px - 140;
	if (px - cam_x < 60) cam_x = px - 60;
	if (cam_x < 0) cam_x = 0;
	if (cam_x > LEVEL_W * TILE - fbw) cam_x = LEVEL_W * TILE - fbw;
}

static void delay_frame(void)
{
	volatile int i;
	for (i = 0; i < 60000; i++)
		;
}

void game_main(void)
{
	/* fernly already brought up the USB serial port and the LCD; re-running
	 * serial_init() here kills the live USB link, so don't. */
	writel(0xffe0 | 0x0008, 0xa0030004);	/* WDT_LENGTH: longest timeout */
	writel(0x2200 | 1, 0xa0030000);		/* WDT_MODE: enable, key 0x2200 */
	fb = lcd_fb();
	fbw = lcd_width();
	fbh = lcd_height();
	lives = 3;
	score = 0;
	reset_level();
	serial_puts("\nFernvale platformer: a/d move, w jump, r restart, q reboot\n");
	while (1) {
		writel(0x1971, 0xa0030008);		/* WDT_RESTART */
		poll_serial();
		scan_keys();
		step();
		render();
		delay_frame();
		if ((++frame % 128) == 0)
			printf("frame %d x=%d y=%d score=%d lives=%d keys=%c%c%c%c%c%c\n", frame, px, py, score, lives,
			       key_down('L') ? 'L' : '-', key_down('R') ? 'R' : '-', key_down('U') ? 'U' : '-',
			       key_down('D') ? 'D' : '-', key_down('A') ? 'A' : '-', key_down('B') ? 'B' : '-');
	}
}
