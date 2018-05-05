#include <stdio.h>
#include <debug.h>
#include "userprog/pagefault.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/swap.h"
#endif

void bad_exit (struct intr_frame *f);

void
page_fault_handler (struct intr_frame *f, bool not_present, bool write UNUSED, bool user, void *fault_addr)
{
#ifdef VM
	if (!is_user_vaddr (fault_addr) || fault_addr < VADDR_BASE || !not_present) {
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
				swap_in(s_pte, s_pte->vaddr);	
			}
			else {
				PANIC ("supplemental page table entry exists but not swapped??");
			}
		}
		else {																				// Not mapped yet
			void *kpage;
			struct thread *t = thread_current();

			if (!(fault_addr >= f->esp - 32 || (!write && f->esp <= fault_addr))){				// TODO: check this condition
				bad_exit(f);
			}
			
			while (pagedir_get_page (t->pagedir, fault_page) == NULL && page_lookup(fault_page, thread_current()->tid) == NULL) {

				kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	
				if (kpage == NULL)
					PANIC ("page_fault_handler: out of memory in user pool (kpage)");
	
				set_frame_entry (fault_page, kpage);
	
				if (pagedir_get_page (t->pagedir, fault_page) == NULL) {
					if (!pagedir_set_page (t->pagedir, fault_page, kpage, true)) {
						PANIC ("page_fault_handler: pagedir_set_page failed");	
					}
				}
				else {
					PANIC ("page_fault_handler: upage is already mapped");
				}
	
				page_insert (fault_page, true);

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
	thread_current()->exit_status = -1;
	f->eax = -1;
	thread_exit();
}
