/*------------------------------------------------------------------*/
/* 								    */
/* Name        - xlate.c    					    */
/* 								    */
/* Function    - Provide simple ASCII<->EBCDIC translation.         */
/* 								    */
/* Name	       - Neale Ferguson					    */
/* 								    */
/* Date        - January, 2007					    */
/* 								    */
/*------------------------------------------------------------------*/

/*------------------------------------------------------------------*/
/*                   L I C E N S E                                  */
/*------------------------------------------------------------------*/

/*==================================================================*/
/* 								    */
/* CDDL HEADER START						    */
/* 								    */
/* The contents of this file are subject to the terms of the	    */
/* Common Development and Distribution License                      */
/* (the "License").  You may not use this file except in compliance */
/* with the License.						    */
/* 								    */
/* You can obtain a copy of the license at: 			    */
/* - usr/src/OPENSOLARIS.LICENSE, or,				    */
/* - http://www.opensolaris.org/os/licensing.			    */
/* See the License for the specific language governing permissions  */
/* and limitations under the License.				    */
/* 								    */
/* When distributing Covered Code, include this CDDL HEADER in each */
/* file and include the License file at usr/src/OPENSOLARIS.LICENSE.*/
/* If applicable, add the following below this CDDL HEADER, with    */
/* the fields enclosed by brackets "[]" replaced with your own      */
/* identifying information: 					    */
/* Portions Copyright [yyyy] [name of copyright owner]		    */
/* 								    */
/* CDDL HEADER END						    */
/*                                                                  */
/* Copyright 2008 Sine Nomine Associates.                           */
/* All rights reserved.                                             */
/* Use is subject to license terms.                                 */
/* 								    */
/*==================================================================*/

/*------------------------------------------------------------------*/
/*                 D e f i n e s                                    */
/*------------------------------------------------------------------*/


/*========================= End of Defines =========================*/

/*------------------------------------------------------------------*/
/*                 I n c l u d e s                                  */
/*------------------------------------------------------------------*/

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/asm_linkage.h>
#include <sys/vtrace.h>
#include <sys/machthread.h>
#include <sys/clock.h>
#include <sys/privregs.h>

#if !defined(lint)
#include "assym.h"
#endif	/* lint */

/*========================= End of Includes ========================*/

/*------------------------------------------------------------------*/
/*                 T y p e d e f s                                  */
/*------------------------------------------------------------------*/


/*========================= End of Typedefs ========================*/

/*------------------------------------------------------------------*/
/*                E x t e r n a l   R e f e r e n c e s             */
/*------------------------------------------------------------------*/


/*=================== End of External References ===================*/

/*------------------------------------------------------------------*/
/*                   P r o t o t y p e s                            */
/*------------------------------------------------------------------*/


/*========================= End of Prototypes ======================*/

/*------------------------------------------------------------------*/
/*                 G l o b a l   V a r i a b l e s                  */
/*------------------------------------------------------------------*/

	.section	".rodata"
