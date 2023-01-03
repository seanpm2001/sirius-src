/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1995-1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/* LINTLIBRARY */

/*
 * wget_wch.c
 *
 * XCurses Library
 *
 * Copyright 1990, 1995 by Mortice Kern Systems Inc.  All rights reserved.
 *
 */

#ifdef M_RCSID
#ifndef lint
static char rcsID[] =
"$Header: /team/ps/sun_xcurses/archive/local_changes/xcurses/src/lib/"
"libxcurses/src/libc/xcurses/rcs/wget_wch.c 1.7 1998/05/22 17:57:03 "
"cbates Exp $";
#endif
#endif

#include <private.h>
#include <m_wio.h>
#include <stdlib.h>

/*
 * Push a wide character back onto the input queue.
 *
 * XPG4 is silent as to whether wget_wch() and wgetch()
 * can both be used in an applicaton.  Assume they can,
 * in which case we have to push the multibyte equivalent
 * back onto the input queue.
 */
int
unget_wch(const wchar_t wc)
{
	int	i, len;
	char	mb[MB_LEN_MAX];

	if (!iqIsEmpty() || (len = wctomb(mb, wc)) < 0)
		return (ERR);

	for (i = 0; i < len; ++i) {
		iqAdd((unsigned char)mb[i]);
	}

	return (OK);
}

int
wget_wch(WINDOW *w, wint_t *wcp)
{
	cchar_t	cc;
	int	ch, oecho;
	t_wide_io	*wio;


	/*
	 * Disable echo temporarily, because we're using
	 * wgetch() to read in individual bytes and only
	 * want echo the resulting character, not the
	 * individual bytes composing the character.
	 */
	oecho = __m_set_echo(0);

	/*
	 * Input function is wgetch(), which takes a WINDOW * for
	 * a parameter.  The WINDOW * is used to set the "focus" by
	 * updatng and position the cursor in the relevant window and
	 * provide window specific settings.  Input for all windows
	 * comes from one stream (__m_screen->_if), which is normally
	 * the terminal, but can be redirected.
	 */
	wio = (t_wide_io *) __m_screen->_in;
	wio->object = w;

	/* Get the first byte or KEY_ value. */
	if ((ch = wgetch(w)) == ERR) {
		(void) __m_set_echo(oecho);
		return (ERR);
	}
	if (ch < __KEY_BASE) {
		(void) __m_set_echo(oecho);
		if (oecho) {
			(void) beep();
			return (ERR);
		} else {
			*wcp = ch;
			return (KEY_CODE_YES);
		}
	}

	/*
	 * Push the byte back onto the input stream so that
	 * it can be processed by __m_wio_get().
	 */
	iqPush(ch);

	/*
	 * Fetch a wide character from a narrow input stream.
	 * Invalid sequences are preserved as individual bytes.
	 * Handles insignificant and redundant shifts in the input
	 * stream.
	 */
	*wcp = m_wio_get(wio);

	/* Restore echo. */
	(void) __m_set_echo(oecho);

	/*
	 * Push any invalid multibyte sequence back onto the
	 * input stack, so that no data is lost, just in case
	 * the application mixes wide (wget_wch()) and narrow
	 * (wgetch()) input methods.
	 */
	while (wio->_next < wio->_size)
		iqPush(wio->_mb[--wio->_size]);

	/* Now echo wide character if necessary. */
	if ((__m_screen->_flags & S_ECHO) && *wcp != WEOF) {
		if (*wcp == L'\b') {
			if (w->_curx <= 0) {
				(void) beep();
				return (ch);
			}
			w->_curx--;
			(void) wdelch(w);
		} else {
			(void) __m_wc_cc(*wcp, &cc);
			(void) wadd_wch(w, &cc);
		}
		(void) wrefresh(w);
	}

	return (OK);
}
