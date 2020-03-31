/* Copyright (c) 2010 by Michael J. Roberts.  All Rights Reserved. */
/*
Name
  osnetwam.cpp - TADS OS networking and threading: WASM implementation
Function

Notes

Modified
  04/06/10 MJRoberts  -  Creation
*/

#include <arpa/inet.h>
#include <atomic>
#include <fcntl.h>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// #include <curl/curl.h>

#include "os.h"
#include "osifcnet.h"
#include "t3std.h"
#include "vmdatasrc.h"
#include "vmfile.h"
#include "vmnet.h"

/*
 *   To enable extra code for libcurl debugging, define this macro
 */
// #define OSNU_CURL_DEBUG

/*
 *   ignore a return value (to suppress superfluous gcc warn_unused_result
 *   warnings)
 */
void IGNORE(int) {}

/*
 *   Master thread list
 */
TadsThreadList* G_thread_list = 0;

/* ------------------------------------------------------------------------ */
/*
 *   Thread/event resource leak checking
 */
#ifdef LEAK_CHECK
OS_Counter mutex_cnt(0), event_cnt(0), thread_cnt(0), spin_lock_cnt(0);
#define IF_LEAK_CHECK(x) x
#else
#define IF_LEAK_CHECK(x)
#endif

/* ------------------------------------------------------------------------ */
/*
 *   debug logging (for debugging purposes only)
 */
void oss_debug_log(const char* fmt, ...)
{
#if 0
    va_list args;
    va_start(args, fmt);
    char *str = t3vsprintf_alloc(fmt, args);
    va_end(args);

    time_t timer = time(0);
    struct tm *tblk = localtime(&timer);
    char *tmsg = asctime(tblk);
    size_t tmsgl = strlen(tmsg);
    if (tmsg > 0 && tmsg[tmsgl-1] == '\n')
        tmsg[--tmsgl] = '\0';

    FILE *fp = fopen("/var/log/frob/tadslog2.txt", "a");
    fprintf(fp, "[%s] %s\n", tmsg, str);
    fclose(fp);

    t3free(str);
#endif
}

/* ------------------------------------------------------------------------ */
/*
 *   Reference counter
 */

OS_Counter::OS_Counter(long c)
    : cnt(c)
{}

/* ------------------------------------------------------------------------ */
/*
 *   Watchdog thread
 */

/* the watchdog quit event, as a global static */
OS_Event* OSS_Watchdog::quit_evt = 0;

/* ------------------------------------------------------------------------ */
/*
 *   Package initialization and termination
 */

/*
 *   Initialize the networking and threading package
 */
void os_net_init(TadsNetConfig* config)
{
   fprintf(stderr, "Tads NET Config disable...\n\n");
   exit(1);
}

/*
 *   Clean up the package in preparation for application exit
 */
void os_net_cleanup() { return; }

/* ------------------------------------------------------------------------ */
/*
 *   Local file selector dialog.  We don't currently implement a stand-alone
 *   Web UI mode on Unix platforms, so this is a no-op.
 */
int osnet_askfile(const char* prompt,
                  char* fname_buf,
                  int fname_buf_len,
                  int prompt_type,
                  int file_type)
{
   return OS_AFE_FAILURE;
}

/*
 *   Connect to the client UI.  A Web-based game calls this after starting
 *   its internal HTTP server, to send instructions back to the client on how
 *   the client UI can connect to the game.
 *
 *   This Unix implementation currently only supports client/server mode.
 */
int osnet_connect_webui(VMG_ const char* addr,
                        int port,
                        const char* path,
                        char** errmsg)
{
   /*
    *   Web server mode: our parent process is the conventional Web server
    *   running the php launch page.  The php launch page has a pipe
    *   connection to our stdout.  Send the start page information back to
    *   the php page simply by writing the information to stdout.
    */
   printf("\nconnectWebUI:http://%s:%d%s\n", addr, port, path);
   fflush(stdout);

   /* success */
   *errmsg = 0;
   return TRUE;
}

/* ------------------------------------------------------------------------ */
/*
 *   Local host information
 */

int os_get_hostname(char* buf, size_t buflen)
{
   /* ask the system for the host name */
   return !gethostname(buf, buflen);
}

