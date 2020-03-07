/*
 * This file is part of 'pdp', a PDP-11 simulator.
 *
 * For information contact:
 *
 *   Computer Science House
 *   Attn: Eric Edwards
 *   Box 861
 *   25 Andrews Memorial Drive
 *   Rochester, NY 14623
 *
 * Email:  mag@potter.csh.rit.edu
 * FTP:    ftp.csh.rit.edu:/pub/csh/mag/pdp.tar.Z
 * 
 * Copyright 1994, Eric A. Edwards
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Eric A. Edwards makes no
 * representations about the suitability of this software
 * for any purpose.  It is provided "as is" without expressed
 * or implied warranty.
 */

/*
 * main.c -  Main routine and setup.
 */


#include "defines.h"
#include "scr.h"
#include "conf.h"
#include "intl.h"
#include <locale.h>

/*
 * Globals.
 */

struct bk_state current_state;

const char * printer_file = 0;

/*
 * At start-up, bkmodel == 0, 1, or 2 means BK-0010, 3 means BK-0011M.
 * During simulation, bkmodel == 0 is BK-0010, 1 is BK-0011M.
 */

/* Standard path and ROMs for basic hardware configurations */

char * romdir = "./roms"; /* default ROM path */
const char *const monitor10rom = "MONIT10.ROM";
const char *const diskrom = "DISK_327.ROM";
const char *const bos11rom = "B11M_BOS.ROM";
const char *const bos11extrom = "B11M_EXT.ROM";
const char *const basic11arom = "BAS11M_0.ROM";
const char *const basic11brom = "BAS11M_1.ROM";

/*
 * Command line flags and variables.
 */

flag_t traceflag;	/* print all instruction addresses */
FILE * tracefile;	/* trace goes here */

const char *rompath10, *rompath12, *rompath16;

static int checkpoint(d_word pc);

/*
 * sim_init() - Initialize the cpu registers.
 */

void
sim_init()
{
	int x;

	for ( x = 0; x < 8; ++x ) {
		pdp.regs[x] = 0;
	}
	pdp.ir = 0;
	pdp.psw = 0200;

	pdp_ram_map = 0x0000ffff;
}


int cybuf[1024];
int cybufidx = 0;

void
addtocybuf(int val) {
	cybuf[cybufidx] = val;
	cybufidx = (cybufidx+1) % 1024;
}

int
run_cpu_until(register pdp_regs *p, long long max_ticks) {
#ifndef LIBRETRO
	static char buf[80];
#endif

	while (ticks < max_ticks) {
		d_word oldpc;

		register int result;		/* result of execution */
		int result2;			/* result of error handling */
		int rtt = 0;			/* rtt don't trap yet flag */

		addtocybuf(p->regs[PC]);

		/*
		 * Fetch and execute the instruction.
		 */

#ifndef LIBRETRO
		if (traceflag) {
			disas(p->regs[PC], buf);
			if (tracefile) fprintf(tracefile, "%s\t%s\n", buf, state(p));
			else printf("%s\n", buf);
		}
#endif
		result = ll_word( p, p->regs[PC], &p->ir );
		oldpc = p->regs[PC];
		p->regs[PC] += 2;
		if (result == OK) {
			result = itab[p->ir>>6]( p );
			timing(p);
		}
		
		if (bkmodel && ticks >= ticks_timer) {
                       if (timer_intr_enabled) {
                               ev_register(TIMER_PRI, service, 0, 0100);
                       }
                       ticks_timer += half_frame_delay;
               }
		/*
		 * Mop up the mess.
		 */

		if ( result != OK ) {
			switch( result ) {
			case BUS_ERROR:			/* vector 4 */
				ticks += 64;
				/* Fallthrough */
			case ODD_ADDRESS:
				fprintf( stderr, _(" pc=%06o, last branch @ %06o\n"),
					oldpc, last_branch );
				result2 = service( (d_word) 04 );
				break;
			case CPU_ILLEGAL:		/* vector 10 */
#undef VERBOSE_ILLEGAL
#ifdef VERBOSE_ILLEGAL
				disas(oldpc, buf);
				fprintf( stderr, 
				_("Illegal inst. %s, last branch @ %06o\n"),
					buf, last_branch );
#endif
				result2 = service( (d_word) 010 );
				break;
			case CPU_BPT:			/* vector 14 */
				result2 = service( (d_word) 014 );
				break;
			case CPU_EMT:			/* vector 30 */
				result2 = service( (d_word) 030 );
				break;
			case CPU_TRAP:			/* vector 34 */
				result2 = service( (d_word) 034 );
				break;
			case CPU_IOT:			/* vector 20 */
				result2 = service( (d_word) 020 );
				break;
			case CPU_WAIT:
				in_wait_instr = 1;
				result2 = OK;
				break;
			case CPU_RTT:
				rtt = 1;
				result2 = OK;
				break;
			case CPU_HALT:
				io_stop_happened = 4;
				result2 = service( (d_word) 004 );
				break;
			default:
				fprintf( stderr, _("\nUnexpected return.\n") );
				fprintf( stderr, _("exec=%d pc=%06o ir=%06o\n"),
					result, oldpc, p->ir );
				return 0;
			}
			if ( result2 != OK ) {
				fprintf( stderr, _("\nDouble trap @ %06o.\n"), oldpc);
				lc_word(0177716, &p->regs[PC]);
				p->regs[PC] &= 0177400;
				/* p->regs[SP] = 01000;*/	/* whatever */
			}
		}

		if (( p->psw & 020) && (rtt == 0 )) {		/* trace bit */
			if ( service((d_word) 014 ) != OK ) {
				fprintf( stderr, _("\nDouble trap @ %06o.\n"), p->regs[PC]);
				lc_word(0177716, &p->regs[PC]);
				p->regs[PC] &= 0177400;
				p->regs[SP] = 01000;	/* whatever */
			}
		}
		rtt = 0;
		p->total++;

		if (nflag)
		  sound_flush();

				/*
		 * See if execution should be stopped.  If so
		 * stop running, otherwise look for events
		 * to fire.
		 */

		if ( stop_it ) {
			fprintf( stderr, _("\nExecution interrupted.\n") );
			return 0;
		} else {
			unsigned priority = ( p->psw >> 5) & 7;
			if ( pending_interrupts && priority != 7 ) {
				ev_fire( priority );
			}
		}
		if (checkpoint(p->regs[PC])) {
			return 0;
		}
	}
	
	return 1;
}

