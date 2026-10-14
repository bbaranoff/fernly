/* Port of the DOS "Super Mario in C" clone (nrthyrk/super-mario, Turbo C,
 * VGA mode 13h) to the Fernvale MT6260: same game logic and 8-bit indexed
 * rendering into a 320x200 buffer, which is then converted through the PCX
 * palette and rotated 90 degrees into fernly's 240x320 RGB565 framebuffer.
 * Hold the board sideways.
 *
 * Keys: keypad L/R move, U or A jump, B run, '#' reboot into the boot ROM.
 *       serial a/d move, w jump, b run, q reboot.
 */

#include <stdint.h>
#include <string.h>
#include "bionic.h"
#include "memio.h"
#include "printf.h"

/* USB serial: the mtk-layer1 driver (proven both ways on this board) */
extern void uart_init(uint8_t uart, uint8_t interrupts);
extern int uart_getchar_nb(uint8_t uart, uint8_t *ch);
extern void uart_putchar_wait(uint8_t uart, int c);
extern int serial_puts(const void *s);
int serial_putc(uint8_t c) { uart_putchar_wait(0, c); return 0; }
#include "lcd.h"
#include "fernvale-kbd.h"
#include "assets.h"

#define BACKGROUND 91

/* ---------- original data structures ---------- */
typedef struct {
	signed char mario[7][16][16];
	const unsigned char *block[52];	/* 256 pixels each; original had 2 header bytes, see BLK() */
	signed char world[12][217];
	const unsigned char *coin[3];
	short int bumped[2];
	short int bumploop;
	short int sprite;
	short int direction;
	short int maxlength;
	short int startpos[2];
} graphics;

typedef struct wt { short int action, x, y, dx, dy; struct wt *next; } cwtype;
typedef struct bt { short int action, x, y, dx, dy; struct bt *next; } btype;
typedef struct ct { short int coinx, coiny, coinloop, dy; struct ct *next; } cointype;

static short int screenx, screen_move;
static graphics data;
static int mariox, marioy;
static short int landed;
static cointype coin_head, *hcoin = &coin_head;
static cwtype *hcw, *tcw;
static btype *hbt, *tbt;

static uint8_t vga[320 * 200];
static int force_full;

/* the original indexes sprite data as blck[j*16+i+2] (2 header bytes) */
#define BLK(p, j, i) ((p)[(j) * 16 + (i)])

/* ---------- small libc replacements ---------- */
static int my_abs(int v) { return v < 0 ? -v : v; }
#define abs my_abs

static uint32_t rnd_state = 12345;
static int prand(void)
{
	rnd_state = rnd_state * 1103515245u + 12345u;
	return (rnd_state >> 16) & 1;
}

#define POOL 32
static cwtype cw_pool[POOL];
static btype bt_pool[POOL];
static cointype coin_pool[POOL];
static uint8_t cw_used[POOL], bt_used[POOL], coin_used[POOL];

static void *pool_alloc(void *pool, uint8_t *used, int size)
{
	int i;
	for (i = 0; i < POOL; i++)
		if (!used[i]) {
			used[i] = 1;
			memset((char *)pool + i * size, 0, size);
			return (char *)pool + i * size;
		}
	return 0;
}

static void pool_free(void *pool, uint8_t *used, int size, void *p)
{
	int i = ((char *)p - (char *)pool) / size;
	if (i >= 0 && i < POOL)
		used[i] = 0;
}

/* ---------- time ---------- */
#define USCNT_CON	0xa0180080
#define USCNTI_VAL	0xa0180088

static uint32_t us_now(void) { return readl(USCNTI_VAL); }

static void time_delay(int ms)
{
	uint32_t t0 = us_now();
	while ((uint32_t)(us_now() - t0) < (uint32_t)ms * 1000)
		;
}

/* ---------- keyboard: keypad matrix + serial, exposed like the DOS keybuf ---------- */
enum { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_CTRL, KEY_Q, NKEYS };
static uint8_t keybuf[NKEYS];

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
static uint8_t key_hold[18];
static uint8_t ser_hold[NKEYS];