int os_get_local_ip(char* buf, size_t buflen, const char* host)
{
   /* presume failure */
   int ok = FALSE;

   /* if the caller didn't provide a host name, look up the default */
   char hostbuf[128];
   if(host == 0 && !gethostname(hostbuf, sizeof(hostbuf))) host = hostbuf;

   /*
    *   Start by asking the system for the host name via gethostname(), then
    *   getting the IP addresses for the name.  The complication is that on
    *   many linux systems, this will only return 127.0.0.1, which isn't
    *   useful because that's only the local loopback address.  But we can
    *   at least try...
    */
   if(host != 0) {
      /* get the address info for the host name */
      struct hostent* local_host = gethostbyname(buf);
      if(local_host != 0) {
         /* scan the IP addresses for this host name */
         for(char** al = local_host->h_addr_list; *al != 0; ++al) {
            /* get the IP address */
            const char* ip = inet_ntoa(*(struct in_addr*) *al);

            /* if it's not a 127.0... address, use it */
            if(memcmp(ip, "127.0.", 6) != 0) {
               /* this is an external address - return it */
               lib_strcpy(buf, buflen, ip);
               return TRUE;
            }
         }
      }
   }

   /*
    *   If we get this far, it means that gethostbyname() won't give us
    *   anything besides the useless 127.0.0.1 loopback address.  The next
    *   step is to open a socket and do some mucking around in its system
    *   structures via an ioctl to get a list of the network interfaces in
    *   the system.
    */

   /* clear out the ioctl address structure */
   struct ifconf ifc;
   memset(&ifc, 0, sizeof(ifc));
   ifc.ifc_ifcu.ifcu_req = 0;
   ifc.ifc_len           = 0;

   /* no socket yet */
   int s = -1;

   /*
    *   Open a socket (we don't need to bind it to anything; just open it),
    *   then do the SIOCGICONF ioctl on the socket to get its network
    *   interface information.  We need to do this in two passes: first we
    *   make a call with a null request (ifcu_req) buffer just to find out
    *   how much space we need for the buffer, then we allocate the buffer
    *   and make another call to fetch the data.
    */
   if((s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0
      && ioctl(s, SIOCGIFCONF, &ifc) >= 0
      && (ifc.ifc_ifcu.ifcu_req = (struct ifreq*) t3malloc(ifc.ifc_len)) != 0
      && ioctl(s, SIOCGIFCONF, &ifc) >= 0) {
      /* figure the number of interfaces in the list  */
      int numif = ifc.ifc_len / sizeof(struct ifreq);

      /* iterate over the interfaces in the list */
      struct ifreq* r = ifc.ifc_ifcu.ifcu_req;
      for(int i = 0; i < numif; ++i, ++r) {
         /* get the network address for this interface */
         struct sockaddr_in* sin = (struct sockaddr_in*) &r->ifr_addr;
         const char* ip          = inet_ntoa(sin->sin_addr);

         /* if it's not a 127.0... address, return it */
         if(memcmp(ip, "127.0.", 6) != 0) {
            /* copy the result */
            lib_strcpy(buf, buflen, ip);

            /* success! - and no need to look any further */
            ok = TRUE;
            break;
         }
      }
   }

   /* if we allocated a result buffer, free it */
   if(ifc.ifc_ifcu.ifcu_req != 0) t3free(ifc.ifc_ifcu.ifcu_req);

   /* if we opened a socket, close it */
   if(s >= 0) close(s);

   /* return the status information */
   return ok;
}

/* ------------------------------------------------------------------------ */
/*
 *   Waitable object base class
 */

/*
 *   Wait for the object with a timeout
 */
int OS_Waitable::wait(unsigned long timeout)
{
   return get_event()->evt_wait(timeout);
}

/*
 *   Test the object state without blocking
 */
int OS_Waitable::test() { return get_event()->evt_test(); }

/*
 *   Wait for multiple waitable objects
 */
int OS_Waitable::multi_wait(int cnt, OS_Waitable** objs, unsigned long timeout)
{
   int i;
   int ret;
   struct timespec tm;

   /* figure the timeout, if applicable */
   if(timeout != OS_FOREVER) figure_timeout(&tm, timeout);

   /* create an event to represent the group wait */
   OS_Event group_evt(FALSE);

   /* add the group event as a subscriber to each individual event */
   for(i = 0; i < cnt; ++i) objs[i]->get_event()->subscribe(&group_evt);

   /* now wait until we get an individual event */
   for(;;) {
      /* check to see if any individual event is fired yet */
      for(i = 0, ret = OSWAIT_ERROR; i < cnt; ++i) {
         /* if this event is fired, we have a winner */
         if(objs[i]->test()) {
            /* this is the event that fired */
            ret = OSWAIT_EVENT + i;

            /* we only need one event, so stop looking */
            break;
         }
      }

      /* if we found a fired event, we're done */
      if(ret != OSWAIT_ERROR) break;

      /*
       *   Since we didn't find a signaled event, wait for the group event.
       *   We subscribed to each individual event, so when any of the
       *   subject events are signaled, they'll also signal our group event
       *   object, waking us up from this wait, at which point we'll go
       *   back and look for which event was fired.  Note that if there's a
       *   timeout, we use the wait-with-timeout with the ending time we
       *   figured; otherwise we just use a simple indefinite wait.
       */
      ret = (timeout != OS_FOREVER ? group_evt.evt_wait(&tm)
                                   : (group_evt.evt_wait(), OSWAIT_EVENT));

      /*
       *   If the group event fired, continue with the loop, so that we go
       *   back and find which individual subject event fired.  Otherwise,
       *   we have a timeout or error, so in either case abort the wait and
       *   return the result to our caller.
       */
      if(ret != OSWAIT_EVENT) break;
   }

   /* remove each subscription */
   for(i = 0; i < cnt; ++i) objs[i]->get_event()->unsubscribe(&group_evt);

   /* return the result */
   return ret;
}

void OS_Waitable::figure_timeout(struct timespec* tm, unsigned long timeout)
{
   /* figure the timeout interval in seconds */
   long sec = timeout / 1000UL;

   /* add the timeout to the current time */
   os_time_t cur_sec;
   long cur_nsec;
   os_time_ns(&cur_sec, &cur_nsec);
   cur_sec += sec;
   cur_nsec += (timeout % 1000UL) * 1000000L;
   if(cur_nsec > 999999999L) {
      cur_nsec -= 1000000000L;
      cur_sec++;
   }

   /* pass back the resulting timeout */
   tm->tv_sec  = cur_sec;
   tm->tv_nsec = cur_nsec;
}

/* ------------------------------------------------------------------------ */
/*
 *   Event
 */
OS_Event::OS_Event(int manual_reset)
{
   /* remember the reset mode */
   this->manual_reset = manual_reset;

   /* initial state is not signaled (zero 'set's) */
   cnt = 0;

   /* no multi-wait subscribers yet */
   sub_head = sub_tail = 0;

   /* count it for leak tracking */
   IF_LEAK_CHECK(event_cnt.inc());
}

OS_Event::~OS_Event()
{
   /* count it for leak tracking */
   IF_LEAK_CHECK(event_cnt.dec());
}

/*
 *   Signal the event.  In the case of a manual-reset event, this
 *   releases all threads waiting for the event, and leaves the event in
 *   a signaled state until it's explicitly reset.  For an auto-reset
 *   event, this releases only one thread waiting for the event and then
 *   automatically resets the event.
 */
void OS_Event::signal() {}

/*
 *   Reset the event.  This has no effect for an auto-reset event.
 */
void OS_Event::reset() {}

/* we're obviously our own waitable event object */
OS_Event* OS_Event::get_event() { return this; }

/*
 *   Wait for the event.  If the event is already in the signaled state,
 *   this returns immediately.  Otherwise, this blocks until another
 *   thread signals the event.  For an auto-reset event, the system
 *   immediately resets the event as soon as a thread is released.
 */
void OS_Event::evt_wait() {}

/* on the way out of an event wait, release our mutex */
void OS_Event::evt_wait_cleanup(void* ctx) {}

/*
 *   Wait for the event with a timeout, given in milliseconds.  This
 *   works the same way as wait(), but if the timeout expires before the
 *   event is signaled, this aborts the wait and returns OSWAIT_TIMEOUT.
 *   If the event is signaled before the timeout expires, we return with
 *   OSWAIT_EVENT.
 */
int OS_Event::evt_wait(unsigned long timeout) { return 0; }

/*
 *   Wait for the event with a timeout, given as an ending time in terms
 *   of the system clock.
 */
int OS_Event::evt_wait(const struct timespec* tm) { return 0; }

/*
 *   Test the event, without blocking.  This returns true if the event is
 *   in the signaled state, false if not.
 */
int OS_Event::evt_test() { return 0; }

/*
 *   Subscribe a multi-wait event object.  This adds the object to our
 *   list of multi-wait events that will be notified when this event
 *   fires.  This will wake up the multi-waiters, so that they can
 *   determine whether to wake up from their overall wait.
 */
void OS_Event::subscribe(OS_Event* e) {}

/*
 *   Unsubscribe a multi-wait event object.
 */
void OS_Event::unsubscribe(OS_Event* e) {}

/* ------------------------------------------------------------------------ */
/*
 *   Mutex.
 */

OS_Mutex::OS_Mutex() {}
OS_Mutex::~OS_Mutex() {}

/*
 *   Lock the mutex.  Blocks until the mutex is available.
 */
void OS_Mutex::lock() { padlock_.lock(); }

/*
 *   Test the mutex: if the mutex is available, locks the mutex and
 *   returns true; if not, returns false.  Doesn't block in either case.
 *   On a 'true' return, the mutex is locked, so the caller must unlock
 *   it when done.
 */
int OS_Mutex::test() { return padlock_.try_lock(); }

/*
 *   Unlock the mutex.  This can only be used after a successful lock()
 *   or test().  This releases our lock and makes the mutex available to
 *   other threads.
 */
void OS_Mutex::unlock() { padlock_.unlock(); }

/* ------------------------------------------------------------------------ */
/*
 *   Socket monitor thread
 */
class OS_Socket_Mon_Thread : public OS_Thread
{
 public:
   OS_Socket_Mon_Thread(OS_CoreSocket* s)
   {
      /* keep a reference on our socket */
      this->s = s;
      s->add_ref();
   }

   ~OS_Socket_Mon_Thread()
   {
      /* we're done with the socket */
      s->release_ref();
   }

   /* main thread entrypoint */
   void thread_main()
   {
      /* create the quit notifier pipe */
      if(pipe(qpipe) == -1) {
         s->close();
         s->ready_evt->signal();
         return;
      }

      /*
       *   add a cleanup handler: on exit, we need to signal the 'ready'
       *   event so that any other threads waiting for the socket will
       *   unblock
       */
      {
         std::stringstream ss{""};
         ss << "starting socket monitor thread " << thread_ptr->get_id();
         oss_debug_log(ss.str().c_str());
      }

      /* loop as long as the socket is open */
      for(;;) {
         /*
          *   Wait for the socket to go into blocking mode.  Unix doesn't
          *   have an "inverse select" function that blocks while the
          *   handle is unblocked, so we have to roll our own mechanism
          *   here, using a separate event set up for this purpose.  The
          *   socket signals this event each time it encounters an
          *   EWOULDBLOCK error in a send/recv.
          */
         s->blocked_evt->wait();

#if 1
         /*
          *   Wait for the socket to become ready.  Also poll the quit
          *   notification pipe, so that we'll wake up as soon as our
          *   socket has been closed locally.
          */
         pollfd fd[2];
         fd[0].fd = s->s;
         fd[0].events
             = (s->wouldblock_sending ? POLLOUT : POLLIN | POLLPRI) | POLLHUP;
         fd[1].fd     = qpipe[0];
         fd[1].events = POLLIN;

         /* wait; if that fails, terminate the thread */
         if(poll(fd, 2, -1) < 0) {
            if(errno != EINTR) break;
         }

         /* check to see if the socket is ready, or if it's been closed */
         if(fd[1].revents != 0) {
            /* quit event - exit the loop */
            break;
         } else if(fd[0].revents != 0) {
            /* ready - reset the 'blocked' event, and signal 'ready' */
            s->blocked_evt->reset();
            s->ready_evt->signal();
         } else {
            /* not ready - reset 'ready' and signal 'blocked' */
            s->ready_evt->reset();
            s->blocked_evt->signal();
         }
#else
         /* initialize a selection set for the socket */
         fd_set rfds, wfds;
         FD_ZERO(&rfds);
         FD_ZERO(&wfds);

         /* add the socket to the appropriate sets */
         FD_SET(s->s, s->wouldblock_sending ? &wfds : &rfds);

         /* wait for the socket to become ready */
         if(select(s->s + 1, &rfds, &wfds, 0, 0) <= 0) break;

         /*
          *   if the socket has an exception, give up - the socket must
          *   have been closed or reset, in which case the monitor thread
          *   is no longer useful
          */
         if(FD_ISSET(s->s, &efds)) break;

         /* if the socket selector fired, the socket is ready */
         if(FD_ISSET(s->s, &rfds) || FD_ISSET(s->s, &wfds)) {
            /* reset the 'blocked' event, and signal 'ready' */
            s->blocked_evt->reset();
            s->ready_evt->signal();
         } else {
            /* reset 'ready' and signal 'blocked' */
            s->ready_evt->reset();
            s->blocked_evt->signal();
         }
#endif
      }

      /* pop and invoke the cleanup handler */
      thread_cleanup(this);
   }

   /* notify the thread to shut down */
   void shutdown()
   {
      /* write the notification to the quit pipe */
      (void) write(qpipe[1], "Q", 1);
      s->blocked_evt->signal();
   }

 private:
   static void thread_cleanup(void* ctx)
   {
      /* get the thread object (it's the context) */
      OS_Socket_Mon_Thread* t = (OS_Socket_Mon_Thread*) ctx;

      /* before exiting, let anyone waiting for us go */
      t->s->blocked_evt->signal();
      t->s->ready_evt->signal();
   }

   /* our socket */
   OS_CoreSocket* s;

   /* quit notification pipe, for sending a 'quit' signal to the thread */
   int qpipe[2];
};

/* ------------------------------------------------------------------------ */
/*
 *   Core Socket.  This is the base class for ordinary data channel sockets
 *   and listener sockets.
 */

/* last incoming network event time */
time_t OS_CoreSocket::last_incoming_time = 0;

OS_CoreSocket::OS_CoreSocket()
{
   /* no socket yet */
   s = -1;

   /* no error yet */
   err = 0;

   /* no non-blocking-mode thread yet */
   ready_evt          = 0;
   blocked_evt        = 0;
   mon_thread         = 0;
   wouldblock_sending = FALSE;
}

/* create a socket object wrapping an existing system socket */
OS_CoreSocket::OS_CoreSocket(int s)
{
   this->s            = s;
   err                = 0;
   ready_evt          = 0;
   blocked_evt        = 0;
   wouldblock_sending = FALSE;
}

/*
 *   Destruction
 */
OS_CoreSocket::~OS_CoreSocket()
{
   /* close the socket */
   close();

   /* release resources */
   if(ready_evt != 0) ready_evt->release_ref();
   if(blocked_evt != 0) blocked_evt->release_ref();
   if(mon_thread != 0) mon_thread->release_ref();
}

/*
 *   Close the socket
 */
void OS_CoreSocket::close()
{
   /* close and forget the system socket */
   if(s != -1) {
      ::close(s);
      s = -1;
   }

   /* release our non-blocking status events */
   if(ready_evt != 0) ready_evt->signal();

   /* kill the monitor thread, if applicable; wait for it to exit */
   if(mon_thread != 0) {
      mon_thread->shutdown();
      mon_thread->wait();
   }
}

/*
 *   Set the socket to non-blocking mode
 */
void OS_CoreSocket::set_non_blocking()
{
   /* if it's already in non-blocking mode, there's nothing to do */
   if(ready_evt != 0) return;

   /* set the system socket to non-blocking mode */
   fcntl(s, F_SETFL, O_NONBLOCK);

   /*
    *   Create the "ready" event for the socket.  The monitor thread signals
    *   this event whenever select() shows that the socket is ready.  The
    *   send() and recv() methods reset this event whenever the underlying
    *   socket send/recv functions return EWOULDBLOCK.  Callers can thus
    *   wait on this event when they want to block until the socket is
    *   ready.
    *
    *   The reason that callers would want an event object rather than just
    *   actually blocking on the socket call is that they can combine this
    *   event with others in a multi-wait, so that they'll stop blocking
    *   when the socket is ready OR another event fires, whichever comes
    *   first.
    *
    *   This is a manual reset event because it follows the socket's
    *   readiness state - it doesn't reset on releasing a thread, but rather
    *   when the thread actually exhausts the socket buffer and would cause
    *   the socket to block again.
    */
   ready_evt = new OS_Event(TRUE);

   /*
    *   Create the "blocked" event.  The send() and recv() methods signal
    *   this event when the underlying socket calls return EWOULDBLOCK.  The
    *   monitor thread waits on this event before calling select() on the
    *   socket, so that it doesn't loop endlessly when the socket is ready
    *   for an extended period.  Instead, the monitor thread blocks on this
    *   until the main thread encounters an EWOULDBLOCK error, at which
    *   point the monitor wakes up and performs a select(), which will then
    *   block until the socket becomes ready.
    */
   blocked_evt = new OS_Event(TRUE);

   /*
    *   Assume the socket starts out ready.  When the caller performs the
    *   first socket operation, they'll determine the actual state - if the
    *   send/recv succeed, it really was ready.  If EWOULDBLOCK is returned,
    *   we know that it wasn't ready, at which point we'll update both
    *   events so that the monitor thread can watch for eventual readiness.
    */
   ready_evt->signal();

   /*
    *   Launch the monitor thread.  This thread synchronizes the state of
    *   the sock_evt event with the underlying socket state.
    */
   mon_thread = new OS_Socket_Mon_Thread(this);
   mon_thread->launch();
}

/*
 *   Get the IP address for the given host.  The return value is an
 *   allocated buffer that the caller must delete with 'delete[]'.  If
 *   the host IP address is unavailable, returns null.
 */
char* OS_CoreSocket::get_host_ip(const char* hostname)
{
   /*
    *   if no host name was specified, get the default local host name
    *   from the environment
    */
   if(hostname == 0 || hostname[0] == '\0') {
      hostname = getenv("HOSTNAME");
      if(hostname == 0) return 0;
   }

   /* get the host entry for our local host name */
   struct hostent* local_host = gethostbyname(hostname);
   if(local_host == 0) return 0;

   /* get the IP address from the host entry structure */
   const char* a = inet_ntoa(*(struct in_addr*) local_host->h_addr_list[0]);
   if(a == 0) return 0;

   /* make an allocated copy of the buffer for the caller */
   char* buf = new char[strlen(a) + 1];
   strcpy(buf, a);

   /* return the allocated copy */
   return buf;
}

/*
 *   Get the IP address for our side (the local side) of the socket.
 *   'ip' receives an allocated string with the IP address string in
 *   decimal notation ("192.168.1.15").  We fill in 'port' with the local
 *   port number of the socket.  Returns true on success, false on
 *   failure.  If we return success, the caller is responsible for
 *   freeing the allocated 'ip' string via t3free().
 */
int OS_CoreSocket::get_local_addr(char*& ip, int& port)
{
   socklen_t len;
   struct sockaddr_storage addr;

   /* get the local socket information */
   len = sizeof(addr);
   getsockname(s, (struct sockaddr*) &addr, &len);

   /* parse the address */
   return parse_addr(addr, len, ip, port);
}

/*
 *   Get the IP address for the network peer to which we're connected.
 *   'ip' receives an allocated string with the IP address string in
 *   decimal notation ("192.168.1.15").  We fill in 'port' with the port
 *   number on the remote host.  Returns true on success, false on
 *   failure.  If we return success, the caller is responsible for
 *   freeing the allocated 'ip' string via t3free().
 */
int OS_CoreSocket::get_peer_addr(char*& ip, int& port)
{
   socklen_t len;
   struct sockaddr_storage addr;

   /* get the peer name */
   len = sizeof(addr);
   getpeername(s, (struct sockaddr*) &addr, &len);

   /* parse the address */
   return parse_addr(addr, len, ip, port);
}

int OS_CoreSocket::parse_addr(struct sockaddr_storage& addr,
                              int len,
                              char*& ip,
                              int& port)
{
   char ipstr[INET6_ADDRSTRLEN];

   /* presume failure */
   ip   = 0;
   port = 0;

   /* check the protocol family */
   if(addr.ss_family == AF_INET) {
      /* get the address information */
      struct sockaddr_in* s = (struct sockaddr_in*) &addr;
      port                  = ntohs(s->sin_port);
      inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);

      /* allocate the IP string and return success */
      ip = lib_copy_str(ipstr);
      return TRUE;
   } else if(addr.ss_family == AF_INET6) {
      struct sockaddr_in6* s = (struct sockaddr_in6*) &addr;
      port                   = ntohs(s->sin6_port);
      inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);

      /* allocate the IP string and return success */
      ip = lib_copy_str(ipstr);
      return TRUE;
   } else {
      /* we don't handle other families */
      return FALSE;
   }
}

