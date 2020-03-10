/* Copyright (c) 2010 by Michael J. Roberts.  All Rights Reserved. */
/*
Name
  osnetunix.h - TADS networking and threading, Unix implementation
Function

Notes

Modified
  04/07/10 MJRoberts  - Creation
*/

#ifndef OSNETWASM_H
#define OSNETWASM_H

/* system headers */
#include <atomic>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <mutex>
#include <signal.h>
#include <stdarg.h>
#include <thread>

/* true/false */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* infinite timeout value */
#define OS_FOREVER ((ulong) -1)

/*
 *   Most Unix variants have at least one of SO_NOSIGPIPE or MSG_NOSIGNAL,
 *   and they seem to serve the same purpose everywhere, so if either is
 *   missing just define it away (i.e., define as 0, as these are bit flags).
 *   This doesn't handle the case where a system lacks *both*, but that seems
 *   to be rare, and the code necessary to work around it is messy enough
 *   that we're going to ignore the possibility.
 */
#ifdef SO_NOSIGPIPE
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#ifdef MSG_NOSIGNAL
#define SO_NOSIGPIPE 0
#endif
#endif

/* include the base layer headers */
#include "osifcnet.h"

/* base TADS 3 header */
#include "t3std.h"

/* debug logging */
void oss_debug_log(const char* fmt, ...);

/* ------------------------------------------------------------------------ */
/*
 *   Reference counter
 */
class OS_Counter
{
 public:
   OS_Counter() = default;
   OS_Counter(long c);
   ~OS_Counter() = default;

   /* get the current counter value */
   long get() const { return cnt; }

   /*
    *   Increment/decrement.  These return the post-update result; the
    *   operation must be atomic, so that the return value is the result of
    *   this increment or decrement only, even if another thread
    *   concurrently makes another update.
    */
   long inc() { return cnt.fetch_add(1, std::memory_order_relaxed) + 1; }
   long dec() { return cnt.fetch_sub(1, std::memory_order_relaxed) - 1; }

 private:
   /* reference count */
   std::atomic<long> cnt{1};
};

/* ------------------------------------------------------------------------ */
/*
 *   System resource leak tracking
 */
#ifdef LEAK_CHECK
extern OS_Counter mutex_cnt, event_cnt, thread_cnt, spin_lock_cnt;
#define IF_LEAK_CHECK(x) x
#else
#define IF_LEAK_CHECK(x)
#endif

/* ------------------------------------------------------------------------ */
/*
 *   Include the reference-counted object header
 */
#include "vmrefcnt.h"

/* ------------------------------------------------------------------------ */
/*
 *   Waitable objects
 */

/* private structure: multi-wait subscriber link */
struct osu_waitsub
{
   osu_waitsub(class OS_Event* e_, osu_waitsub* nxt_)
       : e(e_)
       , nxt(nxt_)
   {}

   /* this event */
   class OS_Event* e = nullptr;

   /* next subscriber in list */
   osu_waitsub* nxt = nullptr;
};

/*
 *   Waitable object.  This is the base class for objects that can be used in
 *   a multi-object wait.  A waitable object has two states: unsignaled and
 *   signaled.  Waiting for the object blocks the waiting thread as long as
 *   the object is in the unsignaled state, and releases the thread (allows
 *   it to continue running) as soon as the object enters the signaled state.
 *   Waiting for an object that's already in the signaled state simply allows
 *   the calling thread to continue immediately without blocking.
 */
class OS_Waitable
{
 public:
   OS_Waitable() {}

   virtual ~OS_Waitable() {}

   /*
    *   Wait for the object, with a maximum wait of 'timeout' milliseconds.
    *   Returns an OSWAIT_xxx code to indicate what happened.
    */
   int wait(unsigned long timeout = OS_FOREVER);

   /*
    *   Test to see if the object is ready, without blocking.  Returns true
    *   if the object is ready, false if not.  If the object is ready,
    *   waiting for the object would immediately release the thread.
    *
    *   Note that testing some types of objects "consumes" the ready state
    *   and resets the object to not-ready.  This is true of auto-reset
    *   event objects.
    */
   int test();

