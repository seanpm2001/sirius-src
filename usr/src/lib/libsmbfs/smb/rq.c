/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: rq.c,v 1.4 2004/12/13 00:25:23 lindak Exp $
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <sysexits.h>
#include <libintl.h>

#include <netsmb/smb_lib.h>

extern uid_t real_uid, eff_uid;


int
smb_rq_init(struct smb_ctx *ctx, uchar_t cmd, size_t rpbufsz,
    struct smb_rq **rqpp)
{
	struct smb_rq *rqp;

	rqp = malloc(sizeof (*rqp));
	if (rqp == NULL)
		return (ENOMEM);
	bzero(rqp, sizeof (*rqp));
	rqp->rq_cmd = cmd;
	rqp->rq_ctx = ctx;
	mb_init(&rqp->rq_rq, M_MINSIZE);
	mb_init(&rqp->rq_rp, rpbufsz);
	*rqpp = rqp;
	return (0);
}

void
smb_rq_done(struct smb_rq *rqp)
{
	mb_done(&rqp->rq_rp);
	mb_done(&rqp->rq_rq);
	free(rqp);
}

void
smb_rq_wend(struct smb_rq *rqp)
{
	if (rqp->rq_rq.mb_count & 1)
		smb_error(dgettext(TEXT_DOMAIN,
		    "smbrq_wend: odd word count\n"), 0);
	rqp->rq_wcount = rqp->rq_rq.mb_count / 2;
	rqp->rq_rq.mb_count = 0;
}

int
smb_rq_dmem(struct mbdata *mbp, const char *src, size_t size)
{
	struct mbuf *m;
	char  *dst;
	int cplen, error;

	if (size == 0)
		return (0);
	m = mbp->mb_cur;
	if ((error = m_getm(m, size, &m)) != 0)
		return (error);
	while (size > 0) {
		cplen = M_TRAILINGSPACE(m);
		if (cplen == 0) {
			m = m->m_next;
			continue;
		}
		if (cplen > (int)size)
			cplen = size;
		dst = mtod(m, char *) + m->m_len;
		nls_mem_toext(dst, src, cplen);
		size -= cplen;
		src += cplen;
		m->m_len += cplen;
		mbp->mb_count += cplen;
	}
	mbp->mb_pos = mtod(m, char *) + m->m_len;
	mbp->mb_cur = m;
	return (0);
}

int
smb_rq_dstring(struct mbdata *mbp, const char *s)
{
	return (smb_rq_dmem(mbp, s, strlen(s) + 1));
}

int
smb_rq_simple(struct smb_rq *rqp)
{
	struct smbioc_rq krq;
	struct mbdata *mbp;
	char *data;
	int i;

	mbp = smb_rq_getrequest(rqp);
	m_lineup(mbp->mb_top, &mbp->mb_top);
	data = mtod(mbp->mb_top, char *);
	bzero(&krq, sizeof (krq));
	krq.ioc_cmd = rqp->rq_cmd;
	krq.ioc_twc = rqp->rq_wcount;
	krq.ioc_twords = data;
	krq.ioc_tbc = mbp->mb_count;
	krq.ioc_tbytes = data + rqp->rq_wcount * 2;

	mbp = smb_rq_getreply(rqp);
	krq.ioc_rpbufsz = mbp->mb_top->m_maxlen;
	krq.ioc_rpbuf = mtod(mbp->mb_top, char *);
	seteuid(eff_uid);
	if (ioctl(rqp->rq_ctx->ct_fd, SMBIOC_REQUEST, &krq) == -1) {
		seteuid(real_uid); /* and back to real user */
		return (errno);
	}
	mbp->mb_top->m_len = krq.ioc_rwc * 2 + krq.ioc_rbc;
	rqp->rq_wcount = krq.ioc_rwc;
	rqp->rq_bcount = krq.ioc_rbc;
	seteuid(real_uid); /* and back to real user */
	return (0);
}


int
smb_t2_request(struct smb_ctx *ctx, int setupcount, uint16_t *setup,
	const char *name,
	int tparamcnt, void *tparam,
	int tdatacnt, void *tdata,
	int *rparamcnt, void *rparam,
	int *rdatacnt, void *rdata,
	int *buffer_oflow)
{
	smbioc_t2rq_t *krq;
	int i;
	char *pass;


	krq = (smbioc_t2rq_t *)malloc(sizeof (smbioc_t2rq_t));
	bzero(krq, sizeof (*krq));

	if (setupcount < 0 || setupcount >= SMB_MAXSETUPWORDS) {
		/* Bogus setup count, or too many setup words */
		return (EINVAL);
	}
	for (i = 0; i < setupcount; i++)
		krq->ioc_setup[i] = setup[i];
	krq->ioc_setupcnt = setupcount;
	strcpy(krq->ioc_name, name);
	krq->ioc_tparamcnt = tparamcnt;
	krq->ioc_tparam = tparam;
	krq->ioc_tdatacnt = tdatacnt;
	krq->ioc_tdata = tdata;

	krq->ioc_rparamcnt = *rparamcnt;
	krq->ioc_rdatacnt = *rdatacnt;
	krq->ioc_rparam = rparam;
	krq->ioc_rdata  = rdata;

	seteuid(eff_uid);
	if (ioctl(ctx->ct_fd, SMBIOC_T2RQ, krq) == -1) {
		seteuid(real_uid); /* and back to real user */
		return (errno);
	}

	*rparamcnt = krq->ioc_rparamcnt;
	*rdatacnt = krq->ioc_rdatacnt;
	*buffer_oflow = (krq->ioc_rpflags2 & SMB_FLAGS2_ERR_STATUS) &&
	    (krq->ioc_error == NT_STATUS_BUFFER_OVERFLOW);
	seteuid(real_uid); /* and back to real user */
	free(krq);
	return (0);
}