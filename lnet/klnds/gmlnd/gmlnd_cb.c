/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (c) 2003 Los Alamos National Laboratory (LANL)
 *
 *   This file is part of Lustre, http://www.lustre.org/
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/*
 *	This file implements the nal cb functions
 */


#include "gmnal.h"

ptl_err_t gmnal_cb_recv(lib_nal_t *libnal, void *private, lib_msg_t *cookie,
		   unsigned int niov, struct iovec *iov, size_t offset,
		   size_t mlen, size_t rlen)
{
        void            *buffer = NULL;
	gmnal_srxd_t	*srxd = (gmnal_srxd_t*)private;
	int		status = PTL_OK;
        size_t          msglen = mlen;
        size_t          nob;

	CDEBUG(D_TRACE, "gmnal_cb_recv libnal [%p], private[%p], cookie[%p], "
	       "niov[%d], iov [%p], offset["LPSZ"], mlen["LPSZ"], rlen["LPSZ"]\n",
	       libnal, private, cookie, niov, iov, offset, mlen, rlen);

	switch(srxd->type) {
	case(GMNAL_SMALL_MESSAGE):
		CDEBUG(D_INFO, "gmnal_cb_recv got small message\n");
		/* HP SFS 1380: Proactively change receives to avoid a receive
		 *  side occurrence of filling pkmap_count[].
		 */
		buffer = srxd->buffer;
		buffer += GMNAL_MSGHDR_SIZE;
		buffer += sizeof(ptl_hdr_t);

		while(niov--) {
			if (offset >= iov->iov_len) {
				offset -= iov->iov_len;
			} else {
                                nob = MIN (iov->iov_len - offset, msglen);
                                CDEBUG(D_INFO, "processing iov [%p] base [%p] "
                                       "offset [%d] len ["LPSZ"] to [%p] left "
                                       "["LPSZ"]\n", iov, iov->iov_base,
                                       offset, nob, buffer, msglen);
                                gm_bcopy(buffer, iov->iov_base + offset, nob);
                                buffer += nob;
                                msglen -= nob;
                                offset = 0;
			}
			iov++;
		}
		status = gmnal_small_rx(libnal, private, cookie);
	break;
	case(GMNAL_LARGE_MESSAGE_INIT):
		CDEBUG(D_INFO, "gmnal_cb_recv got large message init\n");
		status = gmnal_large_rx(libnal, private, cookie, niov, 
					 iov, offset, mlen, rlen);
	}

	CDEBUG(D_INFO, "gmnal_cb_recv gmnal_return status [%d]\n", status);
	return(status);
}

ptl_err_t gmnal_cb_recv_pages(lib_nal_t *libnal, void *private,
                              lib_msg_t *cookie, unsigned int kniov,
                              ptl_kiov_t *kiov, size_t offset, size_t mlen,
                              size_t rlen)
{
	gmnal_srxd_t	*srxd = (gmnal_srxd_t*)private;
	int		status = PTL_OK;
	char            *ptr = NULL;
	void            *buffer = NULL;


	CDEBUG(D_TRACE, "gmnal_cb_recv_pages libnal [%p],private[%p], "
	       "cookie[%p], kniov[%d], kiov [%p], offset["LPSZ"], mlen["LPSZ"], rlen["LPSZ"]\n",
	       libnal, private, cookie, kniov, kiov, offset, mlen, rlen);

	if (srxd->type == GMNAL_SMALL_MESSAGE) {
                size_t          msglen = mlen;
                size_t          nob;

		buffer = srxd->buffer;
		buffer += GMNAL_MSGHDR_SIZE;
		buffer += sizeof(ptl_hdr_t);

		/*
		 *	map each page and create an iovec for it
		 */
		while (kniov--) {
			/* HP SFS 1380: Proactively change receives to avoid a
			 *  receive side occurrence of filling pkmap_count[].
			 */
			CDEBUG(D_INFO, "processing kniov [%d] [%p]\n",
                               kniov, kiov);

			if (offset >= kiov->kiov_len) {
				offset -= kiov->kiov_len;
			} else {
                                nob = MIN (kiov->kiov_len - offset, msglen);
				CDEBUG(D_INFO, "kniov page [%p] len [%d] "
                                       "offset[%d]\n", kiov->kiov_page,
                                       kiov->kiov_len, kiov->kiov_offset);
				ptr = ((char *)kmap(kiov->kiov_page)) +
                                        kiov->kiov_offset;

                                CDEBUG(D_INFO, "processing ptr [%p] offset [%d] "
                                       "len ["LPSZ"] from [%p] left ["LPSZ"]\n",
                                       ptr, offset, nob, buffer, msglen);
                                gm_bcopy(buffer, ptr + offset, nob);
				kunmap(kiov->kiov_page);
                                buffer += nob;
                                msglen -= nob;
                                offset = 0;
                        }
                        kiov++;
		}
		CDEBUG(D_INFO, "calling gmnal_small_rx\n");
		status = gmnal_small_rx(libnal, private, cookie);
	}

	CDEBUG(D_INFO, "gmnal_return status [%d]\n", status);
	return(status);
}


