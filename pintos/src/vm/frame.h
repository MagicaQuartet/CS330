#include <stddef.h>
#include <stdbool.h>
#include "threads/thread.h"
#include "threads/synch.h"

struct frame_table
{
	struct lock lock;
	struct list entry_list;
	size_t user_pages;
	uint8_t *base;
};

uint8_t *user_pool_base;

void frame_table_init (size_t user_page_limit);
bool set_frame_entry (void *upage, void *kpage);
