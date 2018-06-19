#include "userprog/syscall.h"
#include <stdio.h>
#include <round.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/pagefault.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include <string.h>
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);
int open_handler (const char *);
int filesize_handler (int);
int read_handler (int, void *, unsigned);
int write_handler (int, const void *, unsigned);
void seek_handler (int, off_t);
unsigned tell_handler (int);
bool close_handler (int);
int mmap_handler(int, void *, int);
bool readdir_handler(int, char *);
bool is_valid_uaddr (void *);
bool is_in_uspace (void *);
bool is_mapped_uaddr (void *);

struct lock lock;
struct intr_frame *_f;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init(&lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
	void * p = f->esp;
	int syscall = -1, temp;
	_f = f;

	if (is_valid_uaddr(p)){
		syscall = *(int *)p;
	}
	else{
		bad_exit(f);
	}

	p += sizeof(int);
	if (!is_valid_uaddr(p)){
		bad_exit(f);
	}
	switch (syscall)
	{
		case SYS_HALT:
			shutdown_power_off();
			break;

		case SYS_EXIT:
			if (thread_current()->pagedir != NULL) {
				struct list_elem *e, *temp;
				for (e = list_begin(&thread_current()->file_list); e != list_end(&thread_current()->file_list); ) {
					temp = e;
					e = list_remove(e);
					file_close(list_entry(temp, struct file_info, elem)->file_p);
					//free(list_entry(temp, struct file_info, elem));
				}
//	printf("thread %d file closed\n", thread_current()->tid);
				printf("%s: exit(%d)\n",thread_current()->name, *(int *)p);
				thread_current()->exit_status = *(int *)p;
				f->eax = *(int *)p;
			}
			thread_exit();
			break;

		case SYS_EXEC:
			if (is_valid_uaddr (*(void **)p)) {
				lock_acquire(&lock);
				f->eax =  process_execute (*(char **)p);
				lock_release(&lock);
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
				lock_acquire(&lock);
				f->eax = filesys_create (*(char **)p, *(off_t *)(p+sizeof(char **)));
				lock_release(&lock);
			}
			else {
				bad_exit(f);
			}
			break;

		case SYS_REMOVE:
			if (is_valid_uaddr (*(void **)p)) {
				f->eax = filesys_remove(*(char **)p);
			}
			else {
				bad_exit(f);
			}
			break;

		case SYS_OPEN:
			if (is_valid_uaddr (*(void **)p)) {
				lock_acquire(&lock);
				temp = open_handler(*(char **)p);
				lock_release(&lock);
				//printf(">>>open: return %d<<<\n", temp);
				if (temp == 0 || temp == 1) {
					bad_exit(f);
				}
				f->eax = temp;
			}
			else {
				bad_exit(f);
			}
			break;

		case SYS_FILESIZE:
			temp = filesize_handler(*(int *)p);
			if (temp != -1) {
				f->eax = temp;
			}
			else {
				bad_exit(f);
			}
			break;

		case SYS_READ:
			if (*(int *)p != 1) {
				if (is_valid_uaddr (*(void **)(p+sizeof(int))) && is_valid_uaddr ((void *)(p+sizeof(int)+sizeof(void *)))) {
					lock_acquire(&lock);
						temp = read_handler (*(int *)p, *(void **)(p+sizeof(int)), *(unsigned *)(p+sizeof(int)+sizeof(void *)));
						lock_release(&lock);
						if (temp == -1) {
							bad_exit(f);
						}
						else {
							f->eax = temp;
						}
				}
				else {
					bad_exit(f);
				}
			}
			else {
				bad_exit(f);
			}
			break;

		case SYS_WRITE:
			if (*(int *)p == 0) {
				bad_exit(f);
			}
			else if (*(int *)p == 1) {
				p += sizeof(int);
				if (is_valid_uaddr (*(void **)p) && is_valid_uaddr ((void *)(p+sizeof(char *)))) {
					putbuf(*(char **)p, *(size_t *)(p+sizeof(char *)));
					f->eax = *(size_t *)(p+sizeof(char*));
				}
				else {
					bad_exit(f);
				}
			}
			else {
				if (is_valid_uaddr ((void *)(p+sizeof(int))) && is_valid_uaddr (*(void **)(p+sizeof(int))) && is_valid_uaddr ((void *)(p+sizeof(int)+sizeof(void *)))) {
					lock_acquire(&lock);
					temp = write_handler (*(int *)p, *(void **)(p+sizeof(int)), *(unsigned *)(p+sizeof(int)+sizeof(void*)));
					lock_release(&lock);
					if (temp == -1) {
						bad_exit(f);
					}
					else {
						f->eax = temp;
					}
				}
				else{
					bad_exit(f);
				}
			}
			break;

		case SYS_SEEK:
			if (is_valid_uaddr ((void *)(p+sizeof(int)))){
				seek_handler(*(int *)p, *(off_t *)(p+sizeof(int)));
			}
			else {
				bad_exit(f);
			}
			break;

		case SYS_TELL:
			f->eax = tell_handler(*(int *)p);
			break;

		case SYS_CLOSE:
			if (!close_handler(*(int *)p)){
				bad_exit(f);
			}
			break;

		case SYS_MMAP:
			if (*(int *)p < 2 || !is_in_uspace(*(void **)(p+sizeof(int))) || (*(void **)(p+sizeof(int))) == NULL || pg_ofs(*(void **)(p+sizeof(int))) != 0){
				f->eax = -1;
			}
			else {
				temp = filesize_handler(*(int *)p);
				if (temp != -1) {
					f->eax = mmap_handler(*(int *)p, *(void **)(p+sizeof(int)), temp);
				}
				else {
					f->eax = -1;
				}
			}
			break;

		case SYS_MUNMAP:
			if (*(int *)p < thread_current()->mmap_id) {
				unmap(*(int *)p);
			}
			break;

		case SYS_CHDIR:
			f->eax = filesys_chdir(*(char **)p);
			break;

		case SYS_MKDIR:
			f->eax = filesys_mkdir(*(char **)p);
			break;

		case SYS_READDIR:
			// I did not check second argument yet
			f->eax = readdir_handler(*(int *)p, *(char **)(p + sizeof(int)));
			break;

		case SYS_ISDIR:
			f->eax = inode_is_dir(file_get_inode(find_opened_file_info(*(int *)p, thread_current())));
			break;

		case SYS_INUMBER:
			f->eax = inode_get_inumber (file_get_inode(find_opened_file_info(*(int *)p, thread_current())));
			break;

		default:
			bad_exit(f);
			break;
	}
}

