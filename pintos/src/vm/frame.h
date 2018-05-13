#include <stddef.h>
#include <stdbool.h>
#include "threads/thread.h"
#include "threads/synch.h"

void *user_pool_base;

void frame_table_init (size_t user_page_limit);
bool set_frame_entry (void *upage, void *kpage);
void *frame_evict(void);
void remove_frame_entry (tid_t t, void*);

void lock_acquire_ft(void);
void lock_release_ft(void);
