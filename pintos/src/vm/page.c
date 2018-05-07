#include <round.h>
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"

unsigned page_hash (const struct hash_elem *, void *);
bool page_less (const struct hash_elem *, const struct hash_elem *, void *);

uint32_t *
s_page_table_create ()
{
	struct hash *s_pt;										// single-level page hash table

	s_pt = malloc(sizeof(struct hash));
	if (s_pt == NULL)
		PANIC ("s_page_table_create: memory allocation failed");

	if (!hash_init(s_pt, page_hash, page_less, NULL))
		PANIC ("s_page_table_create: hash init failed");

	return (uint32_t *)s_pt;
}

void
s_page_table_destroy (uint32_t *p)
{
	hash_destroy ((struct hash *)p, NULL);		// TODO: destructor should do proper jobs to free all s_page_entry resource
}

void
page_insert (const void *vaddr, bool writable)
	{
	if (pg_ofs(vaddr) != 0)
		PANIC ("page_insert: not page address");

	struct s_page_entry *p;

	p = malloc(sizeof(struct s_page_entry));
	if (p == NULL)
		PANIC ("page_insert: out of memory (s_page_entry)");

	p->upage = vaddr;
	p->tid = thread_current()->tid;
	p->is_swapped = false;
	list_init(&p->sector_list);
	p->fd = -1;
	p->writable = writable;

	hash_insert((struct hash *)(thread_current()->s_pt), &p->hash_elem);
}

bool
mmap_insert (const void *vaddr, bool writable, int fd, int mapping, size_t page_read_bytes)
{
	if (pg_ofs(vaddr) != 0 || page_lookup(vaddr, thread_current()->tid) != NULL)
		return false;

	//printf("mmap insert: upage %p\n");

	struct s_page_entry *p;

	p = malloc(sizeof(struct s_page_entry));
	if (p == NULL)
		PANIC ("page_insert: out of memory (s_page_entry)");

	p->upage = vaddr;
	p->tid = thread_current()->tid;
	p->is_swapped = true;
	p->fd = fd;
	p->mapping = mapping;
	p->page_read_bytes = page_read_bytes;
	p->writable = writable;

	hash_insert((struct hash *)(thread_current()->s_pt), &p->hash_elem);

	return true;
}

struct s_page_entry *
page_lookup (const void *vaddr, tid_t tid)
{
	if (pg_ofs(vaddr) != 0)
		PANIC ("page_lookup: not page address");

	struct s_page_entry p;
	struct hash_elem *e;
	p.upage = vaddr;
	e = hash_find ((struct hash *)(find_thread(tid)->s_pt), &p.hash_elem);
	return e != NULL ? hash_entry(e, struct s_page_entry, hash_elem) : NULL;
}

void
page_get_evicted(struct s_page_entry * entry)
{
	struct thread *t;
	entry->is_swapped = true;

	t = find_thread(entry->tid);
	if (t == NULL)
		PANIC("page_get_evicted: invalid tid");
	pagedir_clear_page(t->pagedir, entry->upage);
}

void
page_swap_in (struct s_page_entry * entry, void *kpage)
{
	bool success;
	if (kpage == NULL)
		PANIC ("page_swap_in: kpage is null pointer");
	
	entry->is_swapped = false;
	success =	pagedir_set_page(thread_current()->pagedir, entry->upage, kpage, entry->writable);
	if (!success)
		PANIC ("page_swap_in: pagedir_set_page failed!");
}

void
remove_page_block_sector(struct hash *table)
{
	struct hash_iterator itr;
	struct hash_elem *e;
	struct list_elem *_e;
	struct s_page_entry *entry;
	
	hash_first (&itr, table);
	e = itr.elem;
	while (e != NULL) {
			entry = hash_entry(e, struct s_page_entry, hash_elem);

			if (entry->is_swapped && entry->fd < 2){
				for (_e = list_begin(&entry->sector_list); _e != list_end(&entry->sector_list); _e = list_next(_e)){
					remove_swap_entry(list_entry(_e, struct swap_entry, list_elem)->sector);
				}
			}

			e = hash_next(&itr);
	}
}

unsigned
page_hash (const struct hash_elem *e, void *aux)
{
	const struct s_page_entry *p = hash_entry(e, struct s_page_entry, hash_elem);
	return hash_bytes(&p->upage, sizeof(p->upage));	
}

bool
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	const struct s_page_entry *a_ = hash_entry(a, struct s_page_entry, hash_elem);
	const struct s_page_entry *b_ = hash_entry(b, struct s_page_entry, hash_elem);

	return (uintptr_t)a_->upage < (uintptr_t)b_->upage;
}
