#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */


/* 파일 디스크립터 상수 */
/* tests/userprog/no-vm/multi-oom PASS를 위해서는 page 개수 3 이상 */
#define FDT_PAGES 3
#define FDT_MAX FDT_PAGES * (1<<9)

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    int priority;                       /* Priority. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
    /* Table for whole virtual memory owned by thread. */
    struct supplemental_page_table spt;
#endif

    /* Owned by thread.c. */
    struct intr_frame tf;               /* Information for switching */
    unsigned magic;                     /* Detects stack overflow. */

/*
+=========================================+
|             Project 1 Thread            |
+=========================================+
*/
	/* Alarm_clock_1.1 해당 쓰레드가 깨어나야 할 tick을 저장할 필드 */
	/* Add new field for local tick, e.g. wakeup_tick​ (Use int64 type)​ */
	/* timer.c timer_sleep (int64_t ticks) 에서 형 참고 */
	int64_t wakeup_tick;

/* 	Priority_Scheduling_2.7 구조체 member 추가 */
/*  donation 이후 우선순위를 초기화하기 위해 초기 우선순위 값을 저장할 필드
    해당 쓰레드가 대기하고 있는 lock자료구조의 주소를 저장할 필드
    multiple donation을 고려하기 위한 리스트 추가
    해당 리스트를 위한 elem도 추가 */

	/* donation 전 original priority */
	int original_priority;

	/* wait_on_lock: lock that it waits for​ */
	/* 해당 쓰레드가 대기하고 있는 lock자료구조의 주소를 저장할 필드 */
	struct lock *wait_on_lock;

	/* Donations: list of Donors​ */
	/* priority 를 기부한 thread list */
	struct list donations;
	struct list_elem donation_elem;

/*
+=========================================+
|         Project 2 User Programs         |
+=========================================+
*/
	/* System_Call_2.4 File Descriptor & Manipulation​ */
	/* System_Call_2.4_1 thread 구조체 필드 추가 */
    int exit_status;

    /* 자식 프로세스 순회용 리스트 */
    struct list child_list;
    struct list_elem child_elem; 

    /* wait_sema 를 이용하여 자식 프로세스가 종료할때까지 대기 
    종료 상태 저장 */
    struct semaphore wait_sema;
    
    /* 자식에게 넘겨줄 intr_frame
    fork가 완료될때 까지 부모 대기 forksema
    자식 프로세스 종료상태를 부모가 받을때까지 종료 대기 free_sema */
    struct intr_frame parent_if;        /* 프로세스의 정보를 가진 자료구조 */
    struct semaphore fork_sema;
    struct semaphore free_sema;

    /* FDT & next_fd */
    struct file **fdt;
    int next_fd;

    /* 현재 실행 중인 파일 */
    /* tests/userprog/rox-child
     * tests/userprog/rox-multichild */
    struct file *running;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

/*
+=========================================+
|             Project 1 Thread            |
+=========================================+
*/
/* Alarm_clock_1.3 구현할 함수 선언 */
void thread_sleep(int64_t ticks); 				/* 실행 중인 스레드를 슬립으로 만듬 */
void thread_awake(int64_t ticks); 				/* 슬립큐에서 깨워야할 스레드를 깨움 */
void update_next_tick_to_awake(int64_t ticks); 	/* 최소 틱을 가진 스레드 저장 */
int64_t get_next_tick_to_awake(void); 			/* thread.c의 next_tick_to_awake 반환 */

/* thread.c */
/* Priority_Scheduling_2.2 함수 선언 (한양대 ppt 참고) */
/* 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케줄링 */
void test_max_priority (void);
/* 인자로 주어진 스레드들의 우선순위를 비교 (0 or 1 return) */
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool check_preemption();

/* thread.h + thread.c */
/* Priority_Scheduling_2.10 구현할 함수 선언 */
/* priority donation을 수행 */
void donate_priority(void);
/* donation list에서 스레드 엔트리를 제거 */
void remove_with_lock(struct lock *lock);
/* 우선순위를 다시 계산 */
void refresh_priority(void);

bool cmp_donation_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

#endif /* threads/thread.h */