#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Alarm_clock_1.2 */
/* sleep_list 선언, kaist_ppt 참고 */
static struct list sleep_list;
int64_t next_tick_to_awake = INT64_MAX;


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);


/* Alarm_clock_1.5 맨 위 함수 선언 */
void thread_sleep(int64_t ticks);              /* 실행 중인 스레드를 슬립으로 만듬 */
void thread_awake(int64_t ticks);              /* 슬립큐에서 깨워야할 스레드를 깨움 */
void update_next_tick_to_awake(int64_t ticks); /* 최소 틱을 가진 스레드 저장 */
int64_t get_next_tick_to_awake(void);          /* thread.c의 next_tick_to_awake 반환 */


/* Priority_Scheduling_2.2 함수 선언 (한양대 ppt 참고) */
void test_max_priority (void);
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);


/* Priority_Scheduling_2.10 구현할 함수 선언 */
void donate_priority(void);					/* priority donation을 수행 */
void remove_with_lock(struct lock *lock);	/* donation list에서 스레드 엔트리를 제거 */
void refresh_priority(void);				/* 우선순위를 다시 계산 */
bool cmp_donation_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);



/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
    ASSERT (intr_get_level () == INTR_OFF);

    /* Reload the temporal gdt for the kernel
     * This gdt does not include the user context.
     * The kernel will rebuild the gdt with user context, in gdt_init (). */
    struct desc_ptr gdt_ds = {
        .size = sizeof (gdt) - 1,
        .address = (uint64_t) gdt
    };
    lgdt (&gdt_ds);

    /* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	/* Alarm_clock_1.2 */
	list_init (&sleep_list);    // ready_list 만 사용하는건 비효율
	list_init (&destruction_req);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
    struct thread *t = thread_current ();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pml4 != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
        thread_func *function, void *aux) {
    struct thread *t;
    tid_t tid;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();


    /* SYSCALL 추가 */
    /* 현재 스레드의 자식 리스트에 새로 생성한 스레드 추가 */
    struct thread *curr = thread_current();
    list_push_back(&curr->child_list,&t->child_elem);

    /* 파일 디스크립터 초기화 */
    t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
    if(t->fdt == NULL)
        return TID_ERROR;
    t->next_fd = 2;
    t->fdt[0] = 1;
    t->fdt[1] = 2;
    /* SYSCALL 추가 */


    /* Call the kernel_thread if it scheduled.
     * Note) rdi is 1st argument, and rsi is 2nd argument. */
    t->tf.rip = (uintptr_t) kernel_thread;
    t->tf.R.rdi = (uint64_t) function;
    t->tf.R.rsi = (uint64_t) aux;
    t->tf.ds = SEL_KDSEG;
    t->tf.es = SEL_KDSEG;
    t->tf.ss = SEL_KDSEG;
    t->tf.cs = SEL_KCSEG;
    t->tf.eflags = FLAG_IF;

/* 	Priority_Scheduling_2.3 함수 수정 */
/*  ready_list 에 스레드 삽입 우선 순위대로. (확장 불가능하다는 점에 유의하십시오)​
    스레드가 ready_list 에 추가되면 새 스레드의 우선순위와 현재 스레드의 우선순위를 비교합니다.
    새 스레드의 우선순위가 더 높으면 schedule() 을 호출합니다. (현재 스레드가 CPU를 양보함)
*/
    /* Add to run queue. */
    thread_unblock (t);

    /*  현재 실행 중인 스레드와 새로 삽입된 스레드의 우선순위를 비교합니다. 
        새로 도착한 쓰레드의 우선순위가 더 높으면 CPU를 양보 */
    if(check_preemption()) thread_yield();

    return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);
    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);

