#include <stdint.h>
#include <hash.h>
#include "threads/synch.h"

struct s_page_entry
{
	struct hash_elem hash_elem;
	const void *vaddr;
	bool is_swapped;
};

uint32_t *s_page_table_create (void);
void s_page_table_destroy (uint32_t *);

void page_insert (const void *);
struct s_page_entry *page_lookup (const void *);