   /*
    *   Wait for multiple objects.  This blocks until at least one event in
    *   the list is signaled, at which point it returns OSWAIT_OBJECT+N,
    *   where N is the array index in 'events' of the event that fired.  If
    *   the timeout (given in milliseconds) expires before any events fire,
    *   we return OSWAIT_TIMEOUT.  If the timeout is omitted, we wait
    *   indefinitely, blocking until one of the events fires.
    */
   static int
   multi_wait(int cnt, OS_Waitable** objs, unsigned long timeout = OS_FOREVER);

 protected:
   /*
    *   Figure a timeout end time.  This adds the given timeout to the
    *   current system time to get the system time at the expiration of the
    *   timeout.
    */
   static void figure_timeout(struct timespec* tm, unsigned long timeout);

   /*
    *   Get the associated event object.  For the unix implementation, all
    *   waitable objects are waitable by virtue of having associated event
    *   objects.
    */
   virtual class OS_Event* get_event() = 0;
};

/* ------------------------------------------------------------------------ */
/*
 *   Event
 */
class OS_Event : public CVmRefCntObj, public OS_Waitable
{
   friend class OS_Waitable;

 public:
   OS_Event(int manual_reset);
   ~OS_Event();

   /*
    *   Signal the event.  In the case of a manual-reset event, this
    *   releases all threads waiting for the event, and leaves the event in
    *   a signaled state until it's explicitly reset.  For an auto-reset
    *   event, this releases only one thread waiting for the event and then
    *   automatically resets the event.
    */
   void signal();

   /*
    *   Reset the event.  This has no effect for an auto-reset event.
    */
   void reset();

 protected:
   /* we're obviously our own waitable event object */
   virtual class OS_Event* get_event();

 private:
   /*
    *   Wait for the event.  If the event is already in the signaled state,
    *   this returns immediately.  Otherwise, this blocks until another
    *   thread signals the event.  For an auto-reset event, the system
    *   immediately resets the event as soon as a thread is released.
    */
   void evt_wait();

   /* on the way out of an event wait, release our mutex */
   static void evt_wait_cleanup(void* ctx);

   /*
    *   Wait for the event with a timeout, given in milliseconds.  This
    *   works the same way as wait(), but if the timeout expires before the
    *   event is signaled, this aborts the wait and returns OSWAIT_TIMEOUT.
    *   If the event is signaled before the timeout expires, we return with
    *   OSWAIT_EVENT.
    */
   int evt_wait(unsigned long timeout);

   /*
    *   Wait for the event with a timeout, given as an ending time in terms
    *   of the system clock.
    */
   int evt_wait(const struct timespec* tm);

   /*
    *   Test the event, without blocking.  This returns true if the event is
    *   in the signaled state, false if not.
    */
   int evt_test();

   /*
    *   Subscribe a multi-wait event object.  This adds the object to our
    *   list of multi-wait events that will be notified when this event
    *   fires.  This will wake up the multi-waiters, so that they can
    *   determine whether to wake up from their overall wait.
    */
   void subscribe(OS_Event* e);

   /*
    *   Unsubscribe a multi-wait event object.
    */
   void unsubscribe(OS_Event* e);

   /*
    *   Is this a manual-reset event?  True means that this is a manual
    *   event: signaling the event will release all threads waiting for the
    *   event, and the event will remain signaled until it's explicitly
    *   cleared.  False means that this is an auto-reset event: signaling
    *   the event releases only ONE thread waiting for the event, and as
    *   soon as the thread is released, the system automatically clears the
    *   event, so no more threads will be released until the next signal.
    */
   int manual_reset;

   /* the signal count */
   int cnt;
   std::condition_variable cond;
   std::mutex mutex;

   /* head of our list of multi-wait subscribers */
   osu_waitsub *sub_head, *sub_tail;
};

/* ------------------------------------------------------------------------ */
/*
 *   Mutex.
 */
class OS_Mutex : public CVmRefCntObj
{
 public:
   OS_Mutex();
   // OS_Mutex(const OS_Mutex&) = delete;
   // OS_Mutex(OS_Mutex&&) = delete;
   ~OS_Mutex();
   // OS_Mutex& operator=(const OS_Mutex&) = delete;
   // OS_Mutex& operator=(OS_Mutex&&) = delete;

   void lock();
   int test();
   void unlock();

 private:
   std::mutex padlock_;
};

