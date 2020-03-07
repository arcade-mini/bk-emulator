#include "defines.h"
#include "scr.h"
#include "intl.h"
#include "libretro-defs.h"

#define HOR 512
#define VERT 512
unsigned short framebuf[VERT][HOR];

int cur_shift = 0;
int cur_width = 0;	/* 0 = narrow, !0 = wide */

/* Scan lines */
// The scan lines must be in 8-bit palettized form, as they have
// to be re-blitted to the screen with their new meaning if the
// palette changes.

unsigned char dirty[1024];


enum {
	Black = 0,
	White = 0xffff,
	Red = 0xf800,
	Green = 0x07e0,
	Blue = 0x001f,
	Yellow = Red | Green,
	Magenta = Red | Blue,
	Cyan = Green | Blue,

// FIXME
	DarkRed = Red,
	LightBlue = Blue,
	Brown = Yellow,
	Violet = Cyan
};

/*
 * The colors are ordered in the HW order, not in the "Basic" order.
 * To convert, switch colors 1 and 3.
 */
static const unsigned short palettes[16][5] = {
{ Black, Blue, Green, Red, White },   /* BK0010 Colors */
{ Black, Yellow, Magenta, Red, White },
{ Black, Cyan, Blue, Magenta, White },
{ Black, Green, Cyan, Yellow, White },
{ Black, Magenta, Cyan, White, White },
{ Black, White, White, White, White },
{ Black, DarkRed, Brown, Red, White },
{ Black, Green, Cyan, Yellow, White },
{ Black, Violet, LightBlue, Magenta, White },
{ Black, Yellow, LightBlue, DarkRed, White },  /* Almost the same as the next one */
{ Black, Yellow, Violet, Red, White },
{ Black, Cyan, Yellow, Red, White },          /* CSI-DOS colors */
{ Black, Red, Green, Cyan, White },
{ Black, Cyan, Yellow, White, White },
{ Black, Yellow, Green, White, White },
{ Black, Cyan, Green, White, White },
};

unsigned scr_dirty = 0;

/*
 * Flushes a word into a statically defined scan line;
 * scrolling and line doubling are done during blitting. bufno is 0 or 1,
 * address is relative to the video buffer.
 */
int scr_write(int bufno, c_addr addr, d_word wrd)
{
	int offset, dest_y;
	int i;
	offset = addr * 8;
	dest_y = offset / 512;
	i = 256*bufno+dest_y;
	if (!dirty[i]) {
		dirty[i] = 1;
		scr_dirty++;
	}
	return OK;
}

/* BK-0010 screen refresh - no palettes */
static void scr_refresh_bk0010(unsigned shift, unsigned full);

/* BK-0011 screen refresh - double size, exact interlacing */
static void scr_refresh_bk0011_2(unsigned shift, unsigned full);

void (*scr_refresh)(unsigned, unsigned);

void bk_scr_init() {
    static char init_done = 0;
    int i;
    if (init_done) return;
    init_done = 1;

    for (i = 0; i < 512; i++) {
	dirty[i] = 0;
    }
    scr_common_init();
    scr_refresh = bkmodel ?scr_refresh_bk0011_2 :
	scr_refresh_bk0010;
}

/* * switches between color and B/W.
 */

void scr_mark_dirty () {
	memset (dirty, 1, sizeof(dirty));
	scr_dirty = sizeof(dirty);
}

static void
blit_line_double (int line, int palno, unsigned short *src) {
	const unsigned short *palette = palettes[palno];
	unsigned short *dl = framebuf[line * 2];
	unsigned short *dl2 = framebuf[line * 2 + 1];
	int i;

	if (cflag) {
		for (i = 0; i < 256; i++) {
			int bit = (src[i / 8] >> ((i % 8 * 2))) & 3;
			unsigned short c = palette[bit];
			dl[2 * i] = c;
			dl2[2 * i] = c;
			dl[2 * i + 1] = c;
			dl2[2 * i + 1] = c;
		}
	} else {
		for (i = 0; i < 512; i++) {
			int bit = (src[i / 16] >> ((i % 16))) & 1;
			unsigned short c = palette[bit * 4];
			dl[i] = c;
			dl2[i] = c;
		}
	}
}

static void
blit_line_single (int line, int palno, unsigned short *src) {
	const unsigned short *palette = palettes[palno];
	unsigned short *dl = framebuf[line];
	int i;

	if (cflag) {
		for (i = 0; i < 256; i++) {
			int bit = (src[i / 8] >> ((i % 8 * 2))) & 3;
			unsigned short c = palette[bit];
			dl[2 * i] = c;
			dl[2 * i + 1] = c;
		}
	} else {
		for (i = 0; i < 512; i++) {
			int bit = (src[i / 16] >> ((i % 16))) & 1;
			unsigned short c = palette[bit * 4];
			dl[i] = c;
		}
	}
}

static void
black_out_low() {
	memset (framebuf[128], 0, sizeof (framebuf[0]) * 384);
}

/*
 * Screen refresh for BK-0010 does not need to know about buffers or
 * palettes.
 */
static void
scr_refresh_bk0010(unsigned shift, unsigned full) 
{
	int blit_all = shift != cur_shift || cur_width != full;
	int i;

	/* If more than a few lines changed, no point
	 * doing separate UpdateRect's for each line.
	 */
	int nlines = full ? 256 : 64;
	for (i = 0; i < nlines; i++) {
		int line = (i + shift) & 0xFF;
		if (dirty[line] | blit_all) { 
			unsigned short *in = get_vram_line (0, line);
			blit_line_double (i, 0, in);
		}
	}
	// Only the first 256 lines are used
	memset(dirty, 0, 256);
	if (!full && cur_width) {
		black_out_low();
	}
	cur_width = full;
	cur_shift = shift;
	scr_dirty = 0;
}

static void
scr_refresh_bk0011_2(unsigned shift, unsigned full) {
		int blit_all = change_req || shift != cur_shift || cur_width != full;
	int i;

	/* If more than a few lines changed, no point
	 * doing separate UpdateRect's for each line.
	 */
	int nlines = full ? 512 : 128;
	for (i = 0; i < nlines; i++) {
		// The next line is the reverse mapping of vertbase
		int j = i / 2;
		int line = (j + shift) & 0xFF;
		unsigned physline = 256*req_page[2*j+(i&1)]+line;
		if (dirty[physline] | blit_all) {
			blit_line_single (i, req_palette[2*j+(i&1)],
					  get_vram_line (req_page[2*j+(i&1)], line));
		}
	}
	memset(dirty, 0, 512);
	if (!full && cur_width) {
		black_out_low();
	}
	cur_width = full;
	cur_shift = shift;
	scr_dirty = 0;
	change_req >>= 1;
}

void scr_flush() {
	if (scr_dirty || change_req)
		scr_refresh((tty_scroll - 0330) & 0377, tty_scroll & 01000);
}


void maybe_scr_flush() {
	scr_flush();
}
