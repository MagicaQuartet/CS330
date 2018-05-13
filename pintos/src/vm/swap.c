#include <string.h>
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "vm/frame.h"

struct swap_table
{
	struct lock lock;
	struct list entry_list;
};

struct swap_table *swap_t;
struct block *swap_block;
struct semaphore block_sema;

block_sector_t find_free_sector(void);

bool swap_list_less(const struct list_elem *, const struct list_elem *, void *);

void
swap_table_init()
{
	swap_t = malloc(sizeof(struct swap_table));
	if (swap_t == NULL)
		PANIC ("swap_table_init: memory allocation failed (swap_t)");

	lock_init(&swap_t->lock);
	list_init(&swap_t->entry_list);

	swap_block = block_get_role(BLOCK_SWAP);
}

block_sector_t
swap_out (void *buffer)
{
	struct swap_entry *entry;
	entry = malloc(sizeof(struct swap_entry));
	
	if (entry == NULL)
		PANIC("swap_out: out of memory (swap_entry)");

	//printf("swap out buffer %p\n", buffer);
	lock_acquire_sw();
	entry->sector = find_free_sector();

	list_insert_ordered(&swap_t->entry_list, &entry->list_elem, swap_list_less, NULL);
	lock_release_sw();
	
	block_write (swap_block, entry->sector, buffer);

	return entry->sector;
}

void
swap_in (struct s_page_entry *s_pte, void * upage)
{
	void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	int cnt = PGSIZE / BLOCK_SECTOR_SIZE, i;
	block_sector_t sector;

	if (kpage == NULL)
		kpage = frame_evict();

	//printf("swap in buffer %p tid %d upage: %p\n", kpage, thread_current()->tid, upage);
	if (s_pte->file_p == NULL) {
		for(i = 0; i < cnt; i++){
			sector = list_entry(list_pop_front(&s_pte->sector_list), struct sector_elem, list_elem)->sector;
			remove_swap_entry(sector);
			block_read (swap_block, sector, kpage + i*BLOCK_SECTOR_SIZE);
		}
		page_swap_in(s_pte, kpage);
		set_frame_entry(upage, kpage);
		//printf("swap_in finish\n");
	}
	else {
		void * temp = (void *)s_pte->file_p;
		while (s_pte != NULL && (void *)s_pte->file_p == temp && kpage != NULL) {
			//printf("swap in: tid %d upage %p kpage %p\n", thread_current()->tid, s_pte->upage, kpage);
			page_swap_in (s_pte, kpage);
			set_frame_entry (s_pte->upage, kpage);
			file_read(s_pte->file_p, kpage, s_pte->page_read_bytes);
			//printf("thread %d page_read_bytes: %d\n", thread_current()->tid, s_pte->page_read_bytes);
			if (s_pte->page_read_bytes == PGSIZE)
				file_seek(s_pte->file_p, PGSIZE);
			else {
				memset (kpage + s_pte->page_read_bytes, 0, PGSIZE - s_pte->page_read_bytes);
			}
			kpage = palloc_get_page(PAL_USER | PAL_ZERO);
			if (kpage == NULL)
				kpage = frame_evict();

			s_pte = page_lookup ((s_pte->upage)+PGSIZE, thread_current()->tid);
		}
		file_seek(temp, 0);
	}
}

void
remove_swap_entry (block_sector_t sector)
{
	struct list *l = &swap_t->entry_list;
	struct list_elem *e;

	lock_acquire_sw();
	for (e = list_begin(l); e != list_end(l); e = list_next(e)){
		if (list_entry(e, struct swap_entry, list_elem)->sector == sector){
			list_remove(e);
			break;
		}
	}
	lock_release_sw();

}

block_sector_t
find_free_sector()
{
	struct list_elem *e;
	block_sector_t candidate = 0;
	struct list *l = &swap_t->entry_list;


	if (!list_empty(l)) {
		for (e = list_begin(l); e != list_end (l); e = list_next(e)){
			struct swap_entry *entry = list_entry(e, struct swap_entry, list_elem);
			if (entry->sector == candidate)
				candidate += 1;
			else
				break;
		}
	}


	return candidate;
}

void
lock_acquire_sw()
{
	lock_acquire(&swap_t->lock);
}

void lock_release_sw()
{
	lock_release(&swap_t->lock);
}

bool
swap_list_less(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	const struct swap_entry *_a = list_entry(a, struct swap_entry, list_elem);
	const struct swap_entry *_b = list_entry(b, struct swap_entry, list_elem);

	return _a->sector < _b->sector;
}
