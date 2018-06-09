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
    //struct list *sector_list;           /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
		uint32_t is_dir;										/* 0: ordinary file, 1: directory */
		uint32_t direct;
    uint32_t indirect;
    uint32_t double_indirect;
    void *parent;												/* INODE of parent directory */
    uint32_t unused[100];               /* Not used. */
    block_sector_t sectors[21]; // 16 direct 4 indirect 1 double
  };

struct inode_disk_elem
{
	struct list_elem list_elem;
	block_sector_t sector;
};

struct indirect_inode
{
  block_sector_t sectors[128];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static inline size_t
bytes_to_indirect (off_t size)
{
  if (size <= 16 * BLOCK_SECTOR_SIZE){
    return 0;
  }
  else {
    return DIV_ROUND_UP (size - 16 * BLOCK_SECTOR_SIZE, 128 * BLOCK_SECTOR_SIZE);
  }
}

static inline size_t
bytes_to_double_indirect (off_t size)
{
  if (size <= BLOCK_SECTOR_SIZE * 16 + BLOCK_SECTOR_SIZE * 128 * 4){
    return 0;
  }
  else {
    return 1;
  }
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    off_t length;                       /* File size in bytes. */
    uint32_t is_dir;                    /* 0: ordinary file, 1: directory */
    uint32_t direct;
    uint32_t indirect;
    uint32_t double_indirect;
    void *parent;
    block_sector_t sectors[21];
    //struct inode_disk data;             /* Inode content. */
  };

block_sector_t inode_extend(struct inode *, int);
bool get_inode_block (struct inode_disk *, char *, int);
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
	//printf("inode at sector %d - length %d\n", inode->sector, inode->length);
  if (pos < inode->length){
    int tmp;
    uint32_t indirect[128];
    uint32_t double_indirect[128];
    if (pos < BLOCK_SECTOR_SIZE * 16){
      return inode->sectors[pos/BLOCK_SECTOR_SIZE];
    }
    else if (pos < BLOCK_SECTOR_SIZE * 16 + BLOCK_SECTOR_SIZE * 128 * 4){
      tmp = ((pos - 16 * BLOCK_SECTOR_SIZE) / (BLOCK_SECTOR_SIZE * 128)) + 16;
      //printf("byte to sector indirect : %d, %d\n", tmp, ((pos - 16 * BLOCK_SECTOR_SIZE) % (BLOCK_SECTOR_SIZE * 128)) / BLOCK_SECTOR_SIZE);
      block_read (fs_device, inode->sectors[tmp], indirect);
      //hex_dump (0, indirect, 128, true);
      //printf("sector number? : %d\n", indirect[((pos - 16 * BLOCK_SECTOR_SIZE) % (BLOCK_SECTOR_SIZE * 128)) / BLOCK_SECTOR_SIZE]);
      return indirect[((pos - 16 * BLOCK_SECTOR_SIZE) % (BLOCK_SECTOR_SIZE * 128)) / BLOCK_SECTOR_SIZE];
    }
    else {
      block_read (fs_device, inode->sectors[20], double_indirect);
      tmp = (pos - (BLOCK_SECTOR_SIZE * 16) - (BLOCK_SECTOR_SIZE * 128 * 4)) / (BLOCK_SECTOR_SIZE * 128);
      block_read (fs_device, double_indirect[tmp], indirect);
      tmp = (pos - (BLOCK_SECTOR_SIZE * 16) - (BLOCK_SECTOR_SIZE * 128 * 4)) % (BLOCK_SECTOR_SIZE * 128);
      return indirect[tmp / BLOCK_SECTOR_SIZE];
    }
  }
  else
    return -1;
//	printf("byte_to_sector pos %d, length %d\n", pos, inode->data.length);
  /*if (pos < inode->data.length) {
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
    return -1;*/
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
inode_create (block_sector_t sector, off_t length, uint32_t is_dir, void *parent)
{
  struct inode_disk *disk_inode = NULL;
  bool success = true;
  //printf("come to inode_create, %d\n", length);
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  
	disk_inode = calloc (1, sizeof *disk_inode);
  char *zeros;//[BLOCK_SECTOR_SIZE];
  zeros = calloc (1, BLOCK_SECTOR_SIZE);
  if (disk_inode != NULL)
    {
			size_t sectors = bytes_to_sectors (length), i;
      block_sector_t tmp;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
			disk_inode->is_dir = is_dir;
			disk_inode->parent = parent;
			
			// direct
      if (sectors <= 16){
        for (i = 0; i < sectors; i++) {
          success = get_inode_block (disk_inode, zeros, i);
          if (!success) {
            break;
          }
        }
        disk_inode->direct = sectors-1;
      }
      // indirect
      else {
        for (i = 0; i < 16; i++) {
          success = get_inode_block (disk_inode, zeros, i);
          if (!success) {
            break;
          }
        }
        //printf("now go to indirect\n");
        disk_inode->direct = 15;
        sectors -= 16;
        int indirect_idx = 15;
        int indirect_sector = 0;
        struct indirect_inode *indirect;
        // while sectors > 0 and can be covered with indirect.
        if (sectors > 0){
          indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
        }
        while (sectors > 0) {
          if (indirect_sector % 128 == 0) {
            if (indirect != NULL && indirect_idx != 15){
              //printf("come to here : %d\n", indirect_sector);
              block_write (fs_device, disk_inode->sectors[indirect_idx], indirect);
              free (indirect);
            }
            // indirect full break and goto double
            if (indirect_idx == 19) {
              break;
            }
            indirect_idx++;
            indirect_sector = 0;
            success = get_inode_block (disk_inode, zeros, indirect_idx);
            if (!success) {
              break;
            }
            indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
          }
          //printf(" indirect interior : %d\n", indirect_sector);
          if (free_map_allocate (1, &tmp)) { 
            success = true;
            block_write (fs_device, tmp, zeros);
            indirect->sectors[indirect_sector] = tmp;
          }
          else {
            //printf("free_map_allocate fail\n");
            success = false;
            break;
          }
          sectors--;
          indirect_sector++;
        }
        disk_inode->indirect = indirect_idx;
        // ended with indirect, need to write last indirect inode.
        if (bytes_to_double_indirect (length) == 0) {
          block_write (fs_device, disk_inode->sectors[indirect_idx], indirect);
          free (indirect);
        }
        // double indirect
        else {
          struct indirect_inode *double_indirect;
          disk_inode->double_indirect = 20;
          success = get_inode_block (disk_inode, zeros, 20);
          if (!success) {
            block_write (fs_device, sector, disk_inode);
            free (disk_inode);
            free (zeros);
            return success;
          }
          double_indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
          indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
          indirect_idx = -1;
          indirect_sector = 0;
          while (sectors > 0) {
            // indirect block full, need to make new
            if (indirect_sector % 128 == 0) {
              if (indirect != NULL){
                block_write (fs_device, double_indirect->sectors[indirect_idx], indirect);
                free (indirect);
              }
              indirect_idx++;
              indirect_sector = 0;
              // make new indirect block and save sector number in double_indirect block.
              if (free_map_allocate (1, &tmp)) {
                success = true;
                block_write (fs_device, tmp, zeros);
                double_indirect->sectors[indirect_idx] = tmp;
              }
              else {
                success = false;
                break;
              }
              indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
            }
            if (free_map_allocate (1, &tmp)) { 
              success = true;
              block_write (fs_device, tmp, zeros);
              indirect->sectors[indirect_sector] = tmp;
            }
            else {
              success = false;
              break;
            }
            sectors--;
            indirect_sector++;
          }
          // finish store the last indirect block content and double indirect block content to disk.
          block_write (fs_device, double_indirect->sectors[indirect_idx], indirect);
          if (indirect != NULL){
            free (indirect);
          }
          block_write (fs_device, disk_inode->sectors[20], double_indirect);
          free (double_indirect);
        }
      }
      block_write (fs_device, sector, disk_inode);
      free (disk_inode);
    }
  //printf("inode create finish\n");
  free (zeros);
  return success;
}

