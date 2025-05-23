#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "include/filesys/off_t.h"
#include "userprog/process.h"
#include <list.h>
#include <syscall-nr.h>

void syscall_entry(void);
void syscall_handler(struct intr_frame*);
int sys_exit(int status);
int sys_write(int fd, const void* buffer, unsigned size);
static void check_user_addr (const void *buffer);
static void check_user_buffer (const void *buffer, unsigned size);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init(void)
{
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
              ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler(struct intr_frame* f UNUSED)
{
    // TODO: Your implementation goes here.
    int number = (int)f->R.rax;
    uint64_t arg1 = f->R.rdi;
    uint64_t arg2 = f->R.rsi;
    uint64_t arg3 = f->R.rdx;
    uint64_t arg4 = f->R.r10;
    uint64_t arg5 = f->R.r8;
    uint64_t arg6 = f->R.r9;

    switch (number)
    {
    case SYS_HALT: // no args
        power_off();
        break;

    case SYS_EXIT: // int status
        sys_exit(arg1); // userprog/process.c 구현 필요
        break;

    case SYS_FORK: // const char *thread_name
        // process_fork(name, &f);
        break;

    case SYS_EXEC: // const char *file
        break;

    case SYS_WAIT: // pid_t pid
        // process_wait(pid);
        break;

    case SYS_CREATE: // const char *file, unsigned initial_size
        break;

    case SYS_REMOVE: // const char *file
        break;

    case SYS_OPEN: // const char *file
        break;

    case SYS_FILESIZE: // int fd
        break;

    case SYS_READ: // int fd, void *buffer, unsigned size
        // check_user_addr((void*)f->R.rdi);
        // check_user_buffer((void*)f->R.rdi, (unsigned)f->R.rsi);
        break;

    case SYS_WRITE: // int fd, const void *buffer, unsigned size
        check_user_addr((void*)arg2);
        check_user_buffer((void*)arg2, (unsigned)arg3);
        f->R.rax = sys_write(f->R.rdi, f->R.rsi, f->R.rdx);
        break;

    case SYS_SEEK: // int fd, unsigned position
        break;

    case SYS_TELL: // int fd
        break;

    case SYS_CLOSE: // int fd
        break;

    default:
        printf("Unknown syscall number %d\n", number);
        thread_exit();
    }
}

int sys_exit(int status)
{
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_exit();
    // process_exit()을 호출하는게 맞지않을까?
    return 0;
}

int sys_write(int fd, const void* buffer, unsigned size)
{
    if (fd == 1) // STDOUT
    {
        putbuf(buffer, size);
        return size;
    }
    return -1; // 이후 2 이상일 때 처리 구현 필요
}

static void
check_user_addr (const void *buffer) {
    struct thread *cur = thread_current ();
    if (!is_user_vaddr (buffer) || pml4_get_page (cur->pml4, buffer) == NULL)
        process_exit();
}

static void
check_user_buffer (const void *buffer, unsigned size)
{
    struct thread *cur = thread_current ();
    uint8_t *ptr = (uint8_t *) buffer;
    for (unsigned i = 0; i < size; i++, ptr++)
    {
        if (!is_user_vaddr (ptr) || pml4_get_page (cur->pml4, ptr) == NULL)
        {
            thread_current()->status = -1;
            process_exit();
        }
    }
}