/* 	Priority_Scheduling_2.1 */
/*  스레드가 차단 해제되면 우선순위대로 ready_list 에 삽입됩니다.​ 
    스레드 차단을 해제할 때 list_push_back 대신 list_inert_ordered 를 사용하세요.​ */
    // list_push_back (&ready_list, &t->elem);
    list_insert_ordered (&ready_list, &t->elem, cmp_priority, 0);
    t->status = THREAD_READY;
    intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
    return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
    struct thread *t = running_thread ();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    /* Just set our status to dying and schedule another process.
       We will be destroyed during the call to schedule_tail(). */
    intr_disable ();
    do_schedule (THREAD_DYING);
    NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
    struct thread *curr = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable ();
    if (curr != idle_thread)
        /* Priority_Scheduling_2.3 함수 수정 */
		// list_push_back (&ready_list, &curr->elem);
        list_insert_ordered (&ready_list, &curr->elem, cmp_priority, 0);

    do_schedule (THREAD_READY);
    intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
/* 현재 수행중인 스레드의 우선순위를 변경 */
void
thread_set_priority (int new_priority) {
	thread_current ()->original_priority = new_priority;

/*  Priority_Scheduling_2.15 thread_set_priority() 함수 수정 */
    refresh_priority();

/* 	Priority_Scheduling_2.3 함수 수정 */
/*  현재 쓰레드의 우선 순위와 ready_list에서 가장 높은 우선 순위를 비교
    이후, 스케쥴링 하는 함수 호출
    Reorder the ready_list​ */
	// test_max_priority();
	if(check_preemption()) thread_yield();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
    // return thread_current ()->priority;
    enum intr_level old_level = intr_disable();

	int ret = thread_current()->priority;
	
    intr_set_level(old_level);
	return ret;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
/*	유휴 스레드. 실행할 준비가 된 다른 스레드가 없을 때 실행됩니다.

    유휴 스레드는 처음에 thread_start()에 의해 준비 목록에 놓입니다.
	그것은 처음에 한 번 예약되며, 이때 idle_thread를 초기화하고 
	thread_start()가 계속되도록 전달된 세마포어를 "업"하고 즉시 차단합니다. 
	그 이후에는 유휴 스레드가 준비 목록에 나타나지 않습니다. 
	ready list가 비어있을 때 특별한 경우로 next_thread_to_run()에 의해 반환됩니다. */
static void
idle (void *idle_started_ UNUSED) {
    struct semaphore *idle_started = idle_started_;

    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;) {
        /* Let someone else run. */
        intr_disable ();
        thread_block ();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
    ASSERT (function != NULL);

    intr_enable ();       /* The scheduler runs with interrupts off. */
    function (aux);       /* Execute the thread function. */
    thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
    t->priority = priority;
    t->magic = THREAD_MAGIC;

    /* Priority_Scheduling_2.8 구조체 member 초기화 */
	t->original_priority = priority;
	t->wait_on_lock = NULL;     // lock_init 참고
	list_init (&t->donations);
    // donation_elem 초기화

    /* SYSCALL 추가 */
    /* 자식 리스트 및 세마포어 초기화 */
    list_init(&t->child_list);
    sema_init(&t->wait_sema,0);
    sema_init(&t->fork_sema,0);
    sema_init(&t->free_sema,0);

    // t->exit_status = 0;
    t->running = NULL;
    /* SYSCALL 추가 */
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
    if (list_empty (&ready_list))
        return idle_thread;
    else
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
    __asm __volatile(
            "movq %0, %%rsp\n"
            "movq 0(%%rsp),%%r15\n"
            "movq 8(%%rsp),%%r14\n"
            "movq 16(%%rsp),%%r13\n"
            "movq 24(%%rsp),%%r12\n"
            "movq 32(%%rsp),%%r11\n"
            "movq 40(%%rsp),%%r10\n"
            "movq 48(%%rsp),%%r9\n"
            "movq 56(%%rsp),%%r8\n"
            "movq 64(%%rsp),%%rsi\n"
            "movq 72(%%rsp),%%rdi\n"
            "movq 80(%%rsp),%%rbp\n"
            "movq 88(%%rsp),%%rdx\n"
            "movq 96(%%rsp),%%rcx\n"
            "movq 104(%%rsp),%%rbx\n"
            "movq 112(%%rsp),%%rax\n"
            "addq $120,%%rsp\n"
            "movw 8(%%rsp),%%ds\n"
            "movw (%%rsp),%%es\n"
            "addq $32, %%rsp\n"
            "iretq"
            : : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
    uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
    uint64_t tf = (uint64_t) &th->tf;
    ASSERT (intr_get_level () == INTR_OFF);

    /* The main switching logic.
     * We first restore the whole execution context into the intr_frame
     * and then switching to the next thread by calling do_iret.
     * Note that, we SHOULD NOT use any stack from here
     * until switching is done. */
    __asm __volatile (
            /* Store registers that will be used. */
            "push %%rax\n"
            "push %%rbx\n"
            "push %%rcx\n"
            /* Fetch input once */
            "movq %0, %%rax\n"
            "movq %1, %%rcx\n"
            "movq %%r15, 0(%%rax)\n"
            "movq %%r14, 8(%%rax)\n"
            "movq %%r13, 16(%%rax)\n"
            "movq %%r12, 24(%%rax)\n"
            "movq %%r11, 32(%%rax)\n"
            "movq %%r10, 40(%%rax)\n"
            "movq %%r9, 48(%%rax)\n"
            "movq %%r8, 56(%%rax)\n"
            "movq %%rsi, 64(%%rax)\n"
            "movq %%rdi, 72(%%rax)\n"
            "movq %%rbp, 80(%%rax)\n"
            "movq %%rdx, 88(%%rax)\n"
            "pop %%rbx\n"              // Saved rcx
            "movq %%rbx, 96(%%rax)\n"
            "pop %%rbx\n"              // Saved rbx
            "movq %%rbx, 104(%%rax)\n"
            "pop %%rbx\n"              // Saved rax
            "movq %%rbx, 112(%%rax)\n"
            "addq $120, %%rax\n"
            "movw %%es, (%%rax)\n"
            "movw %%ds, 8(%%rax)\n"
            "addq $32, %%rax\n"
            "call __next\n"         // read the current rip.
            "__next:\n"
            "pop %%rbx\n"
            "addq $(out_iret -  __next), %%rbx\n"
            "movq %%rbx, 0(%%rax)\n" // rip
            "movw %%cs, 8(%%rax)\n"  // cs
            "pushfq\n"
            "popq %%rbx\n"
            "mov %%rbx, 16(%%rax)\n" // eflags
            "mov %%rsp, 24(%%rax)\n" // rsp
            "movw %%ss, 32(%%rax)\n"
            "mov %%rcx, %%rdi\n"
            "call do_iret\n"
            "out_iret:\n"
            : : "g"(tf_cur), "g" (tf) : "memory"
            );
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (thread_current()->status == THREAD_RUNNING);
    while (!list_empty (&destruction_req)) {
        struct thread *victim =
            list_entry (list_pop_front (&destruction_req), struct thread, elem);
        palloc_free_page(victim);
    }
    thread_current ()->status = status;
    schedule ();
}

static void
schedule (void) {
    struct thread *curr = running_thread ();
    struct thread *next = next_thread_to_run ();

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (curr->status != THREAD_RUNNING);
    ASSERT (is_thread (next));
    /* Mark us as running. */
    next->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate (next);
#endif

    if (curr != next) {
        /* If the thread we switched from is dying, destroy its struct
           thread. This must happen late so that thread_exit() doesn't
           pull out the rug under itself.
           We just queuing the page free reqeust here because the page is
           currently used bye the stack.
           The real destruction logic will be called at the beginning of the
           schedule(). */
        if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
            ASSERT (curr != next);
            list_push_back (&destruction_req, &curr->elem);
        }

        /* Before switching the thread, we first save the information
         * of current running. */
        thread_launch (next);
    }
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}



/*
+=========================================+
|                 PROJECT 1               |
+=========================================+
*/
/*  Alarm_clock_1.6
    thread를 sleep queue에 삽입하고 blocked 상태로 만들어 대기
    해당 과정중에는 인터럽트를 받아들이지 않는다.
    devices/timer.c : timer_sleep() 함수에 의해 thread_yield() 함수 대신 호출
*/
/* 스레드를 ticks시각 까지 재우는 함수*/
void thread_sleep(int64_t ticks) {
    /* thread_yield() 참고 */
    struct thread *cur = thread_current();     // idle 스레드는 sleep 되지 않아야한다.
    enum intr_level old_level = intr_disable(); //이전 인터럽트 레벨을 저장하고 인터럽트 방지

    ASSERT(cur != idle_thread);

    /* 현재 스레드를 슬립 큐에 삽입한 후에 스케줄한다. */
    list_push_back(&sleep_list, &cur->elem);

    // awake함수가 실행되어야 할 tick값을 update
    cur->wakeup_tick = ticks;
    // if (curr != idle_thread)
	// 	list_push_back (&sleep_list, &curr->elem);
    update_next_tick_to_awake(ticks);

    /* 이 스레드를 블락하고 다시 스케줄될 때 까지 블락된 상태로 대기*/
    // 이것도 가능!
	// do_schedule (THREAD_BLOCKED);
    thread_block();

    /* 인터럽트를 다시 받아들이도록 수정 */
    intr_set_level(old_level);
}

/* thread_awake 다른 버전 아래에 주석처리 */
/* 자고 있는 스레드 중에 깨어날 시각이 ticks 시각이 지난 애들을 모조리 깨우는 하수 */
void thread_awake(int64_t ticks) {
    next_tick_to_awake = INT64_MAX;
    struct list_elem *e = list_begin(&sleep_list);

    while(e != list_end(&sleep_list)) {
        struct thread *t = list_entry(e, struct thread, elem);
        if(ticks >= t->wakeup_tick) {
            e = list_remove(&t->elem);
            thread_unblock(t);
        }
        else {
            e = list_next(e);
            update_next_tick_to_awake(t->wakeup_tick);
        }
    }
}

/* 가장 먼저 일어나야할 스레드를 비교하여 새로운 값이 작을 경우 변경*/
void update_next_tick_to_awake(int64_t ticks) {
    // if (next_tick_to_awake > ticks)
    //     next_tick_to_awake = ticks;
    
    /* next_tick_to_awake 가 깨워야할 스레드의 깨어날 tick값 중 가장 작은 tick을 갖도록 업데이트 한다. */
    next_tick_to_awake = (ticks < next_tick_to_awake) ? ticks : next_tick_to_awake;
}

/* 가장 먼저 일어나야할 스레드가 일어날 시각을 반환*/
int64_t get_next_tick_to_awake(void) {
    return next_tick_to_awake;
}

/* thread.c */
/* Priority_Scheduling_2.2 함수 선언 (한양대 ppt 참고) */
/* 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케줄링 */
// 첫번째 스레드가 cpu 점유 중인 스레드 보다 우선순위가 높으면 cpu 점유를 양보하는 함수
void test_max_priority(void) {
// /*  ready_list에서 우선 순위가 가장 높은 쓰레드와 현재 쓰레드의 우선 순위를 비교.
//     현재 쓰레드의 우선수위가 더 작다면 thread_yield()
// */
// /*
// 	struct thread *curr = thread_current();
// 	struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);

//     if (!list_empty (&ready_list) && curr->priority < t->priority)
//         thread_yield ();
// */
// 	// 조건 !list_empty (&ready_list) 를 뒤에서 검사하면 fail
//     if (!list_empty(&ready_list) && thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority)
//         thread_yield ();

    struct thread *cur = thread_current();
    if (!list_empty(&ready_list) && cmp_priority(list_front(&ready_list), cur, 0)){
        thread_yield();
    }
}

/* Priority_Scheduling_2.2 함수 선언 (한양대 ppt 참고) */
/* 인자로 주어진 스레드들의 우선순위를 비교 */
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
//     /*  첫 번째 인자의 우선순위가 높으면 1을 반환, 
//     두 번째 인자의 우선순위가 높으면 0을 반환
//     list_insert_ordered() 함수에서 사용
// */
// 	struct thread *thread_a = list_entry(a, struct thread, elem);
// 	struct thread *thread_b = list_entry(b, struct thread, elem);

//     if (thread_a->priority > thread_b->priority)
//         return 1;
//     else
//         return 0;
    
    return list_entry(a, struct thread, elem) ->priority > list_entry(b, struct thread, elem) -> priority;
}

