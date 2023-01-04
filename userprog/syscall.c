#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/* PROJECT 2 SYSCALL 추가 */
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/synch.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System_Call_2.1 check_address() */
void check_address(void *addr);

/* System_Call_2.2 halt, exit, create, remove */
/*====================Process====================*/
typedef int pid_t;	// syscall.h 에 정의되어 있다. 근데 왜 인식 X?
void halt(void);
void exit(int status);
/*====================Process====================*/

/*=====================File======================*/
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
/*=====================File======================*/



/* System_Call_2.3 Hierarchical Process Structure */
/*====================Process====================*/
pid_t fork(const char *thread_name, struct intr_frame *f);
int exec (char *file_name);
// int wait (pid_t pid);
/*====================Process====================*/

/*=====================File======================*/
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write (int fd , void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
/*=====================File======================*/



/* System_Call_2.4_3 함수 추가 */
/* syscall.h syscall.c */
int process_add_file (struct file *f);
/* 파일 객체에 대한 파일 디스크립터 생성 */
struct file *process_get_file(int fd);
/* 프로세스의 파일 디스크립터 테이블을 검색하여 파일 객체의 주소를 리턴 */
void process_close_file (int fd);
/* 파일 디스크립터에 해당하는 파일을 닫고 해당 엔트리 초기화 */



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
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	
    /* Git Book - System Calls
	시스템 호출 번호 : %rax

	인자 들어오는 순서:
	1번째 인자: %rdi
	2번째 인자: %rsi
	3번째 인자: %rdx
	4번째 인자: %r10 (not %rcx)
	5번째 인자: %r8
	6번째 인자: %r9 
	*/

	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 가져온다 */
	// rax: 시스템 콜 넘버
	int sys_number = f->R.rax;

	/* include/lib/syscall-nr.h enum 선언 참고 */
	/* rax is the system call number */
	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			// memcpy(&thread_current()->parent_if, f, sizeof(struct intr_frame));
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			if (exec(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_WAIT:
			f->R.rax = process_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}

/* System_Call_2.1 check_address() */
void check_address(void *addr) {
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL)
		exit(-1);
}

/* System_Call_2.2 halt, exit, create, remove */

/* 핀토스 종료 */
void halt(void) {
	// shutdown_power_off();
	power_off();
}

/* 현재 프로세스 종료 */
void exit(int status) {
/* 	종료 시 “프로세스 이름: exit(status)” 출력 (Process Termination Message)
	정상적으로 종료 시 status는 0
	status : 프로그램이 정상적으로 종료됐는지 확인
	프로세스 디스크립터에 exit status 값을 저장 */

	struct thread *cur = thread_current ();

	/* 프로세스 디스크립터에 exit status 저장 */
	/* thread.h thread 구조체에 exit_status 추가 */
	cur->exit_status = status;

	/* Process_Termination_Message_4.1 */
	/* Kaist PPT 109p */
	printf("%s: exit(%d)\n" , cur->name , status);

	thread_exit();
}

/* 파일 생성 */
bool create (const char *file , unsigned initial_size) {
/*	성공 일 경우 true, 실패 일 경우 false 리턴
	file : 생성할 파일의 이름 및 경로 정보
	initial_size : 생성할 파일의 크기 */
	check_address(file);
	return filesys_create(file, initial_size);
}

/* 파일 삭제 */
bool remove (const char *file) {
/*	file : 제거할 파일의 이름 및 경로 정보
	성공 일 경우 true, 실패 일 경우 false 리턴 */
	check_address(file);
	return filesys_remove(file);
}



/* System_Call_2.3 Hierarchical Process Structure */

pid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}

int exec(char *file_name) {
	check_address(file_name);

	int file_size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		exit(-1);

	strlcpy(fn_copy, file_name, file_size);

	if(process_exec(fn_copy) == -1)
		return -1;

	NOT_REACHED();
	return 0;
}

// int wait (pid_t pid) {
// 	process_wait(pid);
// }





/* System_Call_2.4_4 함수 구현 */
/* Kaist PPT 125p */
/* 한양대 PDF 133p */
int open (const char *file) {
/*	파일을 열 때 사용하는 시스템 콜
	파일이 없을 경우 실패
	성공 시 fd를 생성하고 반환, 실패 시 -1 반환
	File : 파일의 이름 및 경로 정보 */
	check_address(file);

	// [Test] open-missing failed
	// lock_acquire(&filesys_lock);

	struct file *fileobj = filesys_open(file);	// return file_open()

	if (fileobj == NULL)
		return -1;

	int fd = process_add_file(fileobj);
	// fdt is full => return -1
	if (fd == -1)
		file_close(fileobj);

	// lock_release(&filesys_lock);

	return fd;
}