/* ------------------------------------------------------------------------ */
/*
 *   Data socket
 */

/*
 *   Send bytes.  Returns the number of bytes sent, or OS_SOCKET_ERROR if
 *   an error occurs.
 */
int OS_Socket::send(const char* buf, size_t len)
{
   /* send the bytes and note the result */
   int ret = ::send(s, buf, len, MSG_NOSIGNAL);

   /*
    *   If an error occurred, note it.  Treat EAGAIN as equivalent to
    *   EWOULDBLOCK.
    */
   err = (ret < 0 ? (errno == EAGAIN ? EWOULDBLOCK : errno) : 0);

   /* on EWOULDBLOCK, set the blocking status indicators */
   if(err == EWOULDBLOCK) {
      /* the wouldblock direction is 'send' */
      wouldblock_sending = TRUE;

      /* set the events for blocking mode */
      ready_evt->reset();
      blocked_evt->signal();
   }

   /* return the result */
   return ret;
}

/*
 *   Receive bytes.  Returns the number of bytes received, or
 *   OS_SOCKET_ERROR if an error occurs.
 */
int OS_Socket::recv(char* buf, size_t len)
{
   /* read the bytes and note the result */
   int ret = ::recv(s, buf, len, MSG_NOSIGNAL);

   /*
    *   If an error occurred, note it.  Treat EAGAIN as equivalent to
    *   EWOULDBLOCK.
    */
   err = (ret < 0 ? (errno == EAGAIN ? EWOULDBLOCK : errno) : 0);

   /* on EWOULDBLOCK, set the blocking status indicators */
   if(err == EWOULDBLOCK) {
      /* the wouldblock direction is 'receive' */
      wouldblock_sending = FALSE;

      /* set the events for blocking mode */
      ready_evt->reset();
      blocked_evt->signal();
   }

   /* if we successfully received data, this is incoming activity */
   if(err == 0) last_incoming_time = time(0);

   /* return the result */
   return ret;
}

