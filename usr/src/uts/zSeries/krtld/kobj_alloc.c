/*------------------------------------------------------------------*/
/* 								    */
/* Name        - kobj_alloc.					    */
/* 								    */
/* Function    - Miscellaneous storage allocation type routines.    */
/* 								    */
/* Name	       - Neale Ferguson					    */
/* 								    */
/* Date        - March, 2007.					    */
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

#include <sys/kobj.h>
#include <sys/bootconf.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>


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
/* Name		- bop_alloc.                                        */
/*                                                                  */
/* Function	-                                                   */
/*		                               		 	    */
/*------------------------------------------------------------------*/

caddr_t
bop_alloc(struct bootops *bop, caddr_t addr, size_t size, int align) 
{
	return(bop->bsys_alloc(addr, size, align));
}

/*========================= End of Function ========================*/