static int pad(char k)
{
	int i;
	for (i = 0; key_vals[i]; i++)
		if (key_vals[i] == k)
			return key_hold[i] != 0;
	return 0;
}

static void update_keys(void)
{
	int i;
	for (i = 0; i < 18; i++) {
		if (readl(key_addr[i]) == key_mask[i])
			key_hold[i] = 4;
		else if (key_hold[i])
			key_hold[i]--;
	}
	uint8_t c;
	while (uart_getchar_nb(0, &c)) {
		switch (c) {
		case 'a': ser_hold[KEY_LEFT] = 6; break;
		case 'd': ser_hold[KEY_RIGHT] = 6; break;
		case 'w': ser_hold[KEY_UP] = 4; break;
		case 'b': ser_hold[KEY_CTRL] = 10; break;
		case 'q': ser_hold[KEY_Q] = 1; break;
		}
	}
	for (i = 0; i < NKEYS; i++)
		if (ser_hold[i])
			ser_hold[i]--;
	keybuf[KEY_LEFT] = pad('L') || ser_hold[KEY_LEFT];
	keybuf[KEY_RIGHT] = pad('R') || ser_hold[KEY_RIGHT];
	keybuf[KEY_UP] = pad('U') || pad('A') || ser_hold[KEY_UP];
	keybuf[KEY_CTRL] = pad('B') || ser_hold[KEY_CTRL];
	keybuf[KEY_Q] = pad('#') || ser_hold[KEY_Q];
}

/* ---------- rendering (same as gdcfunc.c, VGA buffer replaced by vga[]) ---------- */
static void put_pix(int x, int y, int color)
{
	if (x >= 0 && x < 320 && y >= 0 && y < 200)
		vga[y * 320 + x] = color;
}

static void fillscreen(int color)
{
	memset(vga, color, sizeof(vga));
	force_full = 1;
}

static pixel_t pal[256];
static uint8_t shadow[320 * 200];	/* what the LCD currently shows, 8-bit */

static void present(void)
{
	pixel_t *fb = lcd_fb();
	int x, y;
	/* game (x, y) -> fb row x, fb column 219 - y: board held sideways.
	 * Only pixels that changed since the last frame are written to the
	 * (slow, uncached) framebuffer; the 8-bit buffers are cached. */
	for (y = 0; y < 200; y++) {
		const uint8_t *src = &vga[y * 320];
		uint8_t *old = &shadow[y * 320];
		pixel_t *dst = fb + (219 - y);
		if (!force_full) {
			const uint32_t *s32 = (const uint32_t *)src, *o32 = (const uint32_t *)old;
			int same = 1;
			for (x = 0; x < 80; x++)
				if (s32[x] != o32[x]) { same = 0; break; }
			if (same)
				continue;
		}
		for (x = 0; x < 320; x++, dst += 240)
			if (force_full || src[x] != old[x]) {
				*dst = pal[src[x]];
				old[x] = src[x];
			}
	}
	force_full = 0;
	lcd_run();
}

static int mario_visible_px(int px, int py, int mx, int my)
{
	/* true when the mario sprite covers screen pixel (px, py) and is opaque there */
	if (px >= mx + 32 && px < mx + 16 + 32 && py >= my - 15 && py < my + 16 - 15) {
		if (data.direction == 0 && data.mario[abs(data.sprite / 2)][py - (my - 15)][px - (mx + 32)] != -1)
			return 1;
		if (data.direction != 0 && data.mario[abs(data.sprite / 2)][py - (my - 15)][(mx + 32 + 15) - px] != -1)
			return 1;
	}
	return 0;
}

static void put_masked_pbm(int x, int y, const unsigned char *blck, int mx, int my)
{
	int i, j;
	for (i = 0; i < 16; i++)
		for (j = 0; j < 16; j++) {
			if (mario_visible_px(i + x, j + y, mx, my))
				continue;
			put_pix(x + i, y + j, BLK(blck, j, i) == 0 ? BACKGROUND : BLK(blck, j, i));
		}
}

