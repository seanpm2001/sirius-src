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
 *	Copyright (c) 2001 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#include <sys/stack.h>

/*
 * These crt*.o modules are provided as the bare minimum required
 * from a crt*.o for inclusion in building low level system
 * libraries.  The are only be to included in libraries which
 * contain *no* C++ code and want to avoid the startup code
 * that the C++ runtime has introduced into the crt*.o modules.
 *
 * For further details - see bug#4433015
 */
	.file		"crtn.s"

/*
 * _init function epilogue
 */
	.section	".init"
	ahi	%r15,SA(MINFRAME32)
	lm	%r6,%r14,24(%r15)
	br	%r14

/*
 * _fini function epilogue
 */
	.section	".fini"
	ahi	%r15,SA(MINFRAME32)
	lm	%r6,%r14,24(%r15)
	br	%r14