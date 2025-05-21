#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
// 일반 목적 레지스터 저장 구조체, 인터럽트 발생 시 사용자 프로세스의 일반 레지스터 값들을 저장
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed)); // 구조체의 패딩 없이 메모리에 정렬됨(하드웨어 스택 레이아웃 일치 보장)


// 인터럽트 발생 시 CPU 및 소프트웨어(interrupt stub)가
// 커널 스택에 푸시하는 레지스터와 메타데이터를 나타냄
/*  구성 순서 요약(상위 > 하위 주소, push 순서)
 *	1.	General-purpose 레지스터 (R)
	2.	세그먼트 레지스터 (ds, es)
	3.	인터럽트 메타데이터 (vec_no, error_code)
	4.	CPU 자동 저장 영역 (rip, cs, eflags, rsp, ss)
	용도
	•	유저 → 커널 전환 시 하드웨어 + 어셈블리 루틴이 이 구조체 내용을 커널 스택에 저장
	•	start_process()에서 수동으로 이 구조체를 스택에 구성하여 최초 유저 모드 진입을 가능하게 함
	•	intr_handler_func(struct intr_frame *)로 핸들러들이 이 구조를 전달받아 분석 가능
*/
struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R;    // general-purpose registers
	uint16_t es;			// 세그먼트 레지스터
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;			// 세그먼트 레지스터
	uint16_t __pad3;
	uint32_t __pad4;
	/* Pushed by intrNN_stub in intr-stubs.S. */
    uint64_t vec_no;         // 인터럽트 벡터 번호
/* Sometimes pushed by the CPU,
   otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here. */
    uint64_t error_code;     // 예외 상황일 경우 에러코드 (ex: page fault)
/* Pushed by the CPU.
   These are the interrupted task's saved registers. */
    uintptr_t rip;           // 사용자 프로그램의 명령어 포인터
	uint16_t cs;             // Code segment
	uint16_t __pad5;
	uint32_t __pad6;
    uint64_t eflags;         // CPU 플래그 (IF, ZF 등)
	uintptr_t rsp;           // Stack pointer (64-bit)
	uint16_t ss;             // Stack segment
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void); // 인터럽트 서브루틴 초기화

// 외부 / 내부 인터럽트 핸들러 등록
// vec 벡터번호, dpl : Descriptor Privilege Level, name : 디버깅용 이름
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void); // 현재 인터럽트 컨텍스트 내부인지 확인
void intr_yield_on_return (void); // 인터럽트 리턴 직후 스레드 양보 요청


// 디버깅용 함수
void intr_dump_frame (const struct intr_frame *); // intr_frame 구조체를 출력
const char *intr_name (uint8_t vec); // 벡터 번호에 해당하는 인터럽트 이름을 문자열로 반환

#endif /* threads/interrupt.h */
