#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/*
+=========================================+
|         Project 2 User Programs         |
+=========================================+
*/
/* System_Call_2.4_1 syscall.h 수정 */
/* Read, write시 파일에 대한 동시접근이 일어날 수 있으므로 Lock 사용 */
/* 각 시스템 콜에서 lock 획득 후 시스템 콜 처리, 시스템 콜 완료 시 lock 반납 */
#include "threads/synch.h"
struct lock filesys_lock;

void syscall_init (void);

#endif /* userprog/syscall.h */
