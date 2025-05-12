#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참고하세요. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
/* OS가 부팅된 이후 경과한 타이머 틱 수 */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
/* 타이머 틱당 루프 횟수
   timer_calibrate()에 의해 초기화됨 */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static bool cmp_wakeup_tick(const struct list_elem* a, const struct list_elem* b, void* aux);


// 깨어날 스레드를 모아두는 리스트
static struct list sleep_list;

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
/* 8254 프로그래머블 인터벌 타이머(PIT)를 설정하여
   초당 PIT_FREQ번 인터럽트가 발생하도록 하고,
   해당 인터럽트를 등록함 */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값을 반올림하여 사용 */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	// sleeplist 초기화
	list_init(&sleep_list);

	/* CW: 카운터 0, LSB 후 MSB, 모드 2, 2진수 */
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
/* 짧은 지연을 구현하기 위해 사용되는 loops_per_tick을 보정함 */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two still less than one timer tick. */
	/* loops_per_tick을 한 타이머 틱보다 작은 가장 큰 2의 거듭제곱 값으로 근사함 */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	/* loops_per_tick의 다음 8비트를 더 정밀하게 보정 */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
/* OS가 부팅된 이후 경과한 타이머 틱 수를 반환 */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
/* THEN(이전 timer_ticks()의 반환값) 이후 경과한 타이머 틱 수를 반환 */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
/* 약 TICKS 틱 동안 실행을 일시 중단 */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks (); // 현재 스레드가 시작된 시간을 가져옴
	int64_t wakeup = start + ticks; // sleep 종료시간 획득 (시작 시간 + 일시 정지할 시간)

	ASSERT (intr_get_level () == INTR_ON); // timer_sleep()이 호출 시 '반드시 인터럽트가 켜져 있어야 하는' 전제조건 명시

	/* 인터럽트 비활성화 */
	enum intr_level old_level = intr_disable ();

	/* 깨어날 시각 기록 후 리스트에 삽입 */
	struct thread* cur = thread_current(); // 현재 thread 상태 기록
	cur -> wakeup_tick = wakeup; // 현재 thread 내 wakeup tick을 기록

	/* sleep_list 내부에 wakeup 값을 오름차순 정렬된 상태로 넣기 위해 list_insert_ordered 함수 사용
	   이는 list 가장 앞만 검사했을때 먼저 끝나야 하는 작업들이 나올 수 있도록 */
	list_insert_ordered(&sleep_list, &cur -> elem, cmp_wakeup_tick, NULL);

	/* 스레드 블록 */
	thread_block();

	/* 인터럽트 복원 */
	intr_set_level(old_level);
	/* busy-wait 방식에서만 사용
	while (timer_elapsed (start) < ticks)
		thread_yield ();*/
}

bool
cmp_wakeup_tick(const struct list_elem* a, const struct list_elem* b, void* aux)
{
	struct thread *t1 = list_entry (a, struct thread, elem);
	struct thread *t2 = list_entry (b, struct thread, elem);
	return t1->wakeup_tick < t2->wakeup_tick;
}

/* Suspends execution for approximately MS milliseconds. */
/* 약 MS 밀리초 동안 실행을 일시 중단 */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
/* 약 US 마이크로초 동안 실행을 일시 중단 */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
/* 약 NS 나노초 동안 실행을 일시 중단 */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
/* 타이머 통계를 출력 */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
/* 타이머 인터럽트 핸들러 */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++; // 타이머 틱 수 1 증가
	enum intr_level old_level = intr_disable (); // 현재 인터럽트 상태 비활성화(언블록)
	while (!list_empty (&sleep_list)) // 깨어날 스레드 리스트 검사 시작 (비어있다면 넘어감)
	{
		/* 다음에 깨워야 할 스레드(sleep_list의 head)를 가져옴 */
		struct thread *t = list_entry (list_front (&sleep_list),
			struct thread, elem);
		/* 스레드의 깨우기 시간이 현재 틱수보다 크면 종료, 같을때 넘어감*/
		if (t->wakeup_tick > ticks)
			break;
		list_pop_front(&sleep_list); // 리스트에서 스레드를 제거(가장 앞)
		thread_unblock(t); // 스레드를 블록 상태에서 해제함
	}
	intr_set_level (old_level);		// 인터럽트 상태 복원
	thread_tick(); // 스케쥴러에 타이머 틱 발생 알림
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
/* LOOPS번 반복 시 한 타이머 틱 이상 대기하면 true,
   그렇지 않으면 false 반환 */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	/* 다음 타이머 틱이 발생할 때까지 대기 */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	/* 틱 수가 변했다면, 반복 시간이 너무 길었던 것임 */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.
   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
/* 짧은 지연을 구현하기 위해 LOOPS번 단순 루프를 반복함
   NO_INLINE으로 표시된 이유는 코드 정렬이 타이밍에 큰 영향을 미칠 수 있으므로,
   이 함수가 다양한 위치에서 인라인될 경우 결과 예측이 어려울 수 있기 때문임 */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
/* 약 NUM/DENOM 초 동안 슬립 */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