/*
 * intr_hand() - Handle SIGINT during execution by breaking
 * back to user interface at the end of the current instruction.
 */

void intr_hand()
{
	stop_it = 1;
}

static int checkpoint(d_word pc)
{
    switch(pc) {
    case 0116256:
		if (fake_tape && !bkmodel) {
			fprintf(stderr, "Faking write file to tape\n");
			fake_write_file();
		}
		break;
    case 0116712:
		if (fake_tape && !bkmodel) {
			fprintf(stderr, _("Simulating tune-up sequence\n"));
			fake_tuneup_sequence();
		}
		break;
    case 0117260:
		if (fake_tape && !bkmodel) {
			fprintf(stderr, _("Simulating reading array with tune-up\n"));
			fake_array_with_tuneup();
		}
		break;
    case 0117376:
		if (fake_tape && !bkmodel) {
			fake_read_strobe();
		}
		break;
    case 0160250:
		if (fake_disk)
			fake_disk_io();
		break;
    case 0160372:
		if (fake_disk)
			fake_sector_io();
		break;
    case 0162246:
		fprintf(stderr, "INDEX ");
		break;
    case 0162304:
		fprintf(stderr, "err\n");
		break;
    case 0162312:
		fprintf(stderr, "good\n");
		break;
    case 0160746:
		fprintf(stderr, "WORK\n");
		break;
    case 0162012:
		fprintf(stderr, "FINDH\n");
		break;
    case 0161732:
		fprintf(stderr, "STREAD\n");
		break;
    case 0163004:
		fprintf(stderr, "GOTO00\n");
		break;
    case 0161610:
		fprintf(stderr, "RDSEC\n");
		break;
    case 0163072:
		fprintf(stderr, "FRESEC\n");
		break;
    }
    return (pc == breakpoint);
}

int load_file(FILE *f, int addr) {
  	int len;
	d_byte c1, c2;

  	c1 = fgetc(f);
	c2 = fgetc(f);
	if (-1 == addr)
	    addr = c2 << 8 | c1;
	c1 = fgetc(f);
	c2 = fgetc(f);
	len = c2 << 8 | c1;
	int addr0 = addr;
	fprintf(stderr, _("Reading file into %06o... "), addr);
	/* the file is in little-endian format */
	while (len > 0 && !feof(f)) {
		c1 = fgetc(f);
		c2 = fgetc(f);
		if (OK != sc_word(addr, c2 << 8 | c1)) {
		    break;
		}
		addr += 2;
		len -= 2;
	}
	fprintf(stderr, _("Done.\nLast filled address is %06o\n"), addr - 2);
	return addr0;
}

static void run_game(int addr0) {
        if (addr0 < 01000) {
		lc_word(00776, &pdp.regs[PC]);
	} else
		pdp.regs[PC] = 01000;
	sc_word(0320, 3);
}

void load_and_run(FILE *f) {
	run_game(load_file(f, -1));
}


static int load_bin(const unsigned char *data, size_t sz) {
  	int ptr;

        if (sz < 4)
                return -1;

        int addr = data[1] << 8 | data[0];
	int len = data[3] << 8 | data[2];
        if (len > sz - 4 || len < 0)
                len = sz - 4;
	fprintf(stderr, _("Reading file into %06o... "), addr);
	/* the file is in little-endian format */
	for (ptr = 0; ptr < len; ptr += 2) {
		if (OK != sc_word(addr + ptr,
                                  data[5+ptr] << 8 | data[4+ptr])) {
		    break;
		}
	}
	fprintf(stderr, _("Done.\nLast filled address is %06o\n"), addr + ptr - 2);
	return addr;
}

void load_and_run_bin(const void *data, size_t sz) {
        run_game(load_bin(data, sz));
}