/* create an OS_Socket object to wrap an existing system socket handle */
OS_Socket::OS_Socket(int s)
    : OS_CoreSocket(s)
{}

OS_Socket::~OS_Socket() {}

/* ------------------------------------------------------------------------ */
/*
 *   Listener.  This class represents a network listener socket, which is
 *   used to wait for and accept new incoming connections from clients.
 */

/*
 *   Open the listener on the given port number.  This can be used to
 *   create a server on a well-known port, but it has the drawback that
 *   ports can't be shared, so this will fail if another process is
 *   already using the same port number.
 *
 *   Returns true on success, false on failure.
 */
int OS_Listener::open(const char* hostname, unsigned short port_num)
{
   /* create our socket */
   s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if(s == -1) return FALSE;

   /* bind to the given host address */
   char* ip = get_host_ip(hostname);
   if(ip == 0) return FALSE;

   /*
    *   set up the address request structure to bind to the requested
    *   port on the local address we just figured
    */
   struct sockaddr_in saddr;
   saddr.sin_family      = PF_INET;
   saddr.sin_addr.s_addr = inet_addr(ip);
   saddr.sin_port        = htons(port_num);
   memset(saddr.sin_zero, 0, sizeof(saddr.sin_zero));

   /* done with the IP address */
   delete[] ip;

   /* try binding */
   if(bind(s, (sockaddr*) &saddr, sizeof(saddr))) return FALSE;

   /* put the socket into the 'listening' state */
   if(listen(s, SOMAXCONN)) return FALSE;

   /* success */
   return TRUE;
}

