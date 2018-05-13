#include "vm/frame.h"
#include <stdio.h>
#include <bitmap.h>
#include <round.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "devices/block.h"

struct frame_entry
{
	bool in_use;
	tid_t tid;
	void *upage;
	struct list_elem elem;
};

struct frame_table
{
	struct lock lock;
	struct list entry_list;
	size_t user_pages;
	uint8_t *base;
};

static struct frame_table *ft;

static void hex_dump_at_frame_table(void);
static void hex_dump_at_user_pool_base(void);

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
	ft->base = palloc_get_multiple(PAL_ZERO, data_pages);

	user_pool_base = free_start + kernel_pages * PGSIZE + bm_pages * PGSIZE;		// cf. init_pool (userprog/process.c)

}

// void
// frame_table_destroy()

bool
set_frame_entry (void *upage, void *kpage)
{
	uintptr_t idx;
	struct frame_entry* entry;

	//printf("set frame entry: tid %d upage %p kpage %p\n", thread_current()->tid, upage, kpage);

	if (pg_ofs(upage) != 0) {
		printf("upage is not a page address!\n");
		return false;
	}
	if (pg_ofs(kpage) != 0) {
		printf("kpage is not a page address!\n");
		return false;
	}
	lock_acquire_ft();
	
	idx = pg_no(kpage) - pg_no(user_pool_base);
	entry = (struct frame_entry*) ((uintptr_t)(ft->base) + idx * sizeof(struct frame_entry));
	if (entry->in_use){
		lock_release_ft();
		return false;
	}
	entry->in_use = true;
	entry->tid = thread_current()->tid;
	entry->upage = upage;
	list_push_back (&ft->entry_list, &entry->elem);
	
	lock_release_ft();
	return true;
}

void *
frame_evict()
{
	struct frame_entry *entry;
	struct s_page_entry *page_entry;
	struct sector_elem *sector_elem;
	struct thread *t;
	void *upage, *kpage;
	uintptr_t idx;
	int cnt = PGSIZE / BLOCK_SECTOR_SIZE, i;
	
	lock_acquire_ft();
	if (list_empty(&ft->entry_list)) {
		lock_release_ft();
		return palloc_get_page(PAL_USER);
	}
	else {
		entry = list_entry(list_pop_front (&ft->entry_list), struct frame_entry, elem);
		lock_release_ft();
		entry->in_use = false;
	
		upage = entry->upage;
		
		idx = ((uintptr_t)entry - (uintptr_t)(ft->base)) / sizeof(struct frame_entry);
		kpage = user_pool_base + idx * PGSIZE;
		
		//hex_dump_at_user_pool_base();
		t = find_thread(entry->tid);
		page_entry = page_lookup(upage, entry->tid);
	
		if (page_entry->file_p == NULL || page_entry->mapping < 0) {		
			for (i = 0; i < cnt; i++){
				sector_elem = malloc(sizeof(struct sector_elem));
				sector_elem->sector = swap_out(kpage + i*BLOCK_SECTOR_SIZE);
				list_push_back(&page_entry->sector_list, &sector_elem->list_elem);
			}
		}
		else {
			file_seek(page_entry->file_p, page_entry->page_idx * PGSIZE);
			file_write(page_entry->file_p, page_entry->upage, page_entry->page_read_bytes);
			file_seek(page_entry->file_p, 0);
		}
		page_get_evicted(page_entry);
		//hex_dump_at_user_pool_base();
	
		//printf("frame evict: tid %d upage %p kpage %p\n", entry->tid, upage, kpage);
		return kpage;
	}
}

void
remove_frame_entry (tid_t t, void *upage){
	struct list_elem *e;
	struct frame_entry *entry;
	lock_acquire_ft();
	for (e = list_begin(&ft->entry_list); e != list_end(&ft->entry_list); e = list_next(e)) {
		entry = list_entry (e, struct frame_entry, elem);
		if (entry->tid == t && (upage == NULL || entry->upage == upage)){
			list_remove (&entry->elem);
		}
	}
	lock_release_ft();
}

void
lock_acquire_ft()
{
	lock_acquire(&ft->lock);
}

void
lock_release_ft()
{
	lock_release(&ft->lock);
}

/* Helper function for debugging  */
static void
hex_dump_at_frame_table()
{
	hex_dump((uintptr_t)ft->base, ft->base, ft->user_pages * sizeof(struct frame_entry), false);
	printf("\n");
}

static void
hex_dump_at_user_pool_base()
{
	hex_dump((uintptr_t)user_pool_base, user_pool_base, PGSIZE * 4, false);
	printf("\n");
}
