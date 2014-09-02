/*
 * $Id$
 *
 * Copyright (C) 2005 iptelorg GmbH
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 *  2014-08-25  split from io_wait.h (bogdan)
 */

/*!
 * \file
 * \brief io wait looping and triggering functions
 */


#ifndef _io_wait_loop_h
#define _io_wait_loop_h

#include "io_wait.h"


#ifdef HANDLE_IO_INLINE
/*!\brief generic handle io routine
 * this must be defined in the including file
 * (faster then registering a callback pointer)
 *
 * \param fm pointer to a fd hash entry
 * \param idx index in the fd_array (or -1 if not known)
 * \return return: -1 on error
 *          0 on EAGAIN or when by some other way it is known that no more
 *            io events are queued on the fd (the receive buffer is empty).
 *            Usefull to detect when there are no more io events queued for
 *            sigio_rt, epoll_et, kqueue.
 *         >0 on successfull read from the fd (when there might be more io
 *            queued -- the receive buffer might still be non-empty)
 */
inline static int handle_io(struct fd_map* fm, int idx,int event_type);
#else
static int handle_io(struct fd_map* fm, int idx,int event_type) {
	return 0;
}
#endif



/*! \brief io_wait_loop_x style function
 * wait for io using poll()
 * \param h io_wait handle
 * \param t timeout in s
 * \param repeat if !=0 handle_io will be called until it returns <=0
 * \return number of IO events handled on success (can be 0), -1 on error
 */
inline static int io_wait_loop_poll(io_wait_h* h, int t, int repeat)
{
	int n, r;
	int ret;
again:
		ret=n=poll(h->fd_array, h->fd_no, t*1000);
		if (n==-1){
			if (errno==EINTR) goto again; /* signal, ignore it */
			else{
				LM_ERR("poll: %s [%d]\n", strerror(errno), errno);
				goto error;
			}
		}
		for (r=0; (r<h->fd_no) && n; r++){
			if (h->fd_array[r].revents & POLLOUT) {
				n--;
				/* sanity checks */
				if ((h->fd_array[r].fd >= h->max_fd_no)||
						(h->fd_array[r].fd < 0)){
					LM_CRIT("bad fd %d (no in the 0 - %d range)\n",
							h->fd_array[r].fd, h->max_fd_no);
					/* try to continue anyway */
					h->fd_array[r].events=0; /* clear the events */
					continue;
				}
				handle_io(get_fd_map(h, h->fd_array[r].fd),r,IO_WATCH_WRITE);
			} else if (h->fd_array[r].revents & (POLLIN|POLLERR|POLLHUP)){
				n--;
				/* sanity checks */
				if ((h->fd_array[r].fd >= h->max_fd_no)||
						(h->fd_array[r].fd < 0)){
					LM_CRIT("bad fd %d (no in the 0 - %d range)\n",
							h->fd_array[r].fd, h->max_fd_no);
					/* try to continue anyway */
					h->fd_array[r].events=0; /* clear the events */
					continue;
				}

				while((handle_io(get_fd_map(h, h->fd_array[r].fd), r,IO_WATCH_READ) > 0)
						 && repeat);
			}
		}
error:
	return ret;
}



#ifdef HAVE_SELECT
/*! \brief wait for io using select */
inline static int io_wait_loop_select(io_wait_h* h, int t, int repeat)
{
	fd_set sel_set;
	int n, ret;
	struct timeval timeout;
	int r;

again:
		sel_set=h->master_set;
		timeout.tv_sec=t;
		timeout.tv_usec=0;
		ret=n=select(h->max_fd_select+1, &sel_set, 0, 0, &timeout);
		if (n<0){
			if (errno==EINTR) goto again; /* just a signal */
			LM_ERR("select: %s [%d]\n", strerror(errno), errno);
			n=0;
			/* continue */
		}
		/* use poll fd array */
		for(r=0; (r<h->max_fd_no) && n; r++){
			if (FD_ISSET(h->fd_array[r].fd, &sel_set)){
				while((handle_io(get_fd_map(h, h->fd_array[r].fd), r,IO_WATCH_READ)>0)
						&& repeat);
				n--;
			}
		};
	return ret;
}
#endif



