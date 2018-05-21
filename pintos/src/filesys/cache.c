#include "filesys/cache.h"
#include <debug.h>
#include <list.h>
#include <hash.h>
#include <string.h>
#include "filesys/filesys.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#define CACHE_MAX 64

struct cache_entry
{
	struct hash_elem hash_elem;
	struct list_elem list_elem;
	int sector_idx;
	void *data;
};

struct buffer_cache
{
	struct hash *hash;
	struct list list;
	int cnt;
};

struct buffer_cache *cache;
//struct lock cache_lock;

void cache_destroyer (struct hash_elem *, void *);
unsigned cache_hash (const struct hash_elem *, void *);
bool cache_less (const struct hash_elem *, const struct hash_elem *, void *);
void cache_delete (int sector_idx);
void
cache_init()
{
	cache = malloc(sizeof(struct buffer_cache));
	if (cache == NULL)
		PANIC ("buffer_cache_init: memory allocation failed (cache)");

	cache->hash = malloc(sizeof(struct hash));
	if (cache->hash == NULL)
		PANIC ("buffer_cache_init: memory allocation failed (hash)");
	if (!hash_init(cache->hash, cache_hash, cache_less, NULL))
		PANIC ("buffer_cache_init: hash init failed");
	
	list_init(&cache->list);
	cache->cnt = 0;

	//lock_init(&cache_lock);
}

void
cache_close ()
{
	hash_destroy (cache->hash, cache_destroyer);
	free (cache->hash);
	free (cache);
}

void
cache_destroyer (struct hash_elem *e, void *aux)
{
	struct cache_entry *entry = hash_entry (e, struct cache_entry, hash_elem);
	block_write(fs_device, entry->sector_idx, entry->data);
}

void *
cache_insert (int sector_idx)
{
	struct cache_entry *entry;

	entry = malloc(sizeof(struct cache_entry));
	if (entry == NULL)
		PANIC ("cache_insert: memory allocation failed (cache_entry)");

	entry->sector_idx = sector_idx;
	entry->data = malloc(BLOCK_SECTOR_SIZE);
	if (entry->data == NULL)
		PANIC ("cache_insert: memory allocation failed (data)");

	if (cache->cnt >= CACHE_MAX) {
		struct cache_entry *victim = list_entry(list_pop_front(&cache->list), struct cache_entry, list_elem);
		block_write(fs_device, victim->sector_idx, victim->data);
		hash_delete (cache->hash, &victim->hash_elem);
		free (victim->data);
		free (victim);
		cache->cnt--;
	}

	block_read (fs_device, sector_idx, entry->data);
	list_push_back(&cache->list, &entry->list_elem);
	hash_insert(cache->hash, &entry->hash_elem);
	cache->cnt++;

	return entry;
}

void *
cache_find (int sector_idx)
{
	struct cache_entry entry;
	struct hash_elem *e;

	entry.sector_idx = sector_idx;
	e = hash_find (cache->hash, &entry.hash_elem);

	return e != NULL ? hash_entry(e, struct cache_entry, hash_elem) : NULL;
}

void
cache_read (void *p, void *buffer, off_t sector_ofs, int chunk_size)
{
	struct cache_entry *entry = (struct cache_entry *)p;
	memcpy (buffer, entry->data + sector_ofs, chunk_size);
}

void
cache_write (void *p, void *buffer, off_t sector_ofs, int chunk_size)
{
	struct cache_entry *entry = (struct cache_entry *)p;
	memcpy (entry->data + sector_ofs, buffer, chunk_size);
	//hex_dump (entry->data + sector_ofs, entry->data + sector_ofs, chunk_size, true);
}

unsigned
cache_hash (const struct hash_elem *e, void *aux)
{
	const struct cache_entry *p = hash_entry(e, struct cache_entry, hash_elem);
	return hash_bytes(&p->sector_idx, sizeof(void *)); 
}

bool
cache_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	const struct cache_entry *a_ = hash_entry(a, struct cache_entry, hash_elem);
	const struct cache_entry *b_ = hash_entry(b, struct cache_entry, hash_elem);
	
	return a_->sector_idx < b_->sector_idx;
}

void
cache_delete (int sector_idx)
{
	//printf("come to cache_delete\n");
	struct cache_entry *e;
	e = cache_find (sector_idx);
	hex_dump (e->data, e->data, 40, true);
	block_write (fs_device, e->sector_idx, e->data);
	hash_delete (cache->hash, &e->hash_elem);
	free (e->data);
	free (e);
	cache->cnt--;
	//printf("end of cache_delete\n");
}
//void
//cache_lock_acquire ()
//{
//	lock_acquire(&cache_lock);
//}

//void
//cache_lock_release ()
//{
//	lock_release(&cache_lock);
//}