bool check_preemption() {
    if(list_empty(&ready_list)) return false;
    return list_entry(list_front(&ready_list), struct thread, elem) -> priority > thread_current() -> priority;
}


bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
//     /*  첫 번째 인자의 우선순위가 높으면 1을 반환, 
//     두 번째 인자의 우선순위가 높으면 0을 반환
//     list_insert_ordered() 함수에서 사용
// */
// 	struct thread *thread_a = list_entry(a, struct thread, donation_elem);
// 	struct thread *thread_b = list_entry(b, struct thread, donation_elem);

//     if (thread_a->priority > thread_b->priority)
//         return 1;
//     else
//         return 0;
    
    return list_entry(a, struct thread, donation_elem)->priority > list_entry(b, struct thread, donation_elem)->priority;
}

/* Priority_Scheduling_2.11 donate_priority() 함수 추가 */
/*  priority donation을 수행하는 함수
    Nested donation을 고려하여 구현
    현재 쓰레드가 기다리고 있는 lock과 연결된 모든 쓰레드들을 순회하며,
    현재 쓰레드의 우선순위를 lock을 보유하고 있는 쓰레드에게 기부
    현재 쓰레드가 기다리고 있는 락의 holder -> holde가 기다리고 있는 lock의 holder
    (nested depth는 8로 제한) */
