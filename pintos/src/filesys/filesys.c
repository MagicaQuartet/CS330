#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Partition that contains the file system. */
struct block *fs_device;
struct lock fs_lock;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");
	
	cache_init ();
  inode_init ();
  free_map_init ();
	lock_init(&fs_lock);

  if (format) 
    do_format ();
  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
	cache_close ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
	lock_acquire(&fs_lock);
//  block_sector_t inode_sector = 0;
//	struct dir *dir = dir_open_root();
//	bool success = (dir != NULL
//									&& free_map_allocate (1, &inode_sector)
//									&& inode_create (inode_sector, initial_size, 0)
//									&& dir_add (dir, name, inode_sector, false));
	struct dir *dir;
	struct inode *inode;
	block_sector_t inode_sector = 0;
	bool is_relative, success = false, tmp1, tmp2, tmp3;
	char *token, *save_ptr, *copy;

	if (strlen(name) == 0) {
		lock_release(&fs_lock);
		return false;
	}
	printf("filesys_create %s, %d\n", name, initial_size);
	copy = malloc(sizeof(char) * (strlen(name) + 1));
	memcpy(copy, name, strlen(name) + 1);

	is_relative = (copy[0] != '/');

	if (is_relative) {
    printf("relative\n");
		dir = thread_current()->current_dir;
	}
	else {
    printf("root\n");
		dir = dir_open_root();
	}

	for (token = strtok_r (copy, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
		if (inode_is_removed(dir_get_inode(dir))) {
			lock_release(&fs_lock);
			return NULL;
		}
		if (!dir_lookup (dir, token, &inode)) {
			if (!strcmp (token, "."))
				continue;
			else {
				if (save_ptr[0] != '\0') {
					break;
				}
				else {
          tmp1 = free_map_allocate (1, &inode_sector);
          tmp2 = inode_create (inode_sector, initial_size, 0, NULL);
          tmp3 = dir_add (dir, token, inode_sector, false);
          printf("1 , 2, 3, %d, %d, %d\n", tmp1, tmp2, tmp3);
          if (dir == NULL){
            printf("dir is null");
          }
          printf ("name, sector num, is_dir : %s, %d, %d\n", token, inode_sector);
					success = (dir != NULL
										&& tmp1
										&& tmp2
										&& tmp3);
					break;
				}
			}
		}

		if (!inode_is_dir(inode))
			break;
		else {
			dir = dir_open(inode);
		}
	}

	lock_release(&fs_lock);
	free(copy);
	return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
	lock_acquire(&fs_lock);
//  struct dir *dir = dir_open_root();
//  struct inode *inode = NULL;

//	if (dir != NULL)
//    dir_lookup (dir, name, &inode);
//  dir_close (dir);
  printf("come to filesys_open : %s", name);
	struct dir *dir, *prev_dir;
	struct inode *inode;
	bool is_relative;
	char *token, *save_ptr, *copy;
	
	if (strlen(name) == 0) {
		lock_release(&fs_lock);
		return NULL;
	}

	copy = malloc(sizeof(char) * (strlen(name) + 1));
	memcpy(copy, name, strlen(name) + 1);

	is_relative = (copy[0] != '/');

	if (is_relative) {
		dir = thread_current()->current_dir;
    if (dir == NULL){
      printf("????");
    }
	}
	else {
		dir = dir_open_root();
	}
  printf("checking open1\n");
	for (token = strtok_r (copy, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
		if (inode_is_removed(dir_get_inode(dir))) {
      printf("checking open2\n");
			lock_release(&fs_lock);
			return NULL;
		}
		if (!dir_lookup (dir, token, &inode)) {
      printf("checking open3 : %s\n", token);
			if (!strcmp (token, ".")) {
				if (save_ptr[0] != '\0')
					continue;
				else {
					inode = dir_get_inode(dir);
					break;
				}
			}
			else {
				lock_release(&fs_lock);
				free(copy);
				return NULL;
			}
		}
    printf("checking open4\n");
		if (!inode_is_dir(inode)) {
			if (save_ptr[0] != '\0'){
				lock_release(&fs_lock);
				free(copy);
				return NULL;
			}
			else {
				break;
			}
		}
		else {
			dir = dir_open(inode);
		}
	}
	lock_release(&fs_lock);
	free(copy);
  printf("come to file_open\n");
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
	lock_acquire(&fs_lock);
//	struct dir *dir = dir_open_root ();
//	bool success = dir != NULL && dir_remove (dir, name);
	struct dir *dir;
	struct inode *inode;
	bool is_relative, success = false;
	char *token, *save_ptr, *copy;

	if (strlen(name) == 0) {
		lock_release(&fs_lock);
		return false;
	}

	copy = malloc(sizeof(char) * (strlen(name) + 1));
	memcpy(copy, name, strlen(name) + 1);

	is_relative = (copy[0] != '/');

	if (is_relative) {
		dir = thread_current()->current_dir;
	}
	else {
		dir = dir_open_root();
	}

	for (token = strtok_r (copy, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
		if (!dir_lookup (dir, token, &inode)){
			if (!strcmp (token, ".")){
				continue;
			}
			else {
				break;
			}
		}
		
		if (!inode_is_dir(inode)) {
			if (save_ptr[0] != '\0') {
				break;
			}
			else {
				success = dir != NULL && dir_remove (dir, token);
				break;
			}
		}
		else {
			if (save_ptr[0] != '\0')
				dir = dir_open(inode);
			else {
				char temp[NAME_MAX + 1];
				if (dir != NULL && !dir_readdir(dir_open (inode), temp))
					success = dir_remove (dir, token);
				break;
			}
		}
	}
	lock_release(&fs_lock);
	free(copy);
  return success;
}

bool
filesys_chdir (const char *name)
{
	bool success;
	lock_acquire(&fs_lock);
//	printf("chdir %s\n", name);
	success = dir_change_dir (name);
	lock_release(&fs_lock);
	return success;
}

bool
filesys_mkdir (const char *name)
{
	bool success;
	lock_acquire(&fs_lock);
	//printf("mkdir %s\n", name);
	success = dir_make_dir (name);
	lock_release(&fs_lock);
	return success;
}


/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, NULL))
    PANIC ("root directory creation failed");
	thread_current()->current_dir = dir_open_root();
  free_map_close ();
  printf ("done.\n");
}