static void put_masked_pbmcw(int x, int y, const unsigned char *blck)
{
	int i, j;
	for (i = 0; i < 16; i++)
		for (j = 0; j < 16; j++)
			if (BLK(blck, j, i) != 0)
				put_pix(x + i, y + j, BLK(blck, j, i));
}

static void draw_block(int x, int y, int mx, int my)
{
	int i, j;
	for (i = 0; i < 16; i++)
		for (j = 0; j < 16; j++) {
			if (mario_visible_px(i + x, j + y, mx, my))
				continue;
			put_pix(i + x, j + y, BACKGROUND);
		}
}

/* ---------- enemies and coins (lb.c) ---------- */
static void add_cw(short int x, short int y)
{
	cwtype *temp = pool_alloc(cw_pool, cw_used, sizeof(cwtype));
	if (!temp) return;
	temp->next = 0; temp->x = x; temp->y = y; temp->action = 1; temp->dx = 1; temp->dy = 0;
	if (hcw == 0) hcw = temp; else tcw->next = temp;
	tcw = temp;
}

static void free_cw(void)
{
	cwtype *p;
	while (hcw) { p = hcw; hcw = hcw->next; pool_free(cw_pool, cw_used, sizeof(cwtype), p); }
	hcw = tcw = 0;
}

static void draw_cw(void)
{
	cwtype *p;
	for (p = hcw; p; p = p->next)
		if (p->action == 1)
			put_masked_pbmcw(p->x, p->y, data.block[37]);
}

static int move_cw(int move, int *upp, int *jumploopp)
{
	int up = *upp, jumploop = *jumploopp;
	cwtype *p;
	int loop1;
	for (p = hcw; p; p = p->next) {
		if (p->action == -1) continue;
		if (marioy > p->y && marioy < p->y + 14 && abs(mariox + 32 - p->x) < 10) { p->action = -1; up = -10; jumploop++; continue; }
		if (abs(p->x + 8 - (mariox + 32 + 8)) < 10 && abs(p->y + 8 - (marioy - 8)) < 10) return 3;
		if (p->y < 160) p->dy++;
		for (loop1 = 0; loop1 <= p->dy; loop1++)
			if (data.world[(p->y + loop1) / 16 + 1][(p->x + screenx + 13) / 16 - 2] > 26) { p->dy = loop1; p->dy = 0; }
		p->y += p->dy;
		p->x = p->x - p->dx;
		if (screen_move) p->x = p->x - move / 4;
		if (p->y >= 200 || p->x <= 32 || p->x >= 320 - 16) p->action = -1;
	}
	*upp = up; *jumploopp = jumploop;
	return 0;
}

static void addcoin(int x, int y)
{
	cointype *temp, *c = hcoin;
	while (c->next) c = c->next;
	temp = pool_alloc(coin_pool, coin_used, sizeof(cointype));
	if (!temp) return;
	temp->next = 0; temp->coinx = x; temp->coiny = y; temp->coinloop = -10; temp->dy = 0;
	c->next = temp;
}

static void removecoin(void)
{
	cointype *temp = hcoin->next;
	if (!temp) return;
	hcoin->next = temp->next;
	pool_free(coin_pool, coin_used, sizeof(cointype), temp);
}

static void drawcoin(int x, int mx, int my)
{
	cointype *c = hcoin;
	int loop, num = 0;
	while (c->next) { num++; c = c->next; }
	c = hcoin;
	for (loop = 0; loop < num; loop++) {
		c = c->next;
		if (!c) break;
		if ((c->coinx) * 16 + 32 - x > 16)
			put_masked_pbm(c->coinx * 16 + 32 - x, c->coiny * 16 + c->dy, data.coin[abs(c->coinloop % 3)], mx, my);
		c->coinloop++;
		c->dy += c->coinloop;
		if (c->coinloop >= 10) { removecoin(); c = hcoin; }
	}
}

static void add_bt(short int x, short int y)
{
	btype *temp = pool_alloc(bt_pool, bt_used, sizeof(btype));
	if (!temp) return;
	temp->next = 0; temp->x = x; temp->y = y; temp->action = 1; temp->dx = -1; temp->dy = -1;
	if (hbt == 0) hbt = temp; else tbt->next = temp;
	tbt = temp;
}