int
open_handler(const char *name)
{
	struct file_info *finfo = (struct file_info *)malloc(sizeof(struct file_info));
	struct file *f;
	
	if (finfo == NULL) {
		printf("open_handler: malloc failed!\n");
	}

	f = filesys_open(name);
	if (f != NULL) {
		finfo->fd = (thread_current()->fd_cnt)++;
		finfo->file_p = f;
		if (inode_is_dir(file_get_inode(f)))
			finfo->dir = dir_open(file_get_inode(f));
		else
			finfo->dir = NULL;
		list_push_back (&thread_current()->file_list, &finfo->elem);
		if (strcmp(name, thread_current()->name) == 0){
			file_deny_write(f);
		}
		return finfo->fd;
	}
	else {
		free(finfo);
		return -1;
	}
}

int
filesize_handler(int fd)
{
	struct file_info *finfo;
	int size = -1;
	
	finfo = find_opened_file_info(fd, thread_current());
	if (finfo != NULL) {
		if (inode_is_dir(file_get_inode(finfo->file_p)))
			return -1;
		size = file_length(finfo->file_p);
	}

	return size;
}

int
read_handler (int fd, void *buffer, unsigned size)
{
	unsigned _size = size;
	void *p = buffer;
	struct file_info *finfo;
	int result = 0;

	if (fd == 0) {
		while (_size-- > 0) {
			if (!is_valid_uaddr(p)){
				return -1;
			}
			memset (p, input_getc(), 1);
			p++;
		}
		return size;
	}
	else {
		int pages = DIV_ROUND_UP(size + 1, PGSIZE), i;
		finfo = find_opened_file_info(fd, thread_current());
		if (finfo != NULL) {
			if (inode_is_dir(file_get_inode(finfo->file_p)))
				return -1;

			for (i = 0; i < pages ; i++) {
				if (pagedir_get_page(thread_current()->pagedir, buffer + i*PGSIZE) == NULL) {
					page_fault_handler (_f, true, false, true, buffer + i*PGSIZE);
				}
			}
			result = file_read(finfo->file_p, buffer, size);
			return result;
		}

		return -1;
	}
}

