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

/* Priority_Scheduling_2.4 함수 선언 및 추가 */
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);


/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
/* 	세마포어 SEMA를 VALUE로 초기화합니다. 
	세마포어는 이를 조작하기 위한 두 개의 원자 연산자와 함께 음수가 아닌 정수입니다.

    - down 또는 "P": 값이 양수가 될 때까지 기다린 다음 감소시킵니다.
    - up 또는 "V": 값을 증가시킵니다(그리고 대기 중인 스레드가 있는 경우 깨우기). 
*/
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
/* 	세마포어에서 Down 또는 "P" 작업.
	SEMA의 값이 양수가 될 때까지 기다렸다가 원자적으로 감소시킵니다.

    이 함수는 잠들 수 있으므로 인터럽트 처리기 내에서 호출하면 안 됩니다.
	이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만 
	휴면 상태이면 다음 예약된 스레드가 인터럽트를 다시 켤 것입니다. */
/* semaphore를 요청하고 획득했을 때 value를 1 낮춤​ */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// Semaphore를 얻고 waiters 리스트 삽입 시, 우선순위대로 삽입되도록 수정
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, cmp_priority, NULL);
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
sema_try_down (struct semaphore *sema) {
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
/* 	세마포어에서 Up 또는 "V" 연산.
	SEMA의 값을 증가시키고 SEMA를 기다리는 스레드 중 하나를 깨웁니다.

    이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
/* semaphore를 반환하고 value를 1 높임 */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	bool flag = false;

/*	waiter list에 있는 쓰레드의 우선순위가 변경 되었을 경우를 고려하여 waiter list를 정렬 (list_sort)
	세마포어 해제 후 priority preemption 기능 추가 */
	if (!list_empty (&sema->waiters)) {
		flag = true;
		list_sort(&sema->waiters, cmp_priority, 0);
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}

	sema->value++;
	if(flag && check_preemption())
		thread_yield();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
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
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* synch.h + synch.c */
/* Priority_Scheduling_2.4 함수 선언 및 추가 */
// bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
// /*  semaphore_elem으로부터 각 semaphore_elem의 쓰레드 디스크립터를 획득.
//     첫 번째 인자의 우선순위가 두 번째 인자의 우선순위보다 높으면 1을 반환 낮으면 0을 반환
// */
//     struct semaphore_elem *sem_a = list_entry(a, struct semaphore_elem, elem);
// 	struct semaphore_elem *sem_b = list_entry(b, struct semaphore_elem, elem);

//     if (list_entry(sem_a->semaphore.waiters, ) > sem_b->semaphore.value)
//         return 1;
//     else
//         return 0;
// }

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
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* thread.c lock_acquire() */
/* Priority_Scheduling_2.9 lock_acquire() 함수 수정 */
/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 	필요한 경우 사용할 수 있을 때까지 잠자기 상태로 LOCK을 획득합니다.
	잠금은 현재 스레드에서 이미 보유하고 있지 않아야 합니다.

    이 함수는 잠들 수 있으므로 인터럽트 처리기 내에서 호출하면 안 됩니다.
	이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만
	휴면이 필요한 경우 인터럽트가 다시 켜집니다. */
/*  해당 lock의 holder가 존재하여 wait을 하게 되면 아래 동작을 수행.
    wait을 하게 될 lock 자료구조 포인터 저장
    (쓰레드 디스크립터에 추가한 필드)
	lock의 현재 holder의 donations list에 추가
    priority donation을 수행하는 코드 추가
    구현하게 될 함수인 donate_priority();
    lock 획득 후 lock의 holder 갱신 */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	// 해당 lock의 holder가 존재하는가? yes->wait
	if (lock->holder) {	// holder 기본값 NULL
		// lock 자료구조 포인터 저장
		thread_current()->wait_on_lock = lock;
		// lock의 현재 holder의 donations list에 추가
		list_insert_ordered(&lock->holder->donations, &thread_current()->donation_elem, cmp_priority, NULL);
		// list_push_back(&lock->holder->donations, &thread_current()->donation_elem);
		donate_priority(); 
	}
	
	sema_down (&lock->semaphore);

	thread_current()->wait_on_lock = NULL;

	// lock 획득 후 lock의 holder 갱신
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/* 	현재 스레드가 소유해야 하는 LOCK을 해제합니다.
    이것이 lock_release 기능입니다.

    인터럽트 핸들러는 잠금을 획득할 수 없으므로 
	인터럽트 핸들러 내에서 잠금을 해제하려고 시도하는 것은 
	이치에 맞지 않습니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	/* Priority_Scheduling_2.12 lock_release() 함수 수정 */
	// sema_up (&lock->semaphore); 이전에 수행!
	// 현재 쓰레드의 대기 리스트 갱신
	remove_with_lock(lock);
	// 원래의 priorit로 초기화
	refresh_priority();

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
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
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	// list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		// list_sort(&cond->waiters, cmp_sem_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