/* 자신의 priority를 필요한 lock을 점유하고 있는 thread에게 빌려주는 함수 */
void donate_priority(void) {
    int nested_depth;
    struct thread *cur = thread_current();

    // 일단 8로 설정. 근데 왜 8? -> test case = 8 때문이다~
    for(nested_depth = 0; nested_depth < 8; nested_depth++) {
        if(!cur->wait_on_lock) break;
        struct thread *holder = cur->wait_on_lock->holder;
        // 난 필요하다 느낌... 하지만 필요없다 함...ㅠ
		// if (holder->priority < curr->priority)
        // 	holder->priority = curr->priority;
		holder->priority = cur->priority;
        // ERROR!! cur 생성
		// thread_current() = holder;
        cur = holder;
    }
}

/* Priority_Scheduling_2.13 remove_with_lock() 함수 추가 */
/*  lock을 해지 했을 때, 
    waiters 리스트에서 해당 엔트리를 삭제 하기 위한 함수를 구현
    
    현재 쓰레드의 waiters 리스트를 확인하여 
    해지할 lock을 보유하고 있는 엔트리를 삭제 */
/* 자신에게 priority를 빌려준 thread들을 donations 리스트에서 지우는 함수 */
void remove_with_lock(struct lock *lock) {
    // struct list_elem *d_elem = list_begin(&thread_current()->donations);

    // while (d_elem != list_end (&thread_current()->donations)) {
    //     if (list_entry(d_elem, struct thread, donation_elem)->wait_on_lock == lock)
    //         list_remove(&list_entry(d_elem, struct thread, donation_elem)->donation_elem);
    //     d_elem = list_next(d_elem);
    // }

    struct list_elem *d_elem;
    struct thread *cur = thread_current();

	for (d_elem = list_begin(&cur->donations); d_elem!= list_end(&cur->donations); d_elem = list_next(d_elem)){
		struct thread *t = list_entry(d_elem, struct thread, donation_elem);
		if(t->wait_on_lock == lock) {
			list_remove(&t->donation_elem);
		}
	}
}

