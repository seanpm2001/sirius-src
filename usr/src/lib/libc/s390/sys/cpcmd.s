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
/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/


/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

/* C library -- cpcmd						*/
/* int cpcmd (char *cmd, char *result, size_t resize, int *resLen); */

	.file	"cpcmd.s"

#include <sys/asm_linkage.h>

#ifndef __GNUC__
	ANSI_PRAGMA_WEAK(cpcmd,function)
#endif

#include "SYS.h"

	SYSCALL(cpcmd)
	RET

	SET_SIZE(cpcmd)
