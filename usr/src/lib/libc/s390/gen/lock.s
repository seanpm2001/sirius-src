/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License                  
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
 *
 * Copyright 2008 Sine Nomine Associates.
 * All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

	.file	"lock.s"

#include <sys/asm_linkage.h>

/*
 * lock_try(lp)
 *	- returns non-zero on success.
 */
	ENTRY(_lock_try)
	xi	0(%r2),0xff
	jz	0f
	lhi	%r2,0
	br	%r14
0:	
	lhi	%r2,1
	br	%r14
	SET_SIZE(_lock_try)

/*
 * lock_clear(lp)
 *	- clear lock.
 */
	ENTRY(_lock_clear)
	mvi	0(%r2),0
	br	%r14
	SET_SIZE(_lock_clear)
