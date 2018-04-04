#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
	void * p = f->esp;
	int syscall = *(int *)p;

	p += sizeof(int);
	switch (syscall)
	{
		case SYS_HALT:
			break;
		case SYS_EXIT:
			printf("%s: exit(%d)\n",thread_current()->name, *(int *)p);
			//hex_dump((uintptr_t)p, p, 0x50, true);
			thread_exit();
			break;
		case SYS_EXEC:
			break;
		case SYS_WAIT:
			break;
		case SYS_CREATE:
			break;
		case SYS_REMOVE:
			break;
		case SYS_OPEN:
			break;
		case SYS_FILESIZE:
			break;
		case SYS_READ:
			break;
		case SYS_WRITE:
			if (*(int *)p == 1) {
				p += sizeof(int);
				putbuf(*(char **)p, *(size_t *)(p+sizeof(char *)));
			}
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
	}
  thread_yield ();
}
