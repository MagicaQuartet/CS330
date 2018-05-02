#include "vm/frame.h"
#include <stdio.h>
#include <bitmap.h>
#include <round.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/loader.h"

struct frame_entry
{
	tid_t tid;
	void *upage;
	struct list_elem elem;
};

static struct frame_table *ft;

static bool frame_table_full(void);
static void hex_dump_at_frame_table(void);

void
frame_table_init (size_t user_page_limit)
{
	/* Calculate the number of user page frames (cf. palloc_init) */
	uint8_t *free_start = ptov (1024 * 1024);
	uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
	size_t free_pages = (free_end - free_start) / PGSIZE;
	size_t user_pages = free_pages / 2;
	size_t kernel_pages;
	size_t bm_pages, table_pages, data_pages;

	if (user_pages > user_page_limit)
		user_pages = user_page_limit;
	kernel_pages = free_pages - user_pages;
	
	bm_pages = DIV_ROUND_UP(bitmap_buf_size (user_pages), PGSIZE);
	table_pages = DIV_ROUND_UP (sizeof(struct frame_table), PGSIZE);
	data_pages = DIV_ROUND_UP (user_pages * sizeof(struct frame_entry), PGSIZE);
	ft = palloc_get_multiple(0, table_pages);
	lock_init(&ft->lock);
	list_init(&ft->entry_list);
	ft->user_pages = user_pages - bm_pages;
	ft->base = palloc_get_multiple(0, data_pages);

	user_pool_base = free_start + kernel_pages * PGSIZE + bm_pages * PGSIZE;		// cf. init_pool (userprog/process.c)
}

// void
// frame_table_destroy()

bool
set_frame_entry (void *upage, void *kpage)
{
	uintptr_t idx;
	struct frame_entry* entry;

	if (pg_ofs(upage) != 0) {
		printf("upage is not a page address!\n");
		return false;
	}
	if (pg_ofs(kpage) != 0) {
		printf("kpage is not a page address!\n");
		return false;
	}

	idx = pg_no(kpage) - pg_no(user_pool_base);
	entry = (struct frame_entry*) ((uintptr_t)(ft->base) + idx * sizeof(struct frame_entry));
	entry->tid = thread_current()->tid;
	entry->upage = upage;
	list_push_back (&ft->entry_list, &entry->elem);
	// hex_dump_at_frame_table();
	return true;
}

/* Return true if all user frames are occupied, or false otherwise */
static bool
frame_table_full ()
{
	bool is_full;
	lock_acquire(&ft->lock);
	is_full = (list_size(&ft->entry_list) == ft->user_pages);	
	lock_release(&ft->lock);
	return is_full;
}

/* Helper function for debugging  */
static void
hex_dump_at_frame_table()
{
	hex_dump((uintptr_t)ft->base, ft->base, PGSIZE, false);
	printf("\n");
}