.Lcatbl:
	.byte	0x00, 0x01, 0x02, 0x03, 0x37, 0x2d, 0x2e, 0x2f
	.byte	0x16, 0x05, 0x25, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte	0x10, 0x11, 0x12, 0x13, 0xb6, 0xb5, 0x32, 0x26
	.byte	0x18, 0x19, 0x1c, 0x27, 0x07, 0x1d, 0x1e, 0x1f
	.byte	0x40, 0x5a, 0x7f, 0x7b, 0x5b, 0x6c, 0x50, 0x7d
	.byte	0x4d, 0x5d, 0x5c, 0x4e, 0x6b, 0x60, 0x4b, 0x61
	.byte	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7
	.byte	0xf8, 0xf9, 0x7a, 0x5e, 0x4c, 0x7e, 0x6e, 0x6f
	.byte	0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7
	.byte	0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6
	.byte	0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6
	.byte	0xe7, 0xe8, 0xe9, 0xad, 0xe0, 0xbd, 0x5f, 0x6d
	.byte	0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
	.byte	0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96
	.byte	0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6
	.byte	0xa7, 0xa8, 0xa9, 0xc0, 0x4f, 0xd0, 0xa1, 0x3f
	.byte	0x68, 0xdc, 0x51, 0x42, 0x43, 0x44, 0x47, 0x48
	.byte	0x52, 0x53, 0x54, 0x57, 0x56, 0x58, 0x63, 0x67
	.byte	0x71, 0x9c, 0x9e, 0xcb, 0xcc, 0xcd, 0xdb, 0xdd
	.byte	0xdf, 0xec, 0xfc, 0x4a, 0xb1, 0xb2, 0x3e, 0xb4
	.byte	0x45, 0x55, 0xce, 0xde, 0x49, 0x69, 0x9a, 0x9b
	.byte	0xab, 0x9f, 0xb0, 0xb8, 0xb7, 0xaa, 0x8a, 0x8b
	.byte	0x3c, 0x3d, 0x62, 0x6a, 0x64, 0x65, 0x66, 0x20
	.byte	0x21, 0x22, 0x70, 0x23, 0x72, 0x73, 0x74, 0xbe
	.byte	0x76, 0x77, 0x78, 0x80, 0x24, 0x15, 0x8c, 0x8d
	.byte	0x8e, 0xff, 0x06, 0x17, 0x28, 0x29, 0x9d, 0x2a
	.byte	0x2b, 0x2c, 0x09, 0x0a, 0xac, 0xba, 0xae, 0xaf
	.byte	0x1b, 0x30, 0x31, 0xfa, 0x1a, 0x33, 0x34, 0x35
	.byte	0x36, 0x59, 0x08, 0x38, 0xbc, 0x39, 0xa0, 0xbf
	.byte	0xca, 0x3a, 0xfe, 0x3b, 0x04, 0xcf, 0xda, 0x14
	.byte	0xee, 0x8f, 0x46, 0x75, 0xfd, 0xeb, 0xe1, 0xed
	.byte	0x90, 0xef, 0xb3, 0xfb, 0xb9, 0xea, 0xbb, 0x41

.Lcetbl:
	.byte	0x00, 0x01, 0x02, 0x03, 0xec, 0x09, 0xca, 0x1c
	.byte	0xe2, 0xd2, 0xd3, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte	0x10, 0x11, 0x12, 0x13, 0xef, 0x0a, 0x08, 0xcb
	.byte	0x18, 0x19, 0xdc, 0xd8, 0x1a, 0x1d, 0x1e, 0x1f
	.byte	0xb7, 0xb8, 0xb9, 0xbb, 0xc4, 0x0a, 0x17, 0x1b
	.byte	0xcc, 0xcd, 0xcf, 0xd0, 0xd1, 0x05, 0x06, 0x07
	.byte	0xd9, 0xda, 0x16, 0xdd, 0xde, 0xdf, 0xe0, 0x04
	.byte	0xe3, 0xe5, 0xe9, 0xeb, 0xb0, 0xb1, 0x9e, 0x7f
	.byte	0x20, 0xff, 0x83, 0x84, 0x85, 0xa0, 0xf2, 0x86
	.byte	0x87, 0xa4, 0x9b, 0x2e, 0x3c, 0x28, 0x2b, 0x7c
	.byte	0x26, 0x82, 0x88, 0x89, 0x8a, 0xa1, 0x8c, 0x8b
	.byte	0x8d, 0xe1, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x5e
	.byte	0x2d, 0x2f, 0xb2, 0x8e, 0xb4, 0xb5, 0xb6, 0x8f
	.byte	0x80, 0xa5, 0xb3, 0x2c, 0x25, 0x5f, 0x3e, 0x3f
	.byte	0xba, 0x90, 0xbc, 0xbd, 0xbe, 0xf3, 0xc0, 0xc1
	.byte	0xc2, 0x60, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22
	.byte	0xc3, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67
	.byte	0x68, 0x69, 0xae, 0xaf, 0xc6, 0xc7, 0xc8, 0xf1
	.byte	0xf8, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70
	.byte	0x71, 0x72, 0xa6, 0xa7, 0x91, 0xce, 0x92, 0xa9
	.byte	0xe6, 0x7e, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78
	.byte	0x79, 0x7a, 0xad, 0xa8, 0xd4, 0x5b, 0xd6, 0xd7
	.byte	0xaa, 0x9c, 0x9d, 0xfa, 0x9f, 0x15, 0x14, 0xac
	.byte	0xab, 0xfc, 0xd5, 0xfe, 0xe4, 0x5d, 0xbf, 0xe7
	.byte	0x7b, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47
	.byte	0x48, 0x49, 0xe8, 0x93, 0x94, 0x95, 0xa2, 0xed
	.byte	0x7d, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50
	.byte	0x51, 0x52, 0xee, 0x96, 0x81, 0x97, 0xa3, 0x98
	.byte	0x5c, 0xf6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58
	.byte	0x59, 0x5a, 0xfd, 0xf5, 0x99, 0xf7, 0xf0, 0xf9
	.byte	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37
	.byte	0x38, 0x39, 0xdb, 0xfb, 0x9a, 0xf4, 0xea, 0xc9

	.section	".text"