#ifdef HAVE_EPOLL
inline static int io_wait_loop_epoll(io_wait_h* h, int t, int repeat)
{
	int n, r;

again:
		n=epoll_wait(h->epfd, h->ep_array, h->fd_no, t*1000);
		if (n==-1){
			if (errno==EINTR) goto again; /* signal, ignore it */
			else{
				LM_ERR("epoll_wait(%d, %p, %d, %d): %s [%d]\n",
						h->epfd, h->ep_array, h->fd_no, t*1000,
						strerror(errno), errno);
				goto error;
			}
		}
#if 0
		if (n>1){
			for(r=0; r<n; r++){
				LM_ERR("ep_array[%d]= %x, %p\n",
						r, h->ep_array[r].events, h->ep_array[r].data.ptr);
			}
		}
#endif
		for (r=0; r<n; r++) {
			if (h->ep_array[r].events & EPOLLOUT) {
				handle_io((struct fd_map*)h->ep_array[r].data.ptr,-1,IO_WATCH_WRITE);
			} else if (h->ep_array[r].events & (EPOLLIN|EPOLLERR|EPOLLHUP)){
				while((handle_io((struct fd_map*)h->ep_array[r].data.ptr,-1,IO_WATCH_READ)>0)
					&& repeat);
			}else{
				LM_ERR("unexpected event %x on %d/%d, data=%p\n",
					h->ep_array[r].events, r+1, n, h->ep_array[r].data.ptr);
			}
		}
error:
	return n;
}
#endif



#ifdef HAVE_KQUEUE
inline static int io_wait_loop_kqueue(io_wait_h* h, int t, int repeat)
{
	int n, r;
	struct timespec tspec;

	tspec.tv_sec=t;
	tspec.tv_nsec=0;
again:
		n=kevent(h->kq_fd, h->kq_changes, h->kq_nchanges,  h->kq_array,
					h->fd_no, &tspec);
		if (n==-1){
			if (errno==EINTR) goto again; /* signal, ignore it */
			else{
				LM_ERR("kevent: %s [%d]\n", strerror(errno), errno);
				goto error;
			}
		}
		h->kq_nchanges=0; /* reset changes array */
		for (r=0; r<n; r++){
#ifdef EXTRA_DEBUG
			LM_DBG("event %d/%d: fd=%d, udata=%lx, flags=0x%x\n",
					r, n, h->kq_array[r].ident, (long)h->kq_array[r].udata,
					h->kq_array[r].flags);
#endif
			if (h->kq_array[r].flags & EV_ERROR){
				/* error in changes: we ignore it, it can be caused by
				   trying to remove an already closed fd: race between
				   adding smething to the changes array, close() and
				   applying the changes */
				LM_INFO("kevent error on fd %u: %s [%ld]\n",
							(unsigned int)h->kq_array[r].ident,
							strerror(h->kq_array[r].data),
							(long)h->kq_array[r].data);
			}else /* READ/EOF */
				while((handle_io((struct fd_map*)h->kq_array[r].udata, -1,IO_WATCH_READ)>0)
						&& repeat);
		}
error:
	return n;
}
#endif



