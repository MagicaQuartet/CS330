#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "filesys/file.h"

struct file_info
{
	struct list_elem elem;
	int fd;
	struct file* file_p;
};

void syscall_init (void);
struct file_info *find_opened_file_info (int);

#endif /* userprog/syscall.h */
