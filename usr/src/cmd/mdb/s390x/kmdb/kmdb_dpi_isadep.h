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
/*                                                                  */
/* Copyright 2008 Sine Nomine Associates.                           */
/* All rights reserved.                                             */
/* Use is subject to license terms.                                 */
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _KMDB_DPI_ISADEP_H
#define	_KMDB_DPI_ISADEP_H

#include <mdb/mdb_v9util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	DPI_TOP_WINDOW		(-1)		/* matches cwp */

extern int kmdb_dpi_get_win_register(int, const char *, kreg_t *);
extern int kmdb_dpi_get_cpu_register(int, int, const char *, kreg_t *);

extern int kmdb_dpi_set_win_register(int, const char *, kreg_t);
extern int kmdb_dpi_set_cpu_register(int, int, const char *, kreg_t);

extern int kmdb_dpi_get_rwin(int, int, struct rwindow *);
extern int kmdb_dpi_get_nwin(int);

extern void kmdb_dpi_handle_fault(kreg_t, kreg_t, kreg_t, kreg_t, int);

extern void kmdb_dpi_kernpanic(int cpuid);

#ifdef __cplusplus
}
#endif

#endif /* _KMDB_DPI_ISADEP_H */