#ifdef HAVE_SIGIO_RT
/*! \brief sigio rt version has no repeat (it doesn't make sense)*/
inline static int io_wait_loop_sigio_rt(io_wait_h* h, int t)
{
	int n;
	int ret;
	struct timespec ts;
	siginfo_t siginfo;
	int sigio_band;
	int sigio_fd;
	struct fd_map* fm;


	ret=1; /* 1 event per call normally */
	ts.tv_sec=t;
	ts.tv_nsec=0;
	if (!sigismember(&h->sset, h->signo) || !sigismember(&h->sset, SIGIO)){
		LM_CRIT("the signal mask is not properly set!\n");
		goto error;
	}

again:
	n=sigtimedwait(&h->sset, &siginfo, &ts);
	if (n==-1){
		if (errno==EINTR) goto again; /* some other signal, ignore it */
		else if (errno==EAGAIN){ /* timeout */
			ret=0;
			goto end;
		}else{
			LM_ERR("sigtimed_wait %s [%d]\n", strerror(errno), errno);
			goto error;
		}
	}
	if (n!=SIGIO){
#ifdef SIGINFO64_WORKARROUND
		/* on linux siginfo.si_band is defined as long in userspace
		 * and as int kernel => on 64 bits things will break!
		 * (si_band will include si_fd, and si_fd will contain
		 *  garbage)
		 *  see /usr/src/linux/include/asm-generic/siginfo.h and
		 *      /usr/include/bits/siginfo.h
		 * -- andrei */
		if (sizeof(siginfo.si_band)>sizeof(int)){
			sigio_band=*((int*)&siginfo.si_band);
			sigio_fd=*(((int*)&siginfo.si_band)+1);
		}else
#endif
		{
			sigio_band=siginfo.si_band;
			sigio_fd=siginfo.si_fd;
		}
		if (siginfo.si_code==SI_SIGIO){
			/* old style, we don't know the event (linux 2.2.?) */
			LM_WARN("old style sigio interface\n");
			fm=get_fd_map(h, sigio_fd);
			/* we can have queued signals generated by fds not watched
			 * any more, or by fds in transition, to a child => ignore them*/
			if (fm->type)
				handle_io(fm, -1,IO_WATCH_READ);
		}else{
#ifdef EXTRA_DEBUG
			LM_DBG("siginfo: signal=%d (%d),"
					" si_code=%d, si_band=0x%x,"
					" si_fd=%d\n",
					siginfo.si_signo, n, siginfo.si_code,
					(unsigned)sigio_band,
					sigio_fd);
#endif
			/* on some errors (e.g. when receving TCP RST), sigio_band will
			 * be set to 0x08 (undocumented, no corresp. POLL_xx), so better
			 * catch all events --andrei */
			if (sigio_band/*&(POLL_IN|POLL_ERR|POLL_HUP)*/){
				fm=get_fd_map(h, sigio_fd);
				/* we can have queued signals generated by fds not watched
			 	 * any more, or by fds in transition, to a child
				 * => ignore them */
				if (fm->type)
					handle_io(fm, -1,IO_WATCH_READ);
				else
					LM_ERR("ignoring event"
							" %x on fd %d (fm->fd=%d, fm->data=%p)\n",
							sigio_band, sigio_fd, fm->fd, fm->data);
			}else{
				LM_ERR("unexpected event on fd %d: %x\n", sigio_fd, sigio_band);
			}
		}
	}else{
		/* signal queue overflow
		 * TODO: increase signal queue size: 2.4x /proc/.., 2.6x -rlimits */
		LM_WARN("signal queue overflowed- falling back to poll\n");
		/* clear real-time signal queue
		 * both SIG_IGN and SIG_DFL are needed , it doesn't work
		 * only with SIG_DFL  */
		if (signal(h->signo, SIG_IGN)==SIG_ERR){
			LM_CRIT("couldn't reset signal to IGN\n");
		}

		if (signal(h->signo, SIG_DFL)==SIG_ERR){
			LM_CRIT("couldn't reset signal to DFL\n");
		}
		/* falling back to normal poll */
		ret=io_wait_loop_poll(h, -1, 1);
	}
end:
	return ret;
error:
	return -1;
}
#endif



#ifdef HAVE_DEVPOLL
inline static int io_wait_loop_devpoll(io_wait_h* h, int t, int repeat)
{
	int n, r;
	int ret;
	struct dvpoll dpoll;

		dpoll.dp_timeout=t*1000;
		dpoll.dp_nfds=h->fd_no;
		dpoll.dp_fds=h->fd_array;
again:
		ret=n=ioctl(h->dpoll_fd, DP_POLL, &dpoll);
		if (n==-1){
			if (errno==EINTR) goto again; /* signal, ignore it */
			else{
				LM_ERR("ioctl: %s [%d]\n", strerror(errno), errno);
				goto error;
			}
		}
		for (r=0; r< n; r++){
			if (h->fd_array[r].revents & (POLLNVAL|POLLERR)){
				LM_ERR("pollinval returned for fd %d, revents=%x\n",
							h->fd_array[r].fd, h->fd_array[r].revents);
			}
			/* POLLIN|POLLHUP just go through */
			while((handle_io(get_fd_map(h, h->fd_array[r].fd), r,IO_WATCH_READ) > 0) &&
						repeat);
		}
error:
	return ret;
}
#endif


#endif