/* ------------------------------------------------------------------------ */
/*
 *   Socket error indicator - returned from OS_Socket::send() and recv() if
 *   an error occurs, in lieu of a valid length value.
 */
#define OS_SOCKET_ERROR (-1)

/*
 *   Error values for OS_Socket::last_error().
 */
#define OS_EWOULDBLOCK EWOULDBLOCK   /* send/recv would block */
#define OS_ECONNRESET ECONNRESET     /* connection reset by peer */
#define OS_ECONNABORTED ECONNABORTED /* connection aborted on this end */

/* ------------------------------------------------------------------------ */
/*
 *   Socket.  This is a thin layer implementing the same model as the Unix
 *   socket API.  Windows provides a socket library of its own, but it's
 *   quite idiosyncratic, so we can't just write code directly to the
 *   standard Unix API.
 */
class OS_CoreSocket : public CVmRefCntObj, public OS_Waitable
{
   friend class OS_Socket_Mon_Thread;

 public:
   OS_CoreSocket();
   ~OS_CoreSocket();

   void set_non_blocking();
   void reset_event() {}
   int last_error() const { return err; }
   void close();

   /*
    *   Get the IP address for the given host.  The return value is an
    *   allocated buffer that the caller must delete with 'delete[]'.  If
    *   the host IP address is unavailable, returns null.
    */
   static char* get_host_ip(const char* hostname);
   int get_local_addr(char*& ip, int& port);
   int get_peer_addr(char*& ip, int& port);

   /*
    *   System time (as returned by time()) of last incoming network
    *   activity.  The watchdog thread (if enabled) uses this to make sure
    *   that the bytecode program is actually serving a client, and
    *   terminate the program if it appears to be running without a client
    *   for an extended period.
    */
   static time_t last_incoming_time;

 protected:
   /* create a socket object wrapping an existing system socket */
   OS_CoreSocket(int s);

   /*
    *   Get the waitable event.  A caller who's waiting for the socket wants
    *   to know when the socket is ready, so use the 'ready' event as the
    *   socket wait event.
    */
   virtual class OS_Event* get_event() { return ready_evt; }

   int parse_addr(struct sockaddr_storage& addr, int len, char*& ip, int& port);

   /* our underlying system socket handle */
   int s;

   /* last send/receive error */
   int err;

   /*
    *   for non-blocking sockets, the recv/send direction of the last
    *   EWOULDBLOCK: true means send, false means recv
    */
   int wouldblock_sending;

   /*
    *   The non-blocking status events.  The 'ready' event is signaled
    *   whenever select() shows that the socket is ready, meaning that a
    *   recv() or send() will be able to read/write at least one byte
    *   without blocking.  The 'blocked' event is the opposite: it's
    *   signaled whenever a recv() or send() gets an EWOULDBLOCK error,
    *   indicating that the socket is blocked until more data arrive in or
    *   depart from the buffer.
    *
    *   The 'ready' event is maintained by a separate monitor thread that we
    *   create when the socket is placed into non-blocking mode.
    *
    *   We need both events because we don't have a way of waiting for an
    *   event to become unsignaled.  So we need to signal these two states
    *   independently via separate event objects.
    */
   OS_Event* ready_evt;
   OS_Event* blocked_evt;

   /* non-blocking status monitor thread */
   class OS_Socket_Mon_Thread* mon_thread;
};

/* ------------------------------------------------------------------------ */
/*
 *   Data socket
 */
class OS_Socket : public OS_CoreSocket
{
   friend class OS_Listener;

 public:
   int send(const char* buf, size_t len);
   int recv(char* buf, size_t len);

 protected:
   OS_Socket(int s);
   ~OS_Socket();
};

/* ------------------------------------------------------------------------ */
/*
 *   Listener.  This class represents a network listener socket, which is
 *   used to wait for and accept new incoming connections from clients.
 */
class OS_Listener : public OS_CoreSocket
{
 public:
   OS_Listener() {}
   ~OS_Listener() {}
   int open(const char* hostname, unsigned short port_num);
   int open(const char* hostname);
   OS_Socket* accept();
};

/* ------------------------------------------------------------------------ */
/*
 *   Thread.  Callers subclass this to define the thread entrypoint routine.
 */
class OS_Thread : public CVmWeakRefable, public OS_Waitable
{
   friend class TadsThreadList;