int
write_handler (int fd, const void *buffer, unsigned size)
{
	struct file_info *finfo;
	int pages = DIV_ROUND_UP(size + 1, PGSIZE), i;
	int result;
	
	finfo = find_opened_file_info(fd, thread_current());
	if (finfo != NULL) {
		if (inode_is_dir(file_get_inode(finfo->file_p)))
			return -1;

		for (i = 0; i < pages; i++) {
			if (pagedir_get_page(thread_current()->pagedir, buffer + i*PGSIZE) == NULL)
				page_fault_handler (_f, true, true, true, buffer + i*PGSIZE);
		}
		result = file_write (finfo->file_p, buffer, size);
		return result;
	}

	return -1;
}

void
seek_handler (int fd, off_t pos)
{
	struct file_info *finfo;

	finfo = find_opened_file_info (fd, thread_current());
	file_seek (finfo->file_p, pos);
}

unsigned
tell_handler(int fd)
{
	struct file_info *finfo;
	
	finfo = find_opened_file_info(fd, thread_current());
	return file_tell (finfo->file_p);
}

bool
close_handler(int fd)
{
	struct file_info *finfo;
	finfo = find_opened_file_info(fd, thread_current());
	if (finfo != NULL) {
		file_close(finfo->file_p);
		list_remove (&finfo->elem);
		free(finfo);
		return true;
	}
	return false;
}

int
mmap_handler(int fd, void * addr, int size)
{
	int filesize = size;
	int pages = DIV_ROUND_UP(filesize, PGSIZE);
	size_t page_read_bytes = 0;
	int i, mapping;
	struct file_info *finfo;
	mapping = thread_current()->mmap_id;
	
	finfo = find_opened_file_info(fd, thread_current());
	if (finfo != NULL){
		if (inode_is_dir(file_get_inode(finfo->file_p)))
			return -1;
		for (i = 0; i < pages; i++) {
			page_read_bytes = filesize < PGSIZE ? filesize : PGSIZE;
			if (filesize > PGSIZE)
				filesize -= PGSIZE;
	
				if (!mmap_insert(addr + i*PGSIZE, true, file_reopen(finfo->file_p), mapping, i, page_read_bytes)) {
					return -1;
				}
	
		}
		if (page_read_bytes == PGSIZE) {
			if (!mmap_insert(addr + pages*PGSIZE, true, file_reopen(finfo->file_p), mapping, i, 0)) {
				return -1;
			}
		}
	
		(thread_current()->mmap_id)++;
	
		return mapping;
	}
	else
		return -1;
}

bool
readdir_handler (int fd, char *name)
{
	struct file_info *finfo;
	finfo = find_opened_file_info (fd, thread_current());
	if (finfo != NULL && finfo->dir != NULL) {
		return dir_readdir(finfo->dir, name);
	}
	else
		return false;
}

bool
is_valid_uaddr (void *p)
{
	if (is_in_uspace(p)) 
		return is_mapped_uaddr(p);
	else
		return false;
}

bool
is_in_uspace (void *p)
{
	return p != NULL && is_user_vaddr(p);
}

bool
is_mapped_uaddr (void *p)
{
	return pagedir_get_page (thread_current()->pagedir, p) != NULL;
}

struct file_info *
find_opened_file_info(int fd, struct thread *t)
{
	struct list_elem *e;
	struct file_info *finfo;

	for (e = list_begin(&t->file_list); e != list_end(&t->file_list); e = list_next(e)) {
		finfo = list_entry(e, struct file_info, elem);
		if (finfo->fd == fd) {
			return finfo;
		}
	}

	return NULL;
}
