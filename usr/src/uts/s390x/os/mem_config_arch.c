/*------------------------------------------------------------------*/
/* 								    */
/* Name        - mem_config_arch.c				    */
/* 								    */
/* Function    -                                                    */
/* 								    */
/* Name	       - Neale Ferguson					    */
/* 								    */
/* Date        - July, 2006  					    */
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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <vm/page.h>
#include <sys/mem_config.h>

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

/*====================== End of Global Variables ===================*/

/*------------------------------------------------------------------*/
/*                                                                  */
/* Name		- arch_kphysm_del_span_ok.                          */
/*                                                                  */
/* Function	-                                                   */
/*		                               		 	    */
/*------------------------------------------------------------------*/

int
arch_kphysm_del_span_ok(pfn_t base, pgcnt_t npgs)
{
	ASSERT(npgs != 0);
	return (0);
}

/*========================= End of Function ========================*/

/*------------------------------------------------------------------*/
/*                                                                  */
/* Name		- arch_kphysm_relocate.                             */
/*                                                                  */
/* Function	- Tell upper layers that we don't support relocat-  */
/*		  ing kpm physical memory.             		    */
/*		                               		 	    */
/*------------------------------------------------------------------*/

int
arch_kphysm_relocate(pfn_t base, pgcnt_t npgs)
{
	ASSERT(npgs != 0);
	return (ENOTSUP);
}

/*========================= End of Function ========================*/

/*------------------------------------------------------------------*/
/*                                                                  */
/* Name		- arch_kphysm_del_supported.                        */
/*                                                                  */
/* Function	- Tell upper layers that we support deleting kpm    */
/*		  physical memory.             		 	    */
/*		                               		 	    */
/*------------------------------------------------------------------*/

int
arch_kphysm_del_supported(void)
{
	return (1);
}

/*========================= End of Function ========================*/