static void free_bt(void)
{
	btype *p;
	while (hbt) { p = hbt; hbt = hbt->next; pool_free(bt_pool, bt_used, sizeof(btype), p); }
	hbt = tbt = 0;
}

static void draw_bt(void)
{
	btype *p;
	for (p = hbt; p; p = p->next)
		if (p->action == 1)
			put_masked_pbmcw(p->x, p->y, data.block[38]);
}

static int move_bt(int move)
{
	btype *p;
	int loop1;
	for (p = hbt; p; p = p->next) {
		if (p->action == -1) continue;
		if (abs(p->x + 8 - (mariox + 32 + 8)) < 14 && abs(p->y + 8 - (marioy - 8)) < 14) return 3;
		if (p->x < mariox + 32 - 50) p->dx = prand(); else if (p->x - 50 > mariox + 32) p->dx = -prand();
		if (p->y < marioy - 50) p->dy = prand(); else if (p->y - 50 > marioy) p->dy = -prand();
		p->y += p->dy; p->x += p->dx;
		for (loop1 = 0; loop1 <= p->dy; loop1++)
			if (data.world[(p->y + loop1) / 16 + 1][(p->x + screenx + 13) / 16 - 2] > 26) p->dy = -p->dy;
		if (screen_move) { p->x = p->x - move / 4; screen_move = 0; }
		if (p->y >= 200 || p->x <= 32 || p->x >= 320 - 16 || p->y < 16) p->action = -1;
	}
	return 0;
}

/* ---------- gdcfunc.c logic ---------- */
static void init(void)
{
	int loop, r, c;
	for (loop = 0; loop < 52; loop++) data.block[loop] = block_px[loop];
	for (loop = 0; loop < 3; loop++) data.coin[loop] = coin_px[loop];
	for (loop = 0; loop < 7; loop++)
		for (r = 0; r < 16; r++)
			for (c = 0; c < 16; c++)
				data.mario[loop][r][c] = (signed char)mario_px[loop][r * 16 + c];
	hcoin->next = 0;
	screenx = 0;
	data.maxlength = 216;
	marioy = 140;
	data.startpos[0] = 85;
	data.startpos[1] = 140;
}

static void getinputs(void)
{
	memcpy(data.world, world_src, sizeof(data.world));
}

static void drawscreen(int x, int mx, int my)
{
	int loop1, loop2, back, x2;
	static int dy;
	x2 = x; back = 0;
	while (x2 >= 16) { x2 -= 16; back++; }
	for (loop1 = 0; loop1 < 12; loop1++)
		for (loop2 = back; loop2 <= back + 16; loop2++) {
			if (loop2 > 216) break;
			if (data.world[loop1][loop2] > 0) {
				if (data.bumped[0] == loop2 && data.bumped[1] == loop1) {
					dy += data.bumploop;
					put_masked_pbm((loop2 << 4) - x + 32, (loop1 << 4) + dy, data.block[data.world[loop1][loop2] - 1], mx, my);
					data.bumploop++;
					if (data.bumploop == 3) { data.bumploop = 0; data.bumped[0] = 0; data.bumped[1] = 0; dy = 0; }
				} else {
					if (data.world[loop1][loop2] == 38) { add_cw((loop2 << 4) - x, loop1 << 4); data.world[loop1][loop2] = 0; }
					else if (data.world[loop1][loop2] == 39) { add_bt((loop2 << 4) - x, loop1 << 4); data.world[loop1][loop2] = 0; }
					else put_masked_pbm((loop2 << 4) - x + 32, loop1 << 4, data.block[data.world[loop1][loop2] - 1], mx, my);
				}
			} else
				draw_block((loop2 << 4) - x + 32, loop1 << 4, mx, my);
		}
	drawcoin(x, mx, my);
	for (loop1 = 0; loop1 <= 15; loop1++)
		for (loop2 = 0; loop2 <= 15; loop2++)
			if (data.mario[abs(data.sprite / 2)][loop1][loop2] >= 0 && my - 15 + loop1 >= 0 && my - 15 + loop1 < 192) {
				if (data.direction == 0)
					put_pix(mx + loop2 + 32, my - 15 + loop1, data.mario[data.sprite / 2][loop1][loop2]);
				else
					put_pix(mx + 15 - loop2 + 32, my - 15 + loop1, data.mario[data.sprite / 2][loop1][loop2]);
			}
}