int filesize(int fd) {
/*	파일의 크기를 알려주는 시스템 콜
	성공 시 파일의 크기를 반환, 실패 시 -1 반환 */
	struct file *fileobj = process_get_file(fd);

	if (fileobj == NULL)
		return -1;

	return file_length(fileobj);
}

int read (int fd , void *buffer, unsigned size) {
/*	열린 파일의 데이터를 읽는 시스템 콜
	성공 시 읽은 바이트 수를 반환, 실패 시 -1 반환
	buffer : 읽은 데이터를 저장할 버퍼의 주소 값
	size : 읽을 데이터 크기
	fd 값이 0일 때 키보드의 데이터를 읽어 버퍼에 저장. 
	(input_getc() 이용) */
	
	check_address(buffer);
	int read_result;

	if(fd == 0){
		for(int i = 0; i<size; ++i){
			((char*)buffer)[i] = input_getc();
		}
		read_result = size;
	}
	else if (fd < 2){
		read_result = - 1;
	}
	else{
		struct file *file = process_get_file(fd);
		if (file == NULL)
			return -1;
		else{
			lock_acquire(&filesys_lock);
			read_result = file_read(file, buffer, size);
			lock_release(&filesys_lock);
		}
	}
	return read_result;
}

int write (int fd , void *buffer, unsigned size) {
/*	열린 파일의 데이터를 기록 시스템 콜
	성공 시 기록한 데이터의 바이트 수를 반환, 실패시 -1 반환
	buffer : 기록 할 데이터를 저장한 버퍼의 주소 값
	size : 기록할 데이터 크기
	fd 값이 1일 때 버퍼에 저장된 데이터를 화면에 출력. 
	(putbuf() 이용) */
	
	check_address(buffer);
	int write_result;

	lock_acquire(&filesys_lock);

	if (fd == 1) {
		putbuf(buffer, size);
		write_result = size;
	}
	else if(fd < 2) {
		write_result = -1;
	}
	else {	
		struct file *file = process_get_file(fd);
		if(file == NULL) {
			write_result = -1;
		}
		else {
			write_result = file_write(file, buffer, size);
		}
	}

	lock_release(&filesys_lock);

	return write_result;
}

void seek(int fd, unsigned position) {
/*	열린 파일의 위치(offset)를 이동하는 시스템 콜
	Position : 현재 위치(offset)를 기준으로 이동할 거리 */
	struct file *fileobj = process_get_file(fd);

	if (fileobj <= 2)
		return;

	/* 불완전한 클래스 형식 
	 * "struct file"에 대한 포인터는 사용할 수 없음 */
	// fileobj->pos = position;
	file_seek(fileobj, position);
}

unsigned tell(int fd) {
/*	열린 파일의 위치(offset)를 알려주는 시스템 콜
	성공 시 파일의 위치(offset)를 반환, 실패 시 -1 반환 */
	struct file *fileobj = process_get_file(fd);

	if (fileobj <= 2)
		return;

	return file_tell(fileobj);
}

void close (int fd) {
/*	열린 파일을 닫는 시스템 콜
	파일을 닫고 File Descriptor를 제거 */
	struct file *fileobj = process_get_file(fd);

	if (fileobj == NULL)
		return;

	process_close_file(fd);
}





/* System_Call_2.4_3 함수 구현 */
int process_add_file(struct file *file) {
	struct thread *cur = thread_current();
	struct file **fd_table = cur->fdt;

	// Project2-extra - (multi-oom) Find open spot from the front
	while (cur->next_fd < FDT_MAX && fd_table[cur->next_fd])
		cur->next_fd++;

	if (cur->next_fd >= FDT_MAX)
		return -1;

	fd_table[cur->next_fd] = file;
	return cur->next_fd;
}

struct file *process_get_file(int fd) {
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDT_MAX)
		return NULL;
	
	return cur->fdt[fd];
}

void process_close_file(int fd) {
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDT_MAX)
		return;

	cur->fdt[fd] = NULL;
}