/*====================== End of Global Variables ===================*/

/*------------------------------------------------------------------*/
/*                                                                  */
/* Name		- a2e.                                              */
/*                                                                  */
/* Function	- Translate from ASCII to EBCDIC.                   */
/*		                               		 	    */
/*------------------------------------------------------------------*/

#if defined(lint)
 
/* ARGSUSED */
void
a2e(const void *src, size_t length)
{}
 
#else /* lint */
 
	ENTRY(a2e)
	ltgr	%r3,%r3			// Count > 0?
	bzr	%r14			// No... just return

	larl	%r4,.Lcatbl
0:
	cghi	%r3,256			// Less than or 256 bytes?
	jle	1f			// Yes... Go do it

	tr	0(256,%r2),0(%r4)	// Do a block at a time
	aghi	%r2,256
	aghi	%r3,-256
	j	0b

1:	ltgr	%r3,%r3			// Anything left
	jz	2f			// No... Exit stage left

	larl	%r5,.Lca2e		// Point at EX
	aghi	%r3,-1			// Adjust for EX
	ex	%r3,0(%r5)		// Translate remaining bytes

2:	br	%r14
	.align 8
	
.Lca2e:	
	tr	0(1,%r2),0(%r4)

	SET_SIZE(a2e)

#endif

/*========================= End of Function ========================*/

/*------------------------------------------------------------------*/
/*                                                                  */
/* Name		- e2a.                                              */
/*                                                                  */
/* Function	- Translate from EBCDIC to ASCII.                   */
/*		                               		 	    */
/*------------------------------------------------------------------*/

#if defined(lint)
 
/* ARGSUSED */
void
e2a(const void *src, size_t length)
{}
 
#else /* lint */
 
	ENTRY(e2a)
	ltgr	%r3,%r3			// Count > 0?
	bzr	%r14			// No... just return

	larl	%r4,.Lcetbl
0:
	cghi	%r3,256			// Less than or 256 bytes?
	jle	1f			// Yes... Go do it

	tr	0(256,%r2),0(%r4)	// Do a block at a time
	aghi	%r2,256
	aghi	%r3,-256
	j	0b

1:	ltgr	%r3,%r3			// Anything left
	jz	2f			// No... Exit stage left

	larl	%r5,.Lce2a		// Point at EX
	aghi	%r3,-1			// Adjust for EX
	ex	%r3,0(%r5)		// Translate remaining bytes

2:	br	%r14
	.align 8
	
.Lce2a:	
	tr	0(1,%r2),0(%r4)

	SET_SIZE(e2a)

#endif /* lint */

/*========================= End of Function ========================*/