static void exitgame(void)
{
	serial_puts("rebooting into BROM\n");
	time_delay(50);
	writel(0x1209, 0xa003001c);
	while (1)
		;
}

static void check_keybuf(int *movep, int *finishp, int *upp, int *downp, int *jumpp)
{
	int move = *movep, up = *upp, down = *downp, jumploop = *jumpp;
	if (keybuf[KEY_RIGHT] && !keybuf[KEY_LEFT]) {
		if (keybuf[KEY_CTRL]) { if (move < 16) move++; }
		else { if (move > 8) move--; if (move < 8) move++; }
		if (landed) data.direction = 0;
	}
	if (keybuf[KEY_LEFT] && !keybuf[KEY_RIGHT]) {
		if (keybuf[KEY_CTRL]) { if (move > -16) move--; }
		else { if (move < -8) move++; if (move > -8) move--; }
		if (landed) data.direction = 1;
	}
	if (keybuf[KEY_UP] && !down) {
		up = -17; data.sprite = 12; jumploop++;
		if (jumploop > 10) { down = 1; jumploop = 0; }
	}
	if ((!keybuf[KEY_RIGHT] && !keybuf[KEY_LEFT]) || (keybuf[KEY_RIGHT] && keybuf[KEY_LEFT])) {
		if (move > 0) move--;
		if (move < 0) move++;
	}
	if (keybuf[KEY_Q]) exitgame();
	*movep = move; *upp = up; *downp = down; *jumpp = jumploop;
	(void)finishp;
}

static void animate_mario(int move)
{
	if (landed) {
		if (data.sprite == 10 && ((move > 0 && data.direction == 0) || (move < 0 && data.direction == 1))) data.sprite = 1;
		if (move == 0) data.sprite = 0;
		if ((move > 0 && data.direction == 0) || (move < 0 && data.direction == 1)) {
			if (abs(move) < 16) data.sprite++; else data.sprite += 2;
			if (data.sprite >= 10) data.sprite = 2;
		}
		if ((move > 0 && data.direction == 1) || (move < 0 && data.direction == 0)) data.sprite = 10;
	}
}

static void move_mario(int *movep)
{
	int move = *movep, loop1, loop2;
	if (marioy > 16) {
		if (move > 0)
			for (loop1 = 0; loop1 < 16; loop1++)
				for (loop2 = 0; loop2 <= move; loop2++)
					if (data.world[(marioy - loop1) / 16][(mariox + screenx + 13 + loop2 / 4) / 16] > 26) move = loop2;
		if (move < 0)
			for (loop1 = 0; loop1 < 16; loop1++)
				for (loop2 = 0; loop2 >= move; loop2--)
					if (data.world[(marioy - loop1) / 16][(mariox + screenx + 2 + loop2 / 4) / 16] > 26 || (mariox + loop1 / 2) <= 0) move = loop2;
	}
	if (mariox > 100 && move > 0) { screenx += move / 4; screen_move = 1; }
	else if (mariox + move / 4 > 0) { mariox += move / 4; screen_move = 0; }
	*movep = move;
}

