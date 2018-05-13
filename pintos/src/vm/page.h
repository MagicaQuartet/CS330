#include <stdint.h>
#include <hash.h>
#include <list.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "devices/block.h"

struct s_page_entry
{
	struct hash_elem hash_elem;
	tid_t tid;
	const void *upage;
	bool is_swapped;
	bool writable;

	/* segment page entry */
	struct list sector_list;

	/* mmap page entry */
	struct file * file_p;
	int mapping;
	size_t page_idx;
	size_t page_read_bytes;
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
void page_swap_in (struct s_page_entry *, void *);
void remove_page_block_sector(struct hash *);
void page_get_evicted(struct s_page_entry *);

bool mmap_insert (const void *, bool, struct file *, int, size_t, size_t);
void unmap(int);

void lock_acquire_pagedir(struct thread *);
void lock_release_pagedir(struct thread *);

void lock_acquire_s_pt(struct thread *);
void lock_release_s_pt(struct thread *);
