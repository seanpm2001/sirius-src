#ifndef	_RELOCATE_AMD64_MSG_DOT_H
#define	_RELOCATE_AMD64_MSG_DOT_H

#ifndef	__lint

typedef int	Msg;

#define	MSG_ORIG(x)	&__sgs_msg[x]

extern	const char *	_sgs_msg(Msg);

#define	MSG_INTL(x)	_sgs_msg(x)


#define	MSG_R_AMD64_NONE	1
#define	MSG_R_AMD64_NONE_SIZE	12

#define	MSG_R_AMD64_64	14
#define	MSG_R_AMD64_64_SIZE	10

#define	MSG_R_AMD64_PC32	25
#define	MSG_R_AMD64_PC32_SIZE	12

#define	MSG_R_AMD64_GOT32	38
#define	MSG_R_AMD64_GOT32_SIZE	13

#define	MSG_R_AMD64_PLT32	52
#define	MSG_R_AMD64_PLT32_SIZE	13

#define	MSG_R_AMD64_COPY	66
#define	MSG_R_AMD64_COPY_SIZE	12

#define	MSG_R_AMD64_GLOB_DATA	79
#define	MSG_R_AMD64_GLOB_DATA_SIZE	17

#define	MSG_R_AMD64_JUMP_SLOT	97
#define	MSG_R_AMD64_JUMP_SLOT_SIZE	17

#define	MSG_R_AMD64_RELATIVE	115
#define	MSG_R_AMD64_RELATIVE_SIZE	16

#define	MSG_R_AMD64_GOTPCREL	132
#define	MSG_R_AMD64_GOTPCREL_SIZE	16

#define	MSG_R_AMD64_32	149
#define	MSG_R_AMD64_32_SIZE	10

#define	MSG_R_AMD64_32S	160
#define	MSG_R_AMD64_32S_SIZE	11

#define	MSG_R_AMD64_16	172
#define	MSG_R_AMD64_16_SIZE	10

#define	MSG_R_AMD64_PC16	183
#define	MSG_R_AMD64_PC16_SIZE	12

#define	MSG_R_AMD64_8	196
#define	MSG_R_AMD64_8_SIZE	9

#define	MSG_R_AMD64_PC8	206
#define	MSG_R_AMD64_PC8_SIZE	11

#define	MSG_R_AMD64_DTPMOD64	218
#define	MSG_R_AMD64_DTPMOD64_SIZE	16

#define	MSG_R_AMD64_DTPOFF64	235
#define	MSG_R_AMD64_DTPOFF64_SIZE	16

#define	MSG_R_AMD64_TPOFF64	252
#define	MSG_R_AMD64_TPOFF64_SIZE	15

#define	MSG_R_AMD64_TLSGD	268
#define	MSG_R_AMD64_TLSGD_SIZE	13

#define	MSG_R_AMD64_TLSLD	282
#define	MSG_R_AMD64_TLSLD_SIZE	13

#define	MSG_R_AMD64_DTPOFF32	296
#define	MSG_R_AMD64_DTPOFF32_SIZE	16

#define	MSG_R_AMD64_GOTTPOFF	313
#define	MSG_R_AMD64_GOTTPOFF_SIZE	16

#define	MSG_R_AMD64_TPOFF32	330
#define	MSG_R_AMD64_TPOFF32_SIZE	15

#define	MSG_R_AMD64_PC64	346
#define	MSG_R_AMD64_PC64_SIZE	12

#define	MSG_R_AMD64_GOTPC32	359
#define	MSG_R_AMD64_GOTPC32_SIZE	15

#define	MSG_R_AMD64_GOTOFF64	375
#define	MSG_R_AMD64_GOTOFF64_SIZE	16

#define	MSG_R_AMD64_GOT64	392
#define	MSG_R_AMD64_GOT64_SIZE	13

#define	MSG_R_AMD64_GOTPCREL64	406
#define	MSG_R_AMD64_GOTPCREL64_SIZE	18

#define	MSG_R_AMD64_GOTPC64	425
#define	MSG_R_AMD64_GOTPC64_SIZE	15

#define	MSG_R_AMD64_GOTPLT64	441
#define	MSG_R_AMD64_GOTPLT64_SIZE	16

#define	MSG_R_AMD64_PLTOFF64	458
#define	MSG_R_AMD64_PLTOFF64_SIZE	16

#define	MSG_R_AMD64_SIZE32	475
#define	MSG_R_AMD64_SIZE32_SIZE	14

#define	MSG_R_AMD64_SIZE64	490
#define	MSG_R_AMD64_SIZE64_SIZE	14