static void check_jump(int *upp, int *downp, int *jumploopp)
{
	int loop1, loop2, up = *upp, down = *downp, jumploop = *jumploopp;
	loop1 = 3; landed = 0;
	while (loop1 < 13 && landed == 0) {
		landed = data.world[(marioy + 1) / 16][(mariox + screenx + loop1) / 16] > 26;
		loop1++;
	}
	if (!landed || marioy < 16) { if (up < 17) up += 1; }
	if (up > 0) {
		down = 1; loop1 = marioy;
		if (marioy < 207)
			while (loop1++ < marioy + up / 4)
				for (loop2 = 3; loop2 < 14; loop2 += 9)
					if (data.world[loop1 / 16][(mariox + screenx + loop2) / 16] > 26) {
						marioy = loop1 - 1; loop1 = marioy + up / 4; loop2 = 13; up = 0; down = 0; data.sprite = 0;
					}
		marioy += up / 4;
	}
	if (up < 0) {
		if (!keybuf[KEY_UP]) { down = 1; jumploop = 0; }
		loop1 = marioy;
		marioy += up / 4;
		if (marioy > 24)
			while (loop1-- >= marioy + up / 4) {
				if (loop1 / 16 - 1 < 0) break;
				if (data.world[loop1 / 16 - 1][(mariox + screenx + 8) / 16] > 26 || data.world[loop1 / 16 - 1][(mariox + screenx + 8) / 16] == 18) {
					marioy = loop1; loop1 = marioy + up / 4 - 1; up = -up; down = 1; jumploop = 0;
					if (data.bumped[0] == 0 && data.bumped[1] == 0) {
						data.bumped[0] = (mariox + screenx + 8) / 16; data.bumped[1] = marioy / 16 - 1; data.bumploop = -3;
					}
				}
				if (data.world[loop1 / 16 - 1][(mariox + screenx + 12) / 16] > 26 && data.world[loop1 / 16 - 1][(mariox + screenx + 8) / 16] < 26) mariox -= 1;
				if (data.world[loop1 / 16 - 1][(mariox + screenx + 3) / 16] > 26 && data.world[loop1 / 16 - 1][(mariox + screenx + 8) / 16] < 26) mariox += 1;
			}
	}
	*upp = up; *downp = down; *jumploopp = jumploop;
}

static void check_blocks(void)
{
	int bumpedblock;
	if (marioy >= 8 && data.world[(marioy - 8) / 16][(mariox + screenx + 8) / 16] == 10)
		data.world[(marioy - 8) / 16][(mariox + screenx + 8) / 16] = 0;
	if (data.bumploop == -3 && marioy / 16 - 1 >= 0) {
		bumpedblock = data.world[marioy / 16 - 1][(mariox + screenx + 8) / 16];
		switch (bumpedblock) {
		case 18: case 28:
			data.world[marioy / 16 - 1][(mariox + screenx + 8) / 16] = 27;
			addcoin(data.bumped[0], data.bumped[1] - 1); break;
		case 13:
			data.world[marioy / 16 - 1][(mariox + screenx + 8) / 16] = 27;
			add_cw(mariox, 40); add_cw(mariox + 16, 20); add_cw(mariox - 16, 20); break;
		case 29:
			if (data.bumped[1] - 1 >= 0 && data.world[data.bumped[1] - 1][data.bumped[0]] == 10) {
				data.world[data.bumped[1] - 1][data.bumped[0]] = 0;
				addcoin(data.bumped[0], data.bumped[1] - 1);
			}
			break;
		default:
			data.bumped[0] = data.bumped[1] = 0; data.bumploop = 0; break;
		}
	}
}

static void check_finish(int *finishp)
{
	if (marioy >= 184) *finishp = 1;
	if ((mariox + screenx + 16) / 16 == data.maxlength - 13) *finishp = 2;
}

/* ---------- cache (MT6261 HAL hal_tcmcache: CACHE at 0xa0530000, MPU at 0xa0540000) ---------- */
#define CACHE_CON	0xa0530000
#define CACHE_OP	0xa0530004
#define CACHE_EN	0xa053002c
#define MPU_RSTART(i)	(0xa0540000 + 4 * (i))
#define MPU_REND(i)	(0xa0540040 + 4 * (i))
#define MPU_CACHEABLE	(1 << 8)