			/*for (i = 0; i < sectors; i++) {
				struct inode_disk_elem *e = malloc(sizeof(struct inode_disk_elem));
				ASSERT (e != NULL);

				if (free_map_allocate (1, &e->sector)) {
					success = true;
					block_write (fs_device, e->sector, zeros);
					//list_push_back (disk_inode->sector_list, &e->list_elem);
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
  return success;
}*/


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;
  //printf("come to inode_open, sector what? : %d\n", sector);
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
  //printf("come to here\n");
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  struct inode_disk data;
  block_read (fs_device, inode->sector, &data);
  inode->length = data.length;
  inode->direct = data.direct;
  inode->indirect = data.indirect;
  inode->double_indirect = data.double_indirect;
  inode->is_dir = data.is_dir;
  inode->parent = data.parent;
  memcpy (&inode->sectors, &data.sectors, 21 * sizeof(block_sector_t));
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

void inode_update (struct inode *inode);

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  //printf("come to inode_close, what inode? : %d\n", inode->sector);
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  
	inode_update (inode); 
  /* Release resources if this was the last opener. */
	//printf(">>>inode %p<<<\n", inode);
  if (--inode->open_cnt == 0)
    {
			//struct list_elem *e;
			/* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      if (inode->removed)
      {
        free_map_release (inode->sector, 1);
        inode_clear (inode);
      }
			/* Deallocate blocks if removed. */
      /*if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
					for (e = list_begin(inode->data.sector_list); e != list_end(inode->data.sector_list); e = list_next(e)) {
						struct inode_disk_elem *elem = list_entry(e, struct inode_disk_elem, list_elem);
						free_map_release (elem->sector, 1);
					}
        }
        */
      //free (inode); 
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
	uint8_t *bounce = NULL;
  //printf("come to read : %p, %p, %d, %d\n", inode, buffer_, size, offset);
	//cache_lock_acquire();
  while (size > 0) 
    {
			static char buf[512];
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

	//		printf("inode_read_at: read at sector %d\n", sector_idx);

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
			//printf("inode_read_at: sector_idx %d chunk_size %d\n", sector_idx, chunk_size);
      if (chunk_size <= 0)
        break;
      /*
			if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
				block_read (fs_device, sector_idx, buffer + bytes_read);
			}
			else {
				if (bounce == NULL) {
					bounce = malloc(BLOCK_SECTOR_SIZE);
					if (bounce == NULL)
						break;
				}
				block_read(fs_device, sector_idx, bounce);
				memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
			}
      */

			cache_lock_acquire();
			void *entry = cache_find (sector_idx);
			if (entry == NULL) {
				entry = cache_insert (sector_idx);
			}
			cache_read (entry, buffer + bytes_read, sector_ofs, chunk_size);
			cache_lock_release();
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
	//cache_lock_release();

//	block_sector_t sector_idx = byte_to_sector (inode, offset);
//	if (sector_idx != -1) {
//		void *entry = cache_find (sector_idx);
//		if (entry == NULL) {
//			entry = cache_insert (sector_idx);
//		}
//	}
	free(bounce);	
//	printf("inode_read_at: bytes_read %d\n", bytes_read);
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
	uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

	//cache_lock_acquire();
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
			static char buf[512];
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      //printf("Inode write at sector : %d\n", sector_idx);
			int sector_ofs = offset % BLOCK_SECTOR_SIZE;
			
			if (sector_idx == -1) {
				int cnt;
				if (inode_length(inode) % BLOCK_SECTOR_SIZE == 0) {
					cnt = (offset - inode_length(inode)) / BLOCK_SECTOR_SIZE + 1;
				}
				else {
					cnt = offset / BLOCK_SECTOR_SIZE - inode_length(inode) / BLOCK_SECTOR_SIZE;
        }
				
				if (cnt > 0){
          //printf("call extend : %d\n", cnt);
					sector_idx = inode_extend(inode, cnt);
        }
				else {
          sector_idx = byte_to_sector (inode, inode->length - 1);
					//sector_idx = list_entry(list_back(inode->data.sector_list), struct inode_disk_elem, list_elem)->sector;
				}
				inode_update(inode);
			}
//			printf("inode_write_at: read at sector %d\n", sector_idx);

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left && inode_left > 0? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
			if (inode_left <= 0) {
				inode->length += chunk_size - inode_left;
			}
      if (chunk_size <= 0)
        break;
      /*
			if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
				block_write (fs_device, sector_idx, buffer + bytes_written);
			}
			else {
				if (bounce == NULL) {
					bounce = malloc(BLOCK_SECTOR_SIZE);
					if (bounce == NULL)
						break;
				}
				if (sector_ofs > 0 || chunk_size < sector_left)
					block_read (fs_device, sector_idx, bounce);
				else
					memset (bounce, 0, BLOCK_SECTOR_SIZE);
				memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size)
;
				block_write(fs_device, sector_idx, bounce);
			}
      */

			cache_lock_acquire();
			void *entry = cache_find (sector_idx);
			if (entry == NULL) {
				entry = cache_insert (sector_idx);
			}
			cache_write (entry, buffer + bytes_written, sector_ofs, chunk_size);

			cache_lock_release();

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
	//cache_lock_release();
	free (bounce);
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
  return inode->length;
}

block_sector_t
inode_extend (struct inode *inode, int cnt)
{
	//struct inode_disk_elem *elem;
	static char *zeros;//[BLOCK_SECTOR_SIZE];
  zeros = calloc (1, BLOCK_SECTOR_SIZE);
	int i;
  block_sector_t tmp;
  struct indirect_inode * indirect;
  //printf("come to extend : %d, direct : %d\n", cnt, inode->direct);
  block_sector_t current = bytes_to_sectors (inode->length);
  // direct extend
  if (inode->direct == -1){
    inode->direct = 0;
    cnt--;
    if (free_map_allocate (1, &tmp)) {
      block_write (fs_device, tmp, zeros);
      inode->sectors[inode->direct] = tmp;
    }
  }
  while (((inode->direct < 15) && (cnt > 0))) {
    //printf("??? : %d", inode->direct);
    inode->direct = inode->direct + 1;
    //printf("??? : %d", current);
    cnt--;
    if (free_map_allocate (1, &tmp)) {
      block_write (fs_device, tmp, zeros);
      inode->sectors[inode->direct] = tmp;
    }
  }
  //printf("cnt?? : %d\n", cnt);
  //printf("??? : %d", inode->direct);
  if (cnt == 0) {
    //printf("finish");
    free (zeros);
    return tmp;
  }
  // indirect extend
  //printf("inode->length : %d\n", inode->length);
  block_sector_t next_indirect;
  block_sector_t indirect_idx;
  if (cnt > 0){
		int sector_size = bytes_to_sectors(inode->length);
    next_indirect = sector_size >= 16 ? (sector_size - 16) / 128 : 0;
    indirect_idx = sector_size >= 16 ? (sector_size - 16) % 128 : 0;
  }
  else {
    next_indirect = 0;
    indirect_idx = 0;
  }
  //printf("next_indirect, indirect_idx : %d, %d\n", next_indirect, indirect_idx);
  indirect = (struct indirect_inode *)calloc (1, sizeof (struct indirect_inode));

	if (indirect_idx == 0) {
		if (free_map_allocate (1, &tmp)) {
			block_write(fs_device, tmp, zeros);
			inode->sectors[16+next_indirect] = tmp;
		}
	}

  if (indirect_idx != 0 && cnt > 0) {
    inode->indirect = next_indirect + 16;
    block_read (fs_device, inode->sectors[inode->indirect], indirect);
    //hex_dump (0, indirect, sizeof(block_sector_t), true);
    //hex_dump (0, inode->sectors, 21, true);
  }
  else if (cnt > 0) {
    inode->indirect = next_indirect + 16;
  }
  while ((inode->indirect < 20) && (cnt > 0)) {
    
    if (indirect_idx % 128 == 0 && indirect_idx > 0) {
      //hex_dump (0, indirect, 128, true);
      //printf("make indirect inode\n");
      if (indirect != NULL){
        //hex_dump (0, indirect, 128, true);
        block_write (fs_device, inode->sectors[inode->indirect], indirect);
        free (indirect);
      }
      // if we can't add more indirect node, break
      if (inode->indirect == 19){
        break;
      }
      inode->indirect = inode->indirect + 1;
      indirect_idx = 0;
      if (free_map_allocate (1, &tmp)) {
        block_write (fs_device, tmp, zeros);
        inode->sectors[inode->indirect] = tmp;
      }
      indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
    }
    // add block sector nums to indirect node
    if (free_map_allocate (1, &tmp)) { 
      block_write (fs_device, tmp, zeros);
      indirect->sectors[indirect_idx] = tmp;
      //printf("indirect where : %d\n", indirect->sectors[indirect_idx]);
    }
    cnt--;
    indirect_idx++;
  }
  // finish in here?
  if (cnt == 0) {
    if (indirect != NULL) {
      //printf("finish in here?\n");
      //hex_dump (0, indirect, 128 * sizeof(block_sector_t), true);
      block_write (fs_device, inode->sectors[inode->indirect], indirect);
      free (indirect);
    }
    free (zeros);
    return tmp;
  }
  // double indirect extend
  next_indirect = (bytes_to_sectors (inode->length) - (16 + 128 * 4)) / 128;
  block_sector_t next_idx = (bytes_to_sectors (inode->length) - (16 + 128 * 4)) % 128;
  indirect = (struct indirect_inode *)calloc (1, sizeof (struct indirect_inode));
  struct indirect_inode *double_indirect = (struct indirect_inode *)calloc (1, sizeof (struct indirect_inode));
  if (next_indirect == 0 && next_idx == 0){
    if (free_map_allocate(1, &tmp)) {
      block_write (fs_device, tmp, zeros);
      inode->sectors[20] = tmp;
    }
    if (free_map_allocate(1, &tmp)) {
      block_write (fs_device, tmp, zeros);
      double_indirect->sectors[next_indirect] = tmp;
    }
  }
  // already using double, and left to extend. so we have to read it.
  if (!(next_indirect == 0 && next_idx == 0) && cnt > 0){
    inode->double_indirect = 20;
    block_read (fs_device, inode->sectors[inode->double_indirect], double_indirect);
  }
  // already using indirect, and left to extend. so we have to read it.
  if (!(next_idx == 0) && cnt > 0){
    block_read (fs_device, double_indirect->sectors[next_indirect], indirect);
  }
  while (cnt > 0) {
    if (next_idx % 128 == 0) {
      if (indirect != NULL) {
        block_write (fs_device, double_indirect->sectors[next_indirect], indirect);
        free (indirect);
      }
      next_indirect++;
      next_idx = 0;
      if (free_map_allocate (1, &tmp)) {
        block_write (fs_device, tmp, zeros);
        double_indirect->sectors[next_indirect] = tmp;
      }
      indirect = (struct indirect_inode *)calloc (1, sizeof(struct indirect_inode));
    }
    if (free_map_allocate (1, &tmp)) {
      block_write (fs_device, tmp, zeros);
      indirect->sectors[next_idx] = tmp;
    }
    cnt--;
    next_idx++;
  }

  if (cnt == 0) {
    if (indirect != NULL) {
      block_write (fs_device, double_indirect->sectors[next_indirect], indirect);
      free (indirect);
    }
    block_write (fs_device, inode->sectors[20], double_indirect);
    free (double_indirect);
    free (zeros);
    return tmp;
  }
  free (zeros);
  return tmp;
  /*
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
  */
}

bool
inode_is_dir (struct inode *inode)
{
	//hex_dump(0, inode, sizeof(struct inode), true);
	return inode != NULL ? inode->is_dir == 1 : false;
}

bool
inode_is_removed (struct inode *inode)
{
	return inode->removed;
}

void *
inode_get_parent (struct inode *inode)
{
	ASSERT (inode_is_dir (inode));
	return inode->parent;
}

void
inode_clear (struct inode *inode)
{
  size_t sectors = bytes_to_sectors (inode->length);
  size_t indirect_sectors = bytes_to_indirect (inode->length);
  size_t double_indirect_sector = bytes_to_double_indirect (inode->length);
  block_sector_t idx = 0;
  while (sectors > 0 && idx < 16) {
    free_map_release (inode->sectors[idx], 1);
    idx++;
    sectors--;
  }
  while (sectors > 0 && indirect_sectors > 0 && idx < 20){
    size_t left = sectors < 128 ? sectors : 128;
    indirect_clear (inode->sectors[idx], left);
    sectors -= left;
    indirect_sectors--;
    idx++;
  }
  if (double_indirect_sector == 1){
    double_indirect_clear (inode->sectors[20], indirect_sectors, sectors);
  }
}

void
indirect_clear (block_sector_t sector, size_t cnt)
{
  int i;
  struct indirect_inode *indirect;
  indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
  block_read (fs_device, sector, indirect);
  for (i = 0; i < cnt; i++){
    free_map_release (indirect->sectors[i], 1);
  }
  free_map_release (sector, 1);
}

void
double_indirect_clear (block_sector_t sector, size_t left_indirect, size_t left_all)
{
  int i;
  struct indirect_inode *double_indirect;
  double_indirect = (struct indirect_inode *)calloc(1, sizeof(struct indirect_inode));
  block_read (fs_device, sector, double_indirect);
  for (i = 0; i < left_indirect; i++){
    size_t left = left_all < 128 ? left_all : 128;
    indirect_clear (double_indirect->sectors[i], left);
    left_all -= left;
  }
  free_map_release (sector, 1);
}

bool
get_inode_block (struct inode_disk *disk_inode, char *zeros, int idx) {
  bool success;
  block_sector_t tmp;
  //printf("get direct block, what is idx : %d\n", idx);
  //hex_dump (zeros, zeros, BLOCK_SECTOR_SIZE, true);
  if (free_map_allocate (1, &tmp)) {
    success = true;
    block_write (fs_device, tmp, zeros);
    disk_inode->sectors[idx] = tmp;
    //printf("whar sector? : %d\n", tmp);
  }
  else {
    success = false;
  }
  return success;
}

void
inode_update (struct inode *inode) {
	struct inode_disk *disk_inode = (struct inode_disk *)malloc(sizeof(struct inode_disk));
	ASSERT(disk_inode != NULL);

	disk_inode->length = inode->length;
	//printf("inode %d - length %d\n", inode->sector, inode->length);
  disk_inode->magic = INODE_MAGIC;
  disk_inode->is_dir = inode->is_dir;
  disk_inode->direct = inode->direct;
  disk_inode->indirect = inode->indirect;
  disk_inode->double_indirect = inode->double_indirect;
  disk_inode->parent = inode->parent;
  memcpy (&disk_inode->sectors, &inode->sectors, 21 * sizeof (block_sector_t));
 	block_write (fs_device, inode->sector, disk_inode);
	free(disk_inode);
}
