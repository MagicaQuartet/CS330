#include <stdio.h>
#include <debug.h>
#include "userprog/pagefault.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/swap.h"
#endif

void bad_exit (struct intr_frame *f);

void
page_fault_handler (struct intr_frame *f, bool not_present, bool write UNUSED, bool user, void *fault_addr)
{
//	printf("page fault: tid %d fault addr %p\n", thread_current()->tid, fault_addr);
#ifdef VM

	if (!is_user_vaddr (fault_addr) || fault_addr == NULL || !not_present) {
#endif
		bad_exit(f);
#ifdef VM
	}
	else {																					// In Project 3-1, this case will be related to stack growth
		void *fault_page;
		struct s_page_entry *s_pte;

		fault_page = pg_round_down(fault_addr);
		
		s_pte = page_lookup (fault_page, thread_current()->tid);
		
		if (s_pte != NULL) {													// Swapped
			if (s_pte->is_swapped){
				//printf("Let's swap in\n");
				lock_acquire_ft();
				swap_in(s_pte, s_pte->upage);
				lock_release_ft();
			}
			else {
				PANIC ("DO NOT CROSS\n");
			}
		}
		else {																				// Not mapped yet
			void *kpage;
			struct thread *t = thread_current();
			if (!(fault_addr >= f->esp - 32 || (!write && f->esp <= fault_addr))){				// TODO: check this condition
				bad_exit(f);
			}
			
			while (pagedir_get_page (t->pagedir, fault_page) == NULL && page_lookup(fault_page, thread_current()->tid) == NULL) {
				lock_acquire_ft();	
				kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	
				if (kpage == NULL){
					kpage = frame_evict();
				}
	
				if (pagedir_get_page (t->pagedir, fault_page) == NULL) {
					if (!pagedir_set_page (t->pagedir, fault_page, kpage, true)) {
						PANIC ("page_fault_handler: pagedir_set_page failed");	
					}
				}
				else {
					PANIC ("page_fault_handler: upage is already mapped");
				}
				
				set_frame_entry (fault_page, kpage);
				//page_insert (fault_page, true);
				//printf("thread %d upage %p -> kpage %p\n", thread_current()->tid, fault_page, kpage);
				lock_release_ft();
				fault_page += PGSIZE;
			}
		}
	}
#endif
}

void
bad_exit(struct intr_frame *f)
{
	printf("%s: exit(%d)\n", thread_current()->name, -1);
	struct list_elem *e, *temp;
	for (e = list_begin(&thread_current()->file_list); e != list_end(&thread_current()->file_list); ) {
		temp = e;
		e = list_remove(e);
		file_close(list_entry(temp, struct file_info, elem)->file_p);
		free(list_entry(temp, struct file_info, elem));
	}
	thread_current()->exit_status = -1;
	f->eax = -1;
	thread_exit();
}