static void cache_init(void)
{
	int i;
	writew(0, CACHE_EN);
	for (i = 0; i < 16; i++) { writel(0, MPU_RSTART(i)); writel(0, MPU_REND(i)); }
	/* code + data + 8-bit render buffer: cacheable */
	writel(0x00000000 | MPU_CACHEABLE, MPU_RSTART(0)); writel(0x00040000, MPU_REND(0));
	/* LCD framebuffer read by DMA: not cacheable */
	writel(0x00040000, MPU_RSTART(1)); writel(0x00080000, MPU_REND(1));
	/* rest of PSRAM (stack): cacheable */
	writel(0x00080000 | MPU_CACHEABLE, MPU_RSTART(2)); writel(0x00800000, MPU_REND(2));
	/* peripherals and SRAM: plain access */
	writel(0x70000000, MPU_RSTART(3)); writel(0x70010000, MPU_REND(3));
	writel(0x80000000, MPU_RSTART(4)); writel(0xfffff000, MPU_REND(4));
	writel(3, CACHE_OP);				/* invalidate all, CMDEN */
	writew(0x1f, CACHE_EN);
	writew((3 << 8) | (1 << 2) | (1 << 1) | 1, CACHE_CON);	/* 32 KiB, MPDEFEN, MPEN, MCEN */
}

/* ---------- main (gdc.c) ---------- */
void mario_main(void)
{
	int move, finish = 0, up, down = 0, jumploop = 0, i;
	uint32_t frames = 0, t_frame;

	writel(0xffe0 | 0x0008, 0xa0030004);	/* watchdog: longest timeout */
	writel(0x2200 | 1, 0xa0030000);
	writel(0x11530001, USCNT_CON);		/* microsecond counter */
	uart_init(0, 0);			/* USB serial; the host re-enumerates us */
#ifndef NO_CACHE
	cache_init();
#endif
	init();
	getinputs();
	memcpy(pal, palette565, sizeof(pal));
	force_full = 1;
	pal[BACKGROUND] = rgb(0x5c, 0x94, 0xfc);	/* the original's index 91 is green; use a sky blue */
	serial_puts("\nSuper Mario (DOS clone port): a/d move, w jump, b run, q reboot\n");
	while (!finish) {
		marioy = 140;
		if (screenx >= data.startpos[0] * 16) screenx = data.startpos[0] * 16; else screenx = 0;
		if (screenx > 0) marioy = data.startpos[1];
		mariox = 40; up = 0; move = 0; down = 0; data.sprite = 6;
		fillscreen(BACKGROUND);
		while (!finish) {
			t_frame = us_now();
			writel(0x1971, 0xa0030008);	/* kick the watchdog */
			update_keys();
			drawscreen(screenx, mariox, marioy);
			draw_cw();
			draw_bt();
			animate_mario(move);
			move_mario(&move);
			finish = move_cw(move, &up, &jumploop);
			if (finish == 3) break;
			finish = move_bt(move);
			if (marioy < 192) check_keybuf(&move, &finish, &up, &down, &jumploop);
			check_jump(&up, &down, &jumploop);
			check_blocks();
			check_finish(&finish);
			present();
			if ((++frames % 64) == 0)
				printf("frame %d screenx=%d mariox=%d marioy=%d keys=%c%c%c%c\n", (int)frames, screenx, mariox, marioy,
				       keybuf[KEY_LEFT] ? 'L' : '-', keybuf[KEY_RIGHT] ? 'R' : '-', keybuf[KEY_UP] ? 'U' : '-', keybuf[KEY_CTRL] ? 'B' : '-');
			while ((uint32_t)(us_now() - t_frame) < 10000)	/* ~100 fps */
				;
		}
		switch (finish) {
		case 1: finish = 0; serial_puts("fell\n"); time_delay(500); break;
		case 2: screenx = 0; finish = 0; serial_puts("LEVEL COMPLETE\n"); time_delay(3000); break;
		case 3:
			serial_puts("caught\n");
			marioy -= 20; data.sprite = 0;
			for (i = 0; marioy < 192; i++) {
				time_delay(5); marioy += i / 3;
				drawscreen(screenx, mariox, marioy); draw_cw(); draw_bt(); present();
				finish = 0;
			}
			time_delay(100);
			break;
		}
		free_cw();
		free_bt();
		getinputs();
	}
	exitgame();
}