static const char __sgs_msg[505] = { 
/*    0 */ 0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x4e,
/*   10 */ 0x4f,  0x4e,  0x45,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,
/*   20 */ 0x34,  0x5f,  0x36,  0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,
/*   30 */ 0x36,  0x34,  0x5f,  0x50,  0x43,  0x33,  0x32,  0x00,  0x52,  0x5f,
/*   40 */ 0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x47,  0x4f,  0x54,  0x33,
/*   50 */ 0x32,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,
/*   60 */ 0x50,  0x4c,  0x54,  0x33,  0x32,  0x00,  0x52,  0x5f,  0x41,  0x4d,
/*   70 */ 0x44,  0x36,  0x34,  0x5f,  0x43,  0x4f,  0x50,  0x59,  0x00,  0x52,
/*   80 */ 0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x47,  0x4c,  0x4f,
/*   90 */ 0x42,  0x5f,  0x44,  0x41,  0x54,  0x41,  0x00,  0x52,  0x5f,  0x41,
/*  100 */ 0x4d,  0x44,  0x36,  0x34,  0x5f,  0x4a,  0x55,  0x4d,  0x50,  0x5f,
/*  110 */ 0x53,  0x4c,  0x4f,  0x54,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,
/*  120 */ 0x36,  0x34,  0x5f,  0x52,  0x45,  0x4c,  0x41,  0x54,  0x49,  0x56,
/*  130 */ 0x45,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,
/*  140 */ 0x47,  0x4f,  0x54,  0x50,  0x43,  0x52,  0x45,  0x4c,  0x00,  0x52,
/*  150 */ 0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x33,  0x32,  0x00,
/*  160 */ 0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x33,  0x32,
/*  170 */ 0x53,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,
/*  180 */ 0x31,  0x36,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,
/*  190 */ 0x5f,  0x50,  0x43,  0x31,  0x36,  0x00,  0x52,  0x5f,  0x41,  0x4d,
/*  200 */ 0x44,  0x36,  0x34,  0x5f,  0x38,  0x00,  0x52,  0x5f,  0x41,  0x4d,
/*  210 */ 0x44,  0x36,  0x34,  0x5f,  0x50,  0x43,  0x38,  0x00,  0x52,  0x5f,
/*  220 */ 0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x44,  0x54,  0x50,  0x4d,
/*  230 */ 0x4f,  0x44,  0x36,  0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,
/*  240 */ 0x36,  0x34,  0x5f,  0x44,  0x54,  0x50,  0x4f,  0x46,  0x46,  0x36,
/*  250 */ 0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,
/*  260 */ 0x54,  0x50,  0x4f,  0x46,  0x46,  0x36,  0x34,  0x00,  0x52,  0x5f,
/*  270 */ 0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x54,  0x4c,  0x53,  0x47,
/*  280 */ 0x44,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,
/*  290 */ 0x54,  0x4c,  0x53,  0x4c,  0x44,  0x00,  0x52,  0x5f,  0x41,  0x4d,
/*  300 */ 0x44,  0x36,  0x34,  0x5f,  0x44,  0x54,  0x50,  0x4f,  0x46,  0x46,
/*  310 */ 0x33,  0x32,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,
/*  320 */ 0x5f,  0x47,  0x4f,  0x54,  0x54,  0x50,  0x4f,  0x46,  0x46,  0x00,
/*  330 */ 0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x54,  0x50,
/*  340 */ 0x4f,  0x46,  0x46,  0x33,  0x32,  0x00,  0x52,  0x5f,  0x41,  0x4d,
/*  350 */ 0x44,  0x36,  0x34,  0x5f,  0x50,  0x43,  0x36,  0x34,  0x00,  0x52,
/*  360 */ 0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x47,  0x4f,  0x54,
/*  370 */ 0x50,  0x43,  0x33,  0x32,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,
/*  380 */ 0x36,  0x34,  0x5f,  0x47,  0x4f,  0x54,  0x4f,  0x46,  0x46,  0x36,
/*  390 */ 0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,
/*  400 */ 0x47,  0x4f,  0x54,  0x36,  0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,
/*  410 */ 0x44,  0x36,  0x34,  0x5f,  0x47,  0x4f,  0x54,  0x50,  0x43,  0x52,
/*  420 */ 0x45,  0x4c,  0x36,  0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,
/*  430 */ 0x36,  0x34,  0x5f,  0x47,  0x4f,  0x54,  0x50,  0x43,  0x36,  0x34,
/*  440 */ 0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x47,
/*  450 */ 0x4f,  0x54,  0x50,  0x4c,  0x54,  0x36,  0x34,  0x00,  0x52,  0x5f,
/*  460 */ 0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x50,  0x4c,  0x54,  0x4f,
/*  470 */ 0x46,  0x46,  0x36,  0x34,  0x00,  0x52,  0x5f,  0x41,  0x4d,  0x44,
/*  480 */ 0x36,  0x34,  0x5f,  0x53,  0x49,  0x5a,  0x45,  0x33,  0x32,  0x00,
/*  490 */ 0x52,  0x5f,  0x41,  0x4d,  0x44,  0x36,  0x34,  0x5f,  0x53,  0x49,
/*  500 */ 0x5a,  0x45,  0x36,  0x34,  0x00 };

#else	/* __lint */


typedef char *	Msg;

extern	const char *	_sgs_msg(Msg);

#define MSG_ORIG(x)	x
#define MSG_INTL(x)	x

#define	MSG_R_AMD64_NONE	"R_AMD64_NONE"
#define	MSG_R_AMD64_NONE_SIZE	12

