#include "filesys/off_t.h"

void cache_init(void);
void cache_close(void);

void *cache_insert (int);
void *cache_find (int);
void cache_read (void *, void *, off_t, int);
void cache_write (void *, void *, off_t, int);
void cache_delete (int);

void cache_lock_acquire(void);
void cache_lock_release(void);
