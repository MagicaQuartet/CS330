#include <list.h>
#include "devices/block.h"
#include "vm/page.h"

struct swap_entry
{
	struct list_elem list_elem;
	block_sector_t sector;
};

void swap_table_init(void);
block_sector_t swap_out (void *);
void swap_in (struct s_page_entry *, void *);
void remove_swap_entry (block_sector_t);