ptl_err_t gmnal_cb_send(lib_nal_t *libnal, void *private, lib_msg_t *cookie,
                        ptl_hdr_t *hdr, int type, ptl_nid_t nid, ptl_pid_t pid,
                        unsigned int niov, struct iovec *iov, size_t offset,
                        size_t len)
{

	gmnal_data_t	*nal_data;
	void            *buffer = NULL;
	gmnal_stxd_t    *stxd = NULL;


	CDEBUG(D_TRACE, "gmnal_cb_send niov[%d] offset["LPSZ"] len["LPSZ
               "] nid["LPU64"]\n", niov, offset, len, nid);
	nal_data = libnal->libnal_data;
	if (!nal_data) {
		CERROR("no nal_data\n");
		return(PTL_FAIL);
	} else {
		CDEBUG(D_INFO, "nal_data [%p]\n", nal_data);
	}

	if (GMNAL_IS_SMALL_MESSAGE(nal_data, niov, iov, len)) {
                size_t msglen = len;
                size_t nob;

		CDEBUG(D_INFO, "This is a small message send\n");
		/*
		 * HP SFS 1380: With the change to gmnal_small_tx, need to get
		 * the stxd and do relevant setup here
		 */
		stxd = gmnal_get_stxd(nal_data, 1);
		CDEBUG(D_INFO, "stxd [%p]\n", stxd);
		/* Set the offset of the data to copy into the buffer */
		buffer = stxd->buffer + GMNAL_MSGHDR_SIZE + sizeof(ptl_hdr_t);
		while(niov--) {
			if (offset >= iov->iov_len) {
				offset -= iov->iov_len;
			} else {
                                nob = MIN (iov->iov_len - offset, msglen);
                                CDEBUG(D_INFO, "processing iov [%p] base [%p]"
                                      " offset [%d] len ["LPSZ"] to [%p] left"
                                      " ["LPSZ"]\n", iov, iov->iov_base,
                                      offset, nob, buffer, msglen);
                                gm_bcopy(iov->iov_base + offset, buffer, nob);
                                buffer += nob;
                                msglen -= nob;
                                offset = 0;
			}
			iov++;
		}
		gmnal_small_tx(libnal, private, cookie, hdr, type, nid, pid,
			       stxd,  len);
	} else {
		CERROR("Large message send is not supported\n");
		lib_finalize(libnal, private, cookie, PTL_FAIL);
		return(PTL_FAIL);
		gmnal_large_tx(libnal, private, cookie, hdr, type, nid, pid,
				niov, iov, offset, len);
	}
	return(PTL_OK);
}