/*
 *   Open the listener port, with the port number assigned by the system.
 *   The system will select an available port and assign it to this
 *   listener, so this avoids contention for specific port numbers.
 *   However, it requires some separate means of communicating the port
 *   number to clients, since there's no way for them to know the port
 *   number in advance.
 *
 *   Returns true on success, false on failure.
 */
int OS_Listener::open(const char* hostname) { return open(hostname, 0); }

/*
 *   Accept the next incoming connection.  If the listener is in blocking
 *   mode (the default), and there are no pending requests, this blocks
 *   until the next pending request arrives.  In non-blocking mode, this
 *   returns immediately with a null socket if no requests are pending,
 *   and last_error() indicates OS_EWOULDBLOCK.
 */
OS_Socket* OS_Listener::accept()
{
   /* accept a connection on the underlying system socket */
   struct sockaddr_in addr;
   socklen_t addrlen = sizeof(addr);
   int snew          = ::accept(s, (sockaddr*) &addr, &addrlen);

   /* check to see if we got a socket */
   if(snew != -1) {
      /* success - clear the error memory */
      err = 0;

      /* this counts as incoming network activity - note the time */
      last_incoming_time = time(0);

      /*
       *   Set the "linger" option on close.  This helps ensure that we
       *   transmit any error response to a request that causes the
       *   server thread to abort.  Without this option, winsock will
       *   sometimes terminate the connection before transmitting the
       *   final response.
       */
      struct linger ls = {TRUE, 1};
      setsockopt(snew, SOL_SOCKET, SO_LINGER, (char*) &ls, sizeof(ls));

      /*
       *   Turn off SIGPIPE signals on this socket - we deal with
       *   disconnections via error returns on send() rather than
       *   signals.  Note that this setting only applies to some Unix
       *   varieties; we'll do it only if SO_NOSIGPIPE is defined
       *   locally.
       */
#ifdef SO_NOSIGPIPE
      int nsp = 1;
      setsockopt(snew, SOL_SOCKET, SO_NOSIGPIPE, (char*) &nsp, sizeof(nsp));
#endif

      /*
       *   Disable the Nagle algorithm for this socket to minimize
       *   transmit latency.  With the Nagle algorithm in effect,
       *   whenever the program sends a small packet (via ::send(),
       *   e.g.), the TCP layer queues the bytes but doesn't send them
       *   out on the network immediately; instead, it waits a little
       *   while to see if another ::send() quickly follows, in which
       *   case the bytes from the first ::send() can be combined with
       *   those from the second to form one larger packet.  This
       *   continues until the maximum packet size is reached or a
       *   timeout expires, at which point the queued bytes are finally
       *   sent out on the network.  This automatic buffering and
       *   coalescing behavior is beneficial to many appliations,
       *   because there's some overhead associated with each packet;
       *   coalescing multiple small ::send()'s into one large packet
       *   reduces this packet overhead.  However, it's detrimental to
       *   programs like TADS that tend to send a series of small
       *   messages that need to be acknowledged individually before
       *   the next can be sent.  The Nagle algorithm's delay in this
       *   case manifests as pure latency, since the additional bytes
       *   being waited for never arrive, so the original small packet
       *   must be sent out after all - but not until the Nagle
       *   algorithm delay expires, whereas with Nagle turned off, it's
       *   sent out immediately.
       */
      int tcpflag = 1;
      setsockopt(
          snew, IPPROTO_TCP, TCP_NODELAY, (char*) &tcpflag, sizeof(tcpflag));

      /* wrap the socket in an OS_Socket and return the object */
      return new OS_Socket(snew);
   } else {
      /* failed - remember the system error code */
      err = (errno == EAGAIN ? EWOULDBLOCK : errno);

      /* on EWOULDBLOCK, set the blocking status indicators */
      if(err == EWOULDBLOCK) {
         /* the wouldblock direction for listen is 'receive' */
         wouldblock_sending = FALSE;

         /* set the events for blocking mode */
         ready_evt->reset();
         blocked_evt->signal();
      }

      /* return failure */
      return 0;
   }
}