#define	MSG_R_AMD64_64	"R_AMD64_64"
#define	MSG_R_AMD64_64_SIZE	10

#define	MSG_R_AMD64_PC32	"R_AMD64_PC32"
#define	MSG_R_AMD64_PC32_SIZE	12

#define	MSG_R_AMD64_GOT32	"R_AMD64_GOT32"
#define	MSG_R_AMD64_GOT32_SIZE	13

#define	MSG_R_AMD64_PLT32	"R_AMD64_PLT32"
#define	MSG_R_AMD64_PLT32_SIZE	13

#define	MSG_R_AMD64_COPY	"R_AMD64_COPY"
#define	MSG_R_AMD64_COPY_SIZE	12

#define	MSG_R_AMD64_GLOB_DATA	"R_AMD64_GLOB_DATA"
#define	MSG_R_AMD64_GLOB_DATA_SIZE	17

#define	MSG_R_AMD64_JUMP_SLOT	"R_AMD64_JUMP_SLOT"
#define	MSG_R_AMD64_JUMP_SLOT_SIZE	17

#define	MSG_R_AMD64_RELATIVE	"R_AMD64_RELATIVE"
#define	MSG_R_AMD64_RELATIVE_SIZE	16

#define	MSG_R_AMD64_GOTPCREL	"R_AMD64_GOTPCREL"
#define	MSG_R_AMD64_GOTPCREL_SIZE	16

#define	MSG_R_AMD64_32	"R_AMD64_32"
#define	MSG_R_AMD64_32_SIZE	10

#define	MSG_R_AMD64_32S	"R_AMD64_32S"
#define	MSG_R_AMD64_32S_SIZE	11

#define	MSG_R_AMD64_16	"R_AMD64_16"
#define	MSG_R_AMD64_16_SIZE	10

#define	MSG_R_AMD64_PC16	"R_AMD64_PC16"
#define	MSG_R_AMD64_PC16_SIZE	12

#define	MSG_R_AMD64_8	"R_AMD64_8"
#define	MSG_R_AMD64_8_SIZE	9

#define	MSG_R_AMD64_PC8	"R_AMD64_PC8"
#define	MSG_R_AMD64_PC8_SIZE	11

#define	MSG_R_AMD64_DTPMOD64	"R_AMD64_DTPMOD64"
#define	MSG_R_AMD64_DTPMOD64_SIZE	16

#define	MSG_R_AMD64_DTPOFF64	"R_AMD64_DTPOFF64"
#define	MSG_R_AMD64_DTPOFF64_SIZE	16

#define	MSG_R_AMD64_TPOFF64	"R_AMD64_TPOFF64"
#define	MSG_R_AMD64_TPOFF64_SIZE	15

#define	MSG_R_AMD64_TLSGD	"R_AMD64_TLSGD"
#define	MSG_R_AMD64_TLSGD_SIZE	13

#define	MSG_R_AMD64_TLSLD	"R_AMD64_TLSLD"
#define	MSG_R_AMD64_TLSLD_SIZE	13

#define	MSG_R_AMD64_DTPOFF32	"R_AMD64_DTPOFF32"
#define	MSG_R_AMD64_DTPOFF32_SIZE	16

#define	MSG_R_AMD64_GOTTPOFF	"R_AMD64_GOTTPOFF"
#define	MSG_R_AMD64_GOTTPOFF_SIZE	16

#define	MSG_R_AMD64_TPOFF32	"R_AMD64_TPOFF32"
#define	MSG_R_AMD64_TPOFF32_SIZE	15

#define	MSG_R_AMD64_PC64	"R_AMD64_PC64"
#define	MSG_R_AMD64_PC64_SIZE	12

#define	MSG_R_AMD64_GOTPC32	"R_AMD64_GOTPC32"
#define	MSG_R_AMD64_GOTPC32_SIZE	15

#define	MSG_R_AMD64_GOTOFF64	"R_AMD64_GOTOFF64"
#define	MSG_R_AMD64_GOTOFF64_SIZE	16

#define	MSG_R_AMD64_GOT64	"R_AMD64_GOT64"
#define	MSG_R_AMD64_GOT64_SIZE	13

#define	MSG_R_AMD64_GOTPCREL64	"R_AMD64_GOTPCREL64"
#define	MSG_R_AMD64_GOTPCREL64_SIZE	18

#define	MSG_R_AMD64_GOTPC64	"R_AMD64_GOTPC64"
#define	MSG_R_AMD64_GOTPC64_SIZE	15

#define	MSG_R_AMD64_GOTPLT64	"R_AMD64_GOTPLT64"
#define	MSG_R_AMD64_GOTPLT64_SIZE	16

#define	MSG_R_AMD64_PLTOFF64	"R_AMD64_PLTOFF64"
#define	MSG_R_AMD64_PLTOFF64_SIZE	16

#define	MSG_R_AMD64_SIZE32	"R_AMD64_SIZE32"
#define	MSG_R_AMD64_SIZE32_SIZE	14

#define	MSG_R_AMD64_SIZE64	"R_AMD64_SIZE64"
#define	MSG_R_AMD64_SIZE64_SIZE	14

#endif	/* __lint */

#endif
