#include <round.h>
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include "vm/page.h"
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

	p->vaddr = vaddr;
	p->tid = thread_current()->tid;
	p->is_swapped = false;
	list_init(&p->sector_list);
	p->writable = writable;

	hash_insert((struct hash *)(thread_current()->s_pt), &p->hash_elem);
	//printf("hash size: %d\n", hash_size((struct hash*)(thread_current()->s_pt)));
}

struct s_page_entry *
page_lookup (const void *vaddr, tid_t tid)
{
	if (pg_ofs(vaddr) != 0)
		PANIC ("page_lookup: not page address");

	struct s_page_entry p;
	struct hash_elem *e;

	p.vaddr = vaddr;
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
	pagedir_clear_page(t->pagedir, entry->vaddr);
}

void
page_swap_in (struct s_page_entry * entry, void *kpage)
{
	bool success;
	if (kpage == NULL)
		PANIC ("page_swap_in: kpage is null pointer");
	
	entry->is_swapped = false;
	success =	pagedir_set_page(thread_current()->pagedir, entry->vaddr, kpage, entry->writable);
	if (!success)
		PANIC ("page_swap_in: pagedir_set_page failed!");
}

unsigned
page_hash (const struct hash_elem *e, void *aux)
{
	const struct s_page_entry *p = hash_entry(e, struct s_page_entry, hash_elem);
	return hash_bytes(&p->vaddr, sizeof(p->vaddr));	
}

bool
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	const struct s_page_entry *a_ = hash_entry(a, struct s_page_entry, hash_elem);
	const struct s_page_entry *b_ = hash_entry(b, struct s_page_entry, hash_elem);

	return (uintptr_t)a_->vaddr < (uintptr_t)b_->vaddr;
}