/* ------------------------------------------------------------------------ */
/*
 *   Thread.  Callers subclass this to define the thread entrypoint routine.
 */

OS_Thread::OS_Thread()
{
   /* count it in the leak tracker */
   IF_LEAK_CHECK(thread_cnt.inc());

   /* create the event to signal at thread completion */
   done_evt = new OS_Event(TRUE);

   // /* add myself to the master thread list */
   // G_thread_list->add(this);
}

OS_Thread::~OS_Thread()
{
   /*
    *   Detach the thread.  We don't use 'join' to detect when the
    *   thread terminates; instead, we use our thread event.  This lets
    *   the operating system delete the thread resources (in particular,
    *   its stack memory) immediately when the thread exits, rather than
    *   waiting for a 'join' that might never come.
    */
   if(tid_valid()) thread_ptr->detach();

   /* release our 'done' event */
   done_evt->release_ref();

   /* clean up the master thread list for our deletion */
   // if(G_thread_list != 0) G_thread_list->clean();

   /* count it for leak tracking */
   IF_LEAK_CHECK(thread_cnt.dec());
}

/*
 *   Launch the thread.  Returns true on success, false on failure.
 */
int OS_Thread::launch()
{
   /*
    *   The initial reference for our caller, which constructed this
    *   object.  Add a second reference on behalf of the new thread that
    *   we're launching - it has a reference to 'this' as its thread
    *   context object.
    */
   add_ref();

   /* create the thread */
   int err = 0;
   try {
      thread_ptr
          = std::make_unique<std::thread>([this]() { sys_thread_main(this); });
   } catch(std::exception& e) {
      err = 1;
   }

   /* check for errors */
   if(err != 0) {
      /*
       *   The launch failed, so release the reference we created for
       *   the thread - there actually will be no thread to take over
       *   that reference.
       */
      release_ref();

      /* signal the 'done' event immediately */
      done_evt->signal();

      /* not launched */
      return FALSE;
   } else {
      /* indicate success */
      return TRUE;
   }
}

