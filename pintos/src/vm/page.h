#include <stdint.h>
#include <hash.h>
#include <list.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/block.h"

struct s_page_entry
{
	struct hash_elem hash_elem;
	tid_t tid;
	const void *vaddr;
	bool is_swapped;
	struct list sector_list;
	bool writable;
};

struct sector_elem
{
	struct list_elem list_elem;
	block_sector_t sector;
};

uint32_t *s_page_table_create (void);
void s_page_table_destroy (uint32_t *);

void page_insert (const void *, bool);
struct s_page_entry *page_lookup (const void *, tid_t);
void page_get_evicted(struct s_page_entry *);
void page_swap_in (struct s_page_entry *, void *);
