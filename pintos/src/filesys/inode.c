#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
		//struct lock sector_lock;
    struct list *sector_list;            /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };

struct inode_disk_elem
{
	struct list_elem list_elem;
	block_sector_t sector;
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

block_sector_t inode_extend(struct inode *, int);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length) {
		int i;
		struct list_elem *e = list_front(inode->data.sector_list);
		for (i = 0; i < pos / BLOCK_SECTOR_SIZE; i++) {
			e = list_next(e);
			if (e == list_end(inode->data.sector_list))
				return -1;
		}

    return list_entry(e, struct inode_disk_elem, list_elem)->sector;
	}
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
			static char zeros[BLOCK_SECTOR_SIZE];
			size_t sectors = bytes_to_sectors (length), i;
			disk_inode->sector_list = malloc(sizeof(struct list)); 
			list_init(disk_inode->sector_list);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

			for (i = 0; i < sectors; i++) {
				struct inode_disk_elem *e = malloc(sizeof(struct inode_disk_elem));
				ASSERT (e != NULL);

				if (free_map_allocate (1, &e->sector)) {
					success = true;
					block_write (fs_device, e->sector, zeros);
					list_push_back (disk_inode->sector_list, &e->list_elem);
				}
				else {
					success = false;
					break;
				}
			}
			
			if (sectors == 0)
				success = true;

			block_write (fs_device, sector, disk_inode);
			free (disk_inode);
		}
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
			struct list_elem *e;
      
			/* Remove from inode list and release lock. */
      list_remove (&inode->elem);
		
			block_write (fs_device, inode->sector, &inode->data);
      
			/* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
					for (e = list_begin(inode->data.sector_list); e != list_end(inode->data.sector_list); e = list_next(e)) {
						struct inode_disk_elem *elem = list_entry(e, struct inode_disk_elem, list_elem);
						free_map_release (elem->sector, 1);
					}
        }
      free (inode); 
    }

}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

	//cache_lock_acquire();
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
			
			void *entry = cache_find (sector_idx);
			if (entry == NULL) {
				entry = cache_insert (sector_idx);
			}
			cache_read (entry, buffer + bytes_read, sector_ofs, chunk_size);
			
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
	//cache_lock_release();

	block_sector_t sector_idx = byte_to_sector (inode, offset);
	if (sector_idx != -1) {
		void *entry = cache_find (sector_idx);
		if (entry == NULL) {
			entry = cache_insert (sector_idx);
		}
	}
	
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

	//cache_lock_acquire();
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
			int sector_ofs = offset % BLOCK_SECTOR_SIZE;
			
			if (sector_idx == -1) {
				int cnt;
				if (inode_length(inode) % BLOCK_SECTOR_SIZE == 0) {
					cnt = (offset - inode_length(inode)) / BLOCK_SECTOR_SIZE + 1;
				}
				else {
					cnt = offset / BLOCK_SECTOR_SIZE - inode_length(inode) / BLOCK_SECTOR_SIZE;
				}
				
				if (cnt > 0)
					sector_idx = inode_extend(inode, cnt);
				else {
					sector_idx = list_entry(list_back(inode->data.sector_list), struct inode_disk_elem, list_elem)->sector;
				}
			}

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left && inode_left > 0? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
			if (inode_left <= 0) {
				inode->data.length += chunk_size - inode_left;
			}
      if (chunk_size <= 0)
        break;

			void *entry = cache_find (sector_idx);
			if (entry == NULL) {
				entry = cache_insert (sector_idx);
			}
			cache_write (entry, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
	//cache_lock_release();
	
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

block_sector_t
inode_extend (struct inode *inode, int cnt)
{
	struct inode_disk_elem *elem;
	static char zeros[BLOCK_SECTOR_SIZE];
	int i;

	for (i = 0; i < cnt; i++) {
		elem = malloc(sizeof(struct inode_disk_elem));
		ASSERT (elem != NULL);
		if (free_map_allocate (1, &elem->sector)) {
			block_write (fs_device, elem->sector, zeros);
			list_push_back (inode->data.sector_list, &elem->list_elem);	
		}
		else
			return -1;
	}
	
	return elem->sector;
}