 public:
   OS_Thread();
   virtual ~OS_Thread();
   int launch();

   /*
    *   Thread entrypoint routine.  Callers must subclass this class and
    *   provide a concrete definition for this method.  This method is
    *   called at thread entry.
    */
   virtual void thread_main() = 0;

#if 0
   void cancel_thread(int wait = FALSE);
#endif

 protected:
   virtual class OS_Event* get_event();
   static void* sys_thread_main(void* ctx);
   static void sys_thread_cleanup(void* ctx);

   /* system thread ID */
   int tid_valid() { return thread_ptr != nullptr; }
   std::unique_ptr<std::thread> thread_ptr;

   /* end-of-thread event */
   OS_Event* done_evt;
};

/* master thread list global */
extern class TadsThreadList* G_thread_list;

/* ------------------------------------------------------------------------ */
/*
 *   Watchdog thread.  This is an optional system thread that monitors the
 *   program's activity, and terminates the program if it appears to be
 *   either inactive or looping.  When TADS is running as a network server
 *   that's open to anyone's bytecode programs, we can't count on the
 *   underlying bytecode program being well-behaved.  The watchdog thread
 *   handles some common problem areas:
 *
 *   - If the bytecode program has a bug that gets it stuck in an infinite
 *   loop, it will consume as much CPU time as it's given, which will bring
 *   other processes on the server to a crawl.  The watchdog checks
 *   periodically to see if our process has been consuming a disproportionate
 *   amount of CPU time over a recent interval, and will terminate the
 *   process if so.  This will also shut down programs that are intentionally
 *   (not buggily) doing CPU-intensive computations for long stretches, but
 *   this is a reasonable policy anyway: shared TADS game servers are meant
 *   for interactive games, and if someone wants to run heavy-duty number
 *   crunching, they really shouldn't be using a shared game server for it.
 *
 *   - If the bytecode program has a long period without any network requests
 *   from clients, it probably has a bug in its internal session management
 *   code.  On a public server, a given interpreter session is meant to run
 *   only as long as it's actively being used by a client.  The standard TADS
 *   web server library has an internal watchdog that monitors for client
 *   activity, and shuts itself down after a period of inactivity.  However,
 *   we can't count on every bytecode program using the standard library, and
 *   even those that do could have bugs (in the library itself or in user
 *   code) that prevent the internal inactivity watchdog from working
 *   properly.  So, our system watchdog thread serves as a backup here; if we
 *   don't see any incoming client network requests for an extended period,
 *   we'll assume that the program no longer has any job to do, so we'll shut
 *   it down.  Note that a bytecode program could be intentionally running as
 *   a continuous service, e.g., acting as a web server; but that's really an
 *   abuse of a public TADS server, since a continuously resident process
 *   takes up a lot of memory that's meant to be shared with other users.  So
 *   we don't care whether the bytecode program is running without clients
 *   due to bugs or by design; in either case we want to shut it down.
 *
 *   The system administrator enables the watchdog thread by putting the
 *   variable "watchdog = yes" in the tadsweb.config file.
 */

/* the statistics we collect each time the watchdog wakes up */
struct watchdog_stats
{
   /* total user CPU time used as of this interval, in seconds */
   clock_t cpu_time;

   /* wall clock time (time() value) as of this interval, in seconds */
   time_t wall_time;
};

/*
 *   Watchdog thread object class
 */
class OSS_Watchdog : public OS_Thread
{
 public:
   OSS_Watchdog();
   ~OSS_Watchdog();
   virtual void thread_main();

   /* the watchdog thread 'quit' event */
   static OS_Event* quit_evt;

 private:
   /* number of times we've collected statistics */
   int cnt;

   /* next statistics write index */
   int idx;

   /*
    *   Statistics array.  We keep records for the latest NSTATS iterations.
    *   idx points to the next element to write, so idx-1 is the last one we
    *   wrote, and so on.  The array is circular, so the actual array index
    *   of element i is i % NSTATS.
    */
   static const int NSTATS = 10;
   watchdog_stats stats[NSTATS];

   /*
    *   get the nth most recent stats entry - 0 is the latest entry, 1 is
    *   the second older entry, etc
    */
   const watchdog_stats* get_stats(int i) const;
};

#endif /* OSNETWASM_H */
