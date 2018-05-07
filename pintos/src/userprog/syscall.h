#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/synch.h"

struct file_info
{
	struct list_elem elem;
	struct semaphore sema;
	int fd;
	struct file* file_p;
};

void syscall_init (void);
struct file_info *find_opened_file_info (int, struct thread *);

#endif /* userprog/syscall.h */