ptl_err_t gmnal_cb_send_pages(lib_nal_t *libnal, void *private,
                              lib_msg_t *cookie, ptl_hdr_t *hdr, int type,
                              ptl_nid_t nid, ptl_pid_t pid, unsigned int kniov,
                              ptl_kiov_t *kiov, size_t offset, size_t len)
{

	gmnal_data_t	*nal_data;
	char            *ptr;
	void            *buffer = NULL;
	gmnal_stxd_t    *stxd = NULL;
	ptl_err_t       status = PTL_OK;

	CDEBUG(D_TRACE, "gmnal_cb_send_pages nid ["LPU64"] niov[%d] offset["
               LPSZ"] len["LPSZ"]\n", nid, kniov, offset, len);
	nal_data = libnal->libnal_data;
	if (!nal_data) {
		CERROR("no nal_data\n");
		return(PTL_FAIL);
	} else {
		CDEBUG(D_INFO, "nal_data [%p]\n", nal_data);
	}

	/* HP SFS 1380: Need to do the gm_bcopy after the kmap so we can kunmap
	 * more aggressively.  This is the fix for a livelock situation under
	 * load on ia32 that occurs when there are no more available entries in
	 * the pkmap_count array.  Just fill the buffer and let gmnal_small_tx
	 * put the headers in after we pass it the stxd pointer.
	 */
	stxd = gmnal_get_stxd(nal_data, 1);
	CDEBUG(D_INFO, "stxd [%p]\n", stxd);
	/* Set the offset of the data to copy into the buffer */
	buffer = stxd->buffer + GMNAL_MSGHDR_SIZE + sizeof(ptl_hdr_t);

	if (GMNAL_IS_SMALL_MESSAGE(nal_data, 0, NULL, len)) {
                size_t msglen = len;
                size_t nob;

		CDEBUG(D_INFO, "This is a small message send\n");

		while(kniov--) {
			CDEBUG(D_INFO, "processing kniov [%d] [%p]\n", kniov, kiov);
			if (offset >= kiov->kiov_len) {
				offset -= kiov->kiov_len;
			} else {
                                nob = MIN (kiov->kiov_len - offset, msglen);
				CDEBUG(D_INFO, "kniov page [%p] len [%d] offset[%d]\n",
				       kiov->kiov_page, kiov->kiov_len, 
				       kiov->kiov_offset);

				ptr = ((char *)kmap(kiov->kiov_page)) +
                                        kiov->kiov_offset;

                                CDEBUG(D_INFO, "processing ptr [%p] offset [%d]"
                                       " len ["LPSZ"] to [%p] left ["LPSZ"]\n",
                                       ptr, offset, nob, buffer, msglen);
                                gm_bcopy(ptr + offset, buffer, nob);
				kunmap(kiov->kiov_page);
                                buffer += nob;
                                msglen -= nob;
                                offset = 0;
			}
                        kiov++;
		}
		status = gmnal_small_tx(libnal, private, cookie, hdr, type, nid,
					pid, stxd, len);
	} else {
		int	i = 0;
		struct  iovec   *iovec = NULL, *iovec_dup = NULL;
		ptl_kiov_t *kiov_dup = kiov;

		PORTAL_ALLOC(iovec, kniov*sizeof(struct iovec));
		iovec_dup = iovec;
		CERROR("Large message send it is not supported yet\n");
		PORTAL_FREE(iovec, kniov*sizeof(struct iovec));
		return(PTL_FAIL);
		for (i=0; i<kniov; i++) {
			CDEBUG(D_INFO, "processing kniov [%d] [%p]\n", i, kiov);
			CDEBUG(D_INFO, "kniov page [%p] len [%d] offset[%d]\n",
			       kiov->kiov_page, kiov->kiov_len, 
			       kiov->kiov_offset);

			iovec->iov_base = kmap(kiov->kiov_page) 
					         + kiov->kiov_offset;
			iovec->iov_len = kiov->kiov_len;
                        iovec++;
                        kiov++;
		}
		gmnal_large_tx(libnal, private, cookie, hdr, type, nid, 
				pid, kniov, iovec, offset, len);
		for (i=0; i<kniov; i++) {
			kunmap(kiov_dup->kiov_page);
			kiov_dup++;
		}
		PORTAL_FREE(iovec_dup, kniov*sizeof(struct iovec));
	}
	return(status);
}

int gmnal_cb_dist(lib_nal_t *libnal, ptl_nid_t nid, unsigned long *dist)
{
	CDEBUG(D_TRACE, "gmnal_cb_dist\n");
	if (dist)
		*dist = 27;
	return(PTL_OK);
}