/* Priority_Scheduling_2.14 refresh_priority() 함수 추가 */
/*  스레드의 우선순위가 변경 되었을 때, 
    donation을 고려하여 우선순위를 다시 결정하는 함수
    현재 쓰레드의 우선순위를 기부 받기 전의 우선순위로 변경
    현재 쓰레드의 waiters 리스트에서 가장 높은 우선순위를 
    현재 쓰레드의 우선순위와 비교 후 우선순위 설정 */
void refresh_priority(void) {
    // struct thread *curr = thread_current();
	// struct thread *t;

	// curr->priority = curr->original_priority;

	// if (!list_empty(&curr->donations)) {
	// 	list_sort(&curr->donations, cmp_donation_priority, NULL);
		
	// 	t = list_entry(list_front(&curr->donations), struct thread, donation_elem);
	// 	if(t->priority > curr->priority)
	// 		curr->priority = t->priority;
	// }

    struct thread *cur = thread_current();
    cur->priority = cur->original_priority;
   /* cur->donation에 thread가 남아있다면, 
    * 그 안의 thread들을 priority에 따라 정렬한 후에 높은 우선순위(dontation list 가장 앞에 있는 thread의 priority)를 cur thread의 priority로 설정한다.
    */
    if(!list_empty(&cur->donations)) {
        list_sort(&cur->donations, cmp_donation_priority, 0);
        struct thread *front = list_entry(list_front(&cur->donations), struct thread, donation_elem);
        cur->priority = front->priority > cur->priority ? front->priority: cur->priority; 
    }
}

