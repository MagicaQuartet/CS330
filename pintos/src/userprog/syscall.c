#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);
bool is_valid_uaddr (void *p);
void bad_exit(struct intr_frame *);

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
			shutdown_power_off();
			break;
		case SYS_EXIT:
			if (thread_current()->pagedir != NULL) {
				printf("%s: exit(%d)\n",thread_current()->name, *(int *)p);
				thread_current()->exit_status = *(int *)p;
				f->eax = *(int *)p;
			}
			thread_exit();
			break;
		case SYS_EXEC:
			if (is_valid_uaddr (*(void **)p)) {
				f->eax =  process_execute (*(char **)p);
			}
			else {
				bad_exit(f);
			}
			break;
		case SYS_WAIT:
			f->eax = process_wait(*(int *)p);
			break;
		case SYS_CREATE:
			if (is_valid_uaddr (*(void **)p)) {
				f->eax = filesys_create (*(char **)p, *(off_t *)(p+sizeof(char **)));
			}
			else {
				bad_exit(f);
			}
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
				if (is_valid_uaddr (*(void **)p)) {
					putbuf(*(char **)p, *(size_t *)(p+sizeof(char *)));
				}
				else {
					bad_exit(f);
				}
			}
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
	}
}

bool
is_valid_uaddr (void *p)
{
	if(p != NULL && is_user_vaddr(p) && pagedir_get_page (thread_current()->pagedir, p) != NULL) {
		return true;
	}
	else {
		return false;
	}
}

void
bad_exit(struct intr_frame *f) {
	void *p = f->esp;

	*(int*)p = SYS_EXIT;
	*(int*)(p+sizeof(int)) = -1;
	syscall_handler(f);
}