#if 0
    /*
     *   Cancel the thread.
     *   
     *   [This method is currently disabled because of problems that appear
     *   with some versions of Linux and/or gcc - it's not clear who's at
     *   fault, but there appears to be a known bug where pthread_cancel()
     *   fails to trigger cleanup handlers in the target thread.  This method
     *   was only used in one place, which was to kill the monitor thread
     *   associated with a non-blocking socket when the socket was closed.
     *   That's now handled instead via a message through a pipe, so no code
     *   currently uses the routine, and we've #if'd it out to make sure that
     *   no new code uses it without being aware of the cleanup handler bug.
     *   If it becomes desirable to re-enable the routine at some point, we
     *   need to first make sure that there's a solution to the cleanup
     *   handler bug, since that will cause unexpected behavior on platforms
     *   where the bug occurs.]
     */
    void cancel_thread(int wait)
    {
        /* request cancellation of the pthreads thread */
        if (tid_valid)
            pthread_cancel(tid);

        /* if desired, wait for the thrad */
        if (wait)
            done_evt->wait();
    }
#endif

/* get the waitable event */
OS_Event* OS_Thread::get_event() { return done_evt; }

/*
 *   Static entrypoint.  This is the routine the system calls directly;
 *   we then invoke the virtual method with the subclassed thread main.
 */
void* OS_Thread::sys_thread_main(void* ctx)
{
   /* the context is the thread structure */
   OS_Thread* t = (OS_Thread*) ctx;

   /* launch the real thread and remember the result code */
   t->thread_main();

   sys_thread_cleanup(ctx);

   /* return a dummy result code */
   return 0;
}

