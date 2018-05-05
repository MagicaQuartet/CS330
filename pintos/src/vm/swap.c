#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "vm/frame.h"

struct swap_table
{
	struct lock lock;
	struct list entry_list;
};

struct swap_table *swap_t;
struct block *swap_block;

block_sector_t find_free_sector(void);
void remove_swap_entry(block_sector_t);

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

	lock_acquire(&swap_t->lock);
	entry = malloc(sizeof(struct swap_entry));
	
	if (entry == NULL)
		PANIC("swap_out: out of memory (swap_entry)");
	entry->sector = find_free_sector();

	list_insert_ordered(&swap_t->entry_list, &entry->list_elem, swap_list_less, NULL);
	
	block_write (swap_block, entry->sector, buffer);

	lock_release(&swap_t->lock);

	return entry->sector;
}

void
swap_in (struct s_page_entry *s_pte, void * upage)
{
	void *kpage = find_free_frame();
	int cnt = PGSIZE / BLOCK_SECTOR_SIZE, i;
	block_sector_t sector;

	if (kpage == NULL)
		kpage = frame_evict();

	for(i = 0; i < cnt; i++){
		sector = list_entry(list_pop_front(&s_pte->sector_list), struct sector_elem, list_elem)->sector;
		remove_swap_entry(sector);
		block_read (swap_block, sector, kpage + i*BLOCK_SECTOR_SIZE);
	}
	set_frame_entry(upage, kpage);
	page_swap_in(s_pte, kpage);
}

void
remove_swap_entry (block_sector_t sector)
{
	struct list *l = &swap_t->entry_list;
	struct list_elem *e;

	lock_acquire(&swap_t->lock);
	
	for (e = list_begin(l); e != list_end(l); e = list_next(e)){
		if (list_entry(e, struct swap_entry, list_elem)->sector == sector){
			list_remove(e);
		}
	}

	lock_release(&swap_t->lock);
}

block_sector_t
find_free_sector()
{
	struct list_elem *e;
	block_sector_t candidate = 0;
	struct list *l = &swap_t->entry_list;

	//lock_acquire(&swap_t->lock);

	if (!list_empty(l)) {
		for (e = list_begin(l); e != list_end (l); e = list_next(e)){
			struct swap_entry *entry = list_entry(e, struct swap_entry, list_elem);
			if (entry->sector == candidate)
				candidate += 1;
			else
				break;
		}
	}

	//lock_release(&swap_t->lock);

	return candidate;
}

bool
swap_list_less(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	const struct swap_entry *_a = list_entry(a, struct swap_entry, list_elem);
	const struct swap_entry *_b = list_entry(b, struct swap_entry, list_elem);

	return _a->sector < _b->sector;
}
