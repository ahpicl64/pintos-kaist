#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. 함수 내 0으로 처리 */
	THREAD_READY,       /* Not running but ready to run. 함수 내 1으로 처리 */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. 함수 내 2으로 처리 */
	THREAD_DYING        /* About to be destroyed. 함수 내 3으로 처리*/
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

#define USERPROG // 하단에 정의된 	uint64_t *pml4  Page map level 4  인자를 유효하게 만들기 위해 선언

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

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
	int original_priority;		// 기증 받기 전 원래 우선순위를 저장할 필드
	struct lock *wait_lock;		// 해당 스레드가 획득을 기다리는 락. 이 락을 가지고 있는 스레드에게 우선순위를 기증하게됨
	int64_t wakeup_tick;		// 깨어날 시간을 저장할 필드

	struct list donors_list; // 우선순위를 기부한 스레드들을 저장. 우선순위가 큰 애들이 앞으로 옴.

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	struct list_elem donors;	// 기부자 리스트를 쓰기위한 element 요소 생성

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	/* Page map level 4, 페이지 테이블 최상위(4단계) 엔트리에 대한 포인터
	 * pml4_create() 함수로 생성되며
	 * palloc을 통해 물리메모리(커널영역) 내에 프로세스 별 페이지 테이블이 할당됨
	 * 이 때 상위 절반(256~511 엔트리)는 항상 모든 프로세스 동일한 커널 공간으로 매핑
	 * -> 시스템콜, 페이지 폴트 등 커널 진입시 커널코드를 실행하기위해 동일하게 매핑 */
	uint64_t *pml4;
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

extern struct list ready_list;

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

bool compare_priority(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED);
bool compare_donor_priority(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED);
void update_effective_priority(struct thread* t);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
