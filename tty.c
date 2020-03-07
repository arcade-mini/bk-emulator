/*
 * Originally
 * Copyright 1994, Eric A. Edwards
 * After very heavy modifications
 * Copyright 1995-2003 Leonid A. Broukhis

 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Leonid A. Broukhis makes no
 * representations about the suitability of this software
 * for any purpose.  It is provided "as is" without expressed
 * or implied warranty.
 */

/* 
 * tty.c - BK-0010/11M registers 177660-177664.
 * SDL event handling should really be in another file.
 */

#include "defines.h"
#include "tty.h"
#include <ctype.h>
#include "intl.h"

#define TTY_VECTOR      060     /* standard vector for console */
#define TTY_VECTOR2     0274    /* AR2 (ALT) vector for console */

/*
 * Defines for the registers.
 */

#define TTY_IE          0100
#define TTY_DONE        0200

/* magic numbers for special keys */

d_word tty_reg;
d_word tty_data;

static int tty_pending_int = 0;

/*
 * tty_init() - Initialize the BK-0010 keyboard
 */

void tty_init()
{
	unsigned short old_scroll = tty_scroll;
	tty_reg = 0;
	tty_data = 0;
	tty_pending_int = 0;
	tty_scroll = 01330;
	timer_intr_enabled = 0;
	if (old_scroll != tty_scroll) {
		scr_dirty = 1;
	}
	key_pressed = 0100;
}

/*
 * tty_read() - Handle the reading of a "keyboard" register.
 */

int
tty_read( addr, word )
c_addr addr;
d_word *word;
{
	d_word offset = addr & 07;

	switch( offset ) {
	case 0:
		*word = tty_reg;
		break;
	case 2:
		*word = tty_data;
		tty_reg &= ~TTY_DONE;
		break;
	case 4:
		*word = tty_scroll;
		break;
	}
	return OK;
}

/*
 * tty_write() - Handle a write to one of the "keyboard" registers.
 */

int
tty_write(c_addr addr, d_word word )
{
	d_word offset = addr & 07;
	d_word old_scroll;

	switch( offset ) {
	case 0:
		/* only let them set IE */
		tty_reg = (tty_reg & ~TTY_IE) | (word & TTY_IE);
		break;
	case 2:
		if (bkmodel) {
			flag_t old_timer_enb = timer_intr_enabled;
			scr_param_change((word >> 8) & 0xF, word >> 15);
			timer_intr_enabled = (word & 040000) == 0;
			if (timer_intr_enabled != old_timer_enb) {
				fprintf(stderr, _("Timer %s\n"), timer_intr_enabled ? _("ON") : _("OFF"));
			}
			if (!timer_intr_enabled)
				pending_interrupts &= ~(1<<TIMER_PRI);
		} else {
			fprintf(stderr, _("Writing to kbd data register, "));
			return BUS_ERROR;
		}
		break;
	case 4:
		old_scroll = tty_scroll;
		tty_scroll = word & 01377;
		if (old_scroll != tty_scroll) {
			scr_dirty = 1;
		}
		break;
	}
	return OK;
}

/*
 * kl_bwrite() - Handle a byte write.
 */

int
tty_bwrite(c_addr addr, d_byte byte )
{
	d_word offset = addr & 07;
	d_word old_scroll;

	switch( offset ) {
	case 0:
		/* only let them set IE */
		tty_reg = (tty_reg & ~TTY_IE) | (byte & TTY_IE);
		break;
	case 1:
		break;
	case 2:
		fprintf(stderr, _("Writing to kbd data register, "));
		return BUS_ERROR;
	case 3:
		if (bkmodel) {
			flag_t old_timer_enb = timer_intr_enabled;
			scr_param_change(byte & 0xF, byte >> 7);
			timer_intr_enabled = (byte & 0100) == 0;
			if (timer_intr_enabled != old_timer_enb) {
				fprintf(stderr, "Timer %s\n", timer_intr_enabled ? "ON" : "OFF");
			}
			if (!timer_intr_enabled)
				pending_interrupts &= ~(1<<TIMER_PRI);
		} else {
			fprintf(stderr, _("Writing to kbd data register, "));
			return BUS_ERROR;
		}
		break;
	case 4:
		old_scroll = tty_scroll;
		tty_scroll = (tty_scroll & 0xFF00) | (byte & 0377);
		if (old_scroll != tty_scroll) {
			scr_dirty = 1;
		}
		break;
	case 5:
		old_scroll = tty_scroll;
		tty_scroll = (byte << 8) & 2 | tty_scroll & 0377;
		if (old_scroll != tty_scroll) {
			scr_dirty = 1;
		}
		break;
	}
	return OK;
}

/*
 * tty_finish()
 */

static int tty_finish( d_word c )
{
	service(( c & 0200 ) ? TTY_VECTOR2 : TTY_VECTOR);
	tty_pending_int = 0;
	return OK;
}

static void stop_key() {
    io_stop_happened = 4;
    service(04);
}

/*
 * 9 Numpad is mapped to 265(Let's pad it to R button, BACKSPACE)
 * 1 Numpad is mapped to 257 (Pad it to L, TAB)

 * SPACE is mapped to 32
 * RETURN is mapped to 13
 * LCTRL : 306
 * LALT : 308
*/

void tty_keyevent(int c) {
	if(c == -1) {
		key_pressed = 0100;
		return;
	}
	/* modifier keys handling */
	switch (c) {
	case TTY_STOP:
		stop_key();     /* STOP is NMI */
		return;
	case TTY_NOTHING:
		return;
	case TTY_RESET | 0200:
		lc_word(0177716, &pdp.regs[PC]);
		pdp.regs[PC] &= 0177400;
		return;
	}
	tty_reg |= TTY_DONE;
	tty_data = c & 0177;
	key_pressed = 0;
	if ( !tty_pending_int && !(tty_reg & TTY_IE) ) {
		ev_register(TTY_PRI, tty_finish, (unsigned long) 0, c);
		tty_pending_int = c & 0200 ? TTY_VECTOR2 : TTY_VECTOR;
	}
}