/*
 *   Cleanup entrypoint.  The system pthreads package invokes this when
 *   our thread exits, either by returning from the main thread
 *   entrypoint routine or via a cancellation request.  At exit, we
 *   release the thread's reference on the OS_Thread object, and we
 *   signal the thread's 'done' event to indicate that the thread has
 *   terminated.
 */
void OS_Thread::sys_thread_cleanup(void* ctx)
{
   /* the context is the thread structure */
   OS_Thread* t = (OS_Thread*) ctx;

   /* before we release our ref on the thread, save its done event */
   OS_Event* done_evt = t->done_evt;
   done_evt->add_ref();

   /* release the thread's reference on the thread structure */
   t->release_ref();

   /* the thread has now truly exited - signal the termination event */
   done_evt->signal();
   done_evt->release_ref();
}

/*
 *   Send an HTTP request as a client
 */
int OS_HttpClient::request(int opts,
                           const char* host,
                           unsigned short portno,
                           const char* verb,
                           const char* resource,
                           const char* send_headers,
                           size_t send_headers_len,
                           OS_HttpPayload* payload,
                           CVmDataSource* reply,
                           char** headers,
                           char** location,
                           const char* ua)
{
   fprintf(stderr, "CODE DISABLED\n");
   exit(1);
   return 1;
}

/*
 *   Watchdog thread object class
 */

OSS_Watchdog::OSS_Watchdog()
{
   /* create our 'quit' event object */
   quit_evt = new OS_Event(TRUE);

   /* we haven't collected any statistics yet */
   cnt = 0;
   idx = 0;

   /*
    *   Initialize the last incoming network activity time to the
    *   current time.  We haven't actually seen any network activity
    *   yet, of course, but what we're really interested in is the
    *   interval, and it's not meaningful to talk about an interval
    *   longer than we've been running.
    */
   OS_CoreSocket::last_incoming_time = time(0);
}

OSS_Watchdog::~OSS_Watchdog()
{
   /* we're done with our 'quit' event */
   quit_evt->release_ref();
}

void OSS_Watchdog::thread_main()
{
   oss_debug_log("watchdog thread started");

   /* loop until our 'quit' event has fired */
   while(!quit_evt->test()) {
      /* wake up every 10 seconds, or when the 'quit' event fires */
      if(quit_evt->wait(10000) == OSWAIT_TIMEOUT) {
         /*
          *   That timed out, so it's time for another watchdog check.
          *   First, update the timers.
          */
         stats[idx].cpu_time  = clock() / CLOCKS_PER_SEC;
         stats[idx].wall_time = time(0);

         /* bump the statistics counters */
         ++cnt;
         if(++idx >= NSTATS) idx = 0;

         /*
          *   If we've collected enough statistics, do our checks.  We
          *   need at least 60 seconds of trailing data, and we wake
          *   up every 10 seconds, so we need at least seven samples
          *   (#0 is 0 seconds ago, #1 is 10 seconds ago, ... #6 is 60
          *   seconds ago, hence we need 0-6 = 7 samples).
          */
         if(cnt >= 7) {
            /* get the last and the 60-seconds-ago stats */
            const watchdog_stats* cur = get_stats(0);
            const watchdog_stats* prv = get_stats(6);

            /* calculate the percentage of CPU time */
            double cpu_pct = ((double) (cur->cpu_time - prv->cpu_time))
                             / (cur->wall_time - prv->wall_time);

            /* calculate the time since the last network event */
            time_t time_since_net
                = cur->wall_time - OS_CoreSocket::last_incoming_time;

            /*
             *   Check for termination conditions:
             *
             *   1.  If we've consumed 40% or higher CPU over the
             *   last minute, we're probably stuck in a loop; we
             *   might also be running an intentionally
             *   compute-intensive program, which isn't a bug but is
             *   an abuse of a shared server, so in either case kill
             *   the program.  (We're not worried about short bursts
             *   of high CPU usage, which is why we average over a
             *   fairly long real-time period.)
             *
             *   2.  If we haven't seen any incoming network activity
             *   in 6 minutes, the program is probably idle and
             *   should probably have noticed that and shut itself
             *   down already, so it might have a bug in its internal
             *   inactivity monitor.  It could also be intentionally
             *   running as a continuous service, but that's abusive
             *   on a shared server because it uses memory that's
             *   meant to be shared.  In either case, shut it down.
             */
            if(cpu_pct >= 0.4) {
               oss_debug_log("watchdog terminating process due to "
                             "high CPU usage (cpu_pct = %0.2ld%%)",
                             cpu_pct);
               kill(getpid(), 9);
            }
            if(time_since_net > 6 * 60) {
               oss_debug_log("watchdog terminating process due to "
                             "network inactivity (time_since_net"
                             " = %ld seconds)",
                             (long) time_since_net);
               kill(getpid(), 9);
            }
         }
      }
   }
}

/*
 *   get the nth most recent stats entry - 0 is the latest entry, 1 is
 *   the second older entry, etc
 */
const watchdog_stats* OSS_Watchdog::get_stats(int i) const
{
   /*
    *   idx points to the next to write, so idx-1 is the latest entry,
    *   idx-2 is the second entry, etc
    */
   i = idx - 1 - i;

   /* the array is circular, so adjust for wrapping */
   if(i < 0) i += NSTATS;

   /* return the element */
   return &stats[i];
}