// void thread_awake(int64_t ticks) {
//     struct list_elem *element_sleep_list = list_begin(&sleep_list);
//     // int64_t temp_wake = next_tick_to_awake;
//     next_tick_to_awake = INT64_MAX;
//     struct thread *curr_thread;

// 	// enum intr_level old_level;
// 	// old_level = intr_disable ();
	
//     while (element_sleep_list != list_end (&sleep_list)) {
// 		curr_thread = list_entry(element_sleep_list, struct thread, elem);

//         if (curr_thread->wakeup_tick <= ticks) {
// 			/*  Alarm_clock_1.6 */
// 			/*
//                 thread_unblock()을 list_remove() 보다 먼저 실행 시,

//                 list_push_back (struct list *list, struct list_elem *elem) {
// 	                list_insert (list_end (list), elem);
//                 }

//                 list_insert (struct list_elem *before, struct list_elem *elem) {
// 	                ```
// 	                elem->prev = before->prev;
// 	                elem->next = before;
//                     ```
//                 }

//                 (list_push_back() 에서의 매개변수 *list 는 &ready_list 를 받는다.)
//                 이렇게 해당 list_elem 가 ready_list 와 연결되며,
//                 list_remove() 실행 시,

//                 list_remove (struct list_elem *elem) {
// 	                ```
// 	                elem->prev->next = elem->next;
// 	                elem->next->prev = elem->prev;
// 	                ```
//                 }
//                 이렇게 ready_list 에서의 삭제가 발생한다.
//             */
// 			// do_schedule (THREAD_READY);
// 			// thread_unblock(curr_thread);
// 			element_sleep_list = list_remove(element_sleep_list);
// 			thread_unblock(curr_thread);
//         }
//         else {
//             update_next_tick_to_awake(curr_thread->wakeup_tick);
//             element_sleep_list = list_next(element_sleep_list);
//         }
//     }
// 	// intr_set_level (old_level);
// }