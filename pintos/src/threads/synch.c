/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}





/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;
  bool yield_flag;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
    {
      //Find the max element
      struct list_elem * max_elem = list_max(&sema->waiters, priority_thread_less, NULL);
      //Remove it from the list
      struct thread * max_thread = list_entry (max_elem, struct thread, elem);
      //printf("max thread = %s\n", max_thread->name);
      list_remove (max_elem);
      thread_unblock (max_thread);

      if(max_thread->effective_priority > thread_current()->effective_priority) {
        yield_flag = true;
      }
  }
  sema->value++;

  if(!intr_context() && yield_flag) {
    thread_yield();
  }

  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}
/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/*
Recursively donates priority to lock holders

*/

void 
donate_priority_rec(int rec_level, struct lock * desired_lock, struct thread * donor) {
  //if donor has not previously been waiting on the lock set its waiting lock
  if(&donor->waiting_lock == NULL) {
    donor->waiting_lock = desired_lock;
  }
  struct thread * donee = desired_lock->holder;//the thread we're donating to
  sema_down(&donee->priority_sema);
  sema_down(&donor->priority_sema);
  if(donee->effective_priority < donor->effective_priority) { //check if the new priority is higher
    donee->effective_priority = donor->effective_priority; //set priority
    list_push_back(&donee->donation_list, &donor->donor_elem); //add donor to donee's list of donators
    sema_up(&donor->priority_sema);
    sema_up(&donee->priority_sema);
    //if donee is waiting on a lock, recursively donate donees new priority
    if(donee->waiting_lock != NULL && rec_level > 0) { 
      donate_priority_rec(rec_level--, donee->waiting_lock, donee);
    }
  }
  else {
    sema_up(&donor->priority_sema);
    sema_up(&donee->priority_sema);
  }

}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  // try and down semaphore
  // if success we can get the lock
  bool success;
  success = sema_try_down (&lock->semaphore);
  if (success) {
    lock->holder = thread_current ();
  }
  //else donate priority to lock->holder recursive
  else {
    donate_priority_rec(DONATION_REC_LEVEL, lock, thread_current());
    sema_down(&lock->semaphore);//call normal sema-down to block current thread
    lock->holder = thread_current ();
  }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  lock->holder = NULL;
  sema_up (&lock->semaphore);
  revoke_priority_donation(lock);
}

static bool
priority_donate_less (const struct list_elem *a, const struct list_elem *b,
            void *aux UNUSED)
{
 
  struct thread *thread_a = list_entry(a, struct thread, donor_elem);
  struct thread *thread_b = list_entry(b, struct thread, donor_elem);

  return thread_a->effective_priority < thread_b->effective_priority;
}

void
revoke_priority_donation(struct lock * releasing_lock) {
  //iterate through list of current thread's donators
  sema_down(&releasing_lock->holder->priority_sema);
  int donor_mem = &releasing_lock->holder->donation_list;
  printf("donor memory address = %i", donor_mem);
  struct list_elem  *e;
  printf("priority_sema downed \n");
  for (e = list_begin (&releasing_lock->holder->donation_list);e != list_end (&releasing_lock->holder->donation_list);e = list_next (e))
    {
    // printf("iterating thru donor list \n");
    printf("donor list size =  %i\n", list_size(&releasing_lock->holder->donation_list) );
  //find the threads which donated priority for the current lock
      struct thread * t = list_entry(e, struct thread, donor_elem);
      if(t->waiting_lock == releasing_lock){
        //remove them from the list
        list_remove(e);
      }
    }
  printf("iterarated thru donor list \n");
  //if the list is not now empty find the list's max priority thread
    if(!list_empty(&releasing_lock->holder->donation_list)) {
      printf("donor list not empty \n");
      //and set the current thread's effective priority to that thread's priority
      struct list_elem * max_elem = list_max(&releasing_lock->holder->donation_list, priority_donate_less, NULL);
      //Remove it from the list
      struct thread * max_thread = list_entry (max_elem, struct thread, donor_elem);

      sema_down(&max_thread->priority_sema);
      printf("donor list not empty max threads donor sem down \n");
      releasing_lock->holder->effective_priority = max_thread->effective_priority;
      sema_up(&max_thread->priority_sema);
      printf("donor list not empty max threads donor sem up \n");

    }
    else {
      printf("donor list empty \n");
      //set the current thread's priority to its base priority
      releasing_lock->holder->effective_priority = releasing_lock->holder->priority;
    } 
    sema_up(&releasing_lock->holder->priority_sema); 
    printf("end \n");
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

static bool
priority_cond_less (const struct list_elem *a, const struct list_elem *b,
            void *aux UNUSED)
{
  const struct semaphore_elem *sem_a = list_entry (a, struct semaphore_elem, elem);
  const struct semaphore_elem *sem_b = list_entry (b, struct semaphore_elem, elem);
 
  struct thread *thread_a = list_entry(list_front(&sem_a->semaphore.waiters), struct thread, elem);
  struct thread *thread_b = list_entry(list_front(&sem_b->semaphore.waiters), struct thread, elem);

  return thread_a->effective_priority < thread_b->effective_priority;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) {
    if(list_size(&cond->waiters) > 1) {
      struct list_elem * max_elem = list_max(&cond->waiters, priority_cond_less, NULL);
      //Remove it from the list
      struct semaphore * max_semaphore = &list_entry (max_elem, struct semaphore_elem, elem)->semaphore;
      //printf("max thread = %s\n", max_thread->name);
      list_remove (max_elem);
      sema_up(max_semaphore);
    } else {
      sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
    }
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
