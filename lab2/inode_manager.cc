#include "inode_manager.h"
#include <ctime>
// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
<<<<<<< HEAD
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
=======
  if(id > BLOCK_NUM || id < 0 || buf == NULL){ 
    return;
  }

  memcpy(buf, blocks[id - 1], BLOCK_SIZE); 
>>>>>>> lab1
}

void
disk::write_block(blockid_t id, const char *buf)
{
<<<<<<< HEAD
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
=======
  if (id <= 0 || id > BLOCK_NUM || buf == NULL) {
      return;
    }
    memcpy(blocks[id - 1], buf, BLOCK_SIZE);
>>>>>>> lab1
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t block_manager::alloc_block() {

    char bitmap[BLOCK_SIZE];
    int  bitmap_bnum, pos;
    bool free_block_found;

    // search from first block after inode table
    for (bitmap_bnum = BBLOCK(IBLOCK(INODE_NUM, sb.nblocks) + 1);
         bitmap_bnum <= BBLOCK(BLOCK_NUM); bitmap_bnum++) {
        free_block_found = false;

        // read bitmap
        d->read_block(bitmap_bnum, bitmap);

        // read every bit
        for (pos = 0; pos < BPB; ++pos) {
            char byte = bitmap[pos / 8];
            char bit  = byte & ((char)1 << (7 - pos % 8));

            if (bit == 0) { // free block found!
                // mark as used, and write to block
                bitmap[pos / 8] = byte | ((char)1 << (7 - pos % 8));
                d->write_block(bitmap_bnum, bitmap);
                free_block_found = true;
                break;
            }
        }

        if (free_block_found) {
            break;
        }
    }

    if (free_block_found) {
        blockid_t bnum = (bitmap_bnum - BBLOCK(1)) * BPB + pos + 1;


        return bnum;
    } else {
        printf("alloc_block: no empty block available\n");
        return 0;
    }
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */

  if(id <= 0 || id > BLOCK_NUM) return; //out of range

    char buf[BLOCK_SIZE];
    d->read_block(BBLOCK(id), buf);
    // set coresponding bit to zero, and write back
    char byte = buf[(id - 1) % BPB / 8];
    buf[((id - 1) % BPB) / 8] = byte & ~((char)1 << (7 - (id - 1) % BPB % 8));
    d->write_block(BBLOCK(id), buf);

  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

  /* mark bootblock, superblock, bitmap, inode table region as used */
  char buf[BLOCK_SIZE];
  blockid_t cur = 0;
  blockid_t ending = (2 + ((sb.nblocks) + BPB - 1)/BPB + ((sb.ninodes) + IPB - 1)/IPB);
  while (cur < ending) {
    read_block(BBLOCK(cur), buf);
    for (int i = 0; i < BLOCK_SIZE && cur < ending; ++i) {
      unsigned char mask = 0x80;
      while (mask > 0 && cur < ending) {
        buf[i] = buf[i] | mask;
        mask = mask >> 1;
        ++cur;
      }
    }
    write_block(BBLOCK(cur - 1), buf);
  }

  bzero(buf, sizeof(buf));
  memcpy(buf, &sb, sizeof(sb));
  write_block(1, buf);


}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */

 if (type == 0) {
        return 0;
    }

    struct inode *ino;
    char buf[BLOCK_SIZE];
    uint32_t inum;

    for (inum = 1; inum <= INODE_NUM; inum++) {
        bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
        ino = (struct inode *)buf + (inum - 1) % IPB;
        if (ino->type == 0) { // find an empty inode
            break;
        }
    }

    if (inum > INODE_NUM) {
        return 0;
    }

    ino->type = type;
    ino->size = 0;
    unsigned int now = (unsigned int)time(NULL);
    ino->atime = now;
    ino->mtime = now;
    ino->ctime = now;
    bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
    return inum;

}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */

  if ((inum <= 0) || (inum > INODE_NUM)) {
      return;
  }
  struct inode *ino = get_inode(inum);

  if (ino == NULL) {
      return;
  }
  ino->type = 0;
  put_inode(inum, ino);
  free(ino);
  
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your lab1 code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  char block[BLOCK_SIZE];
  inode_t * ino = get_inode(inum);
  char * buf = (char *)malloc(ino->size);
  unsigned int cur = 0;
  for (int i = 0; i < NDIRECT && cur < ino->size; ++i) {
    if (ino->size - cur > BLOCK_SIZE) {//read full block
      bm->read_block(ino->blocks[i], buf + cur);
      cur += BLOCK_SIZE;
    } else {//read left 
      int len = ino->size - cur;
      bm->read_block(ino->blocks[i], block);
      memcpy(buf + cur, block, len);
      cur += len;
    }
  }

  if (cur < ino->size) {//
    char indirect[BLOCK_SIZE];
    bm->read_block(ino->blocks[NDIRECT], indirect);
    for (unsigned int i = 0; i < NDIRECT && cur < ino->size; ++i) {
      blockid_t ix = *((blockid_t *)indirect + i);
      if (ino->size - cur > BLOCK_SIZE) {
        bm->read_block(ix, buf + cur);
        cur += BLOCK_SIZE;
      } else {
        int len = ino->size - cur;
        bm->read_block(ix, block);
        memcpy(buf + cur, block, len);
        cur += len;
      }
    }
  }

  *buf_out = buf;
  *size = ino->size;
  ino->atime = std::time(0);
  ino->ctime = std::time(0);
  put_inode(inum, ino);
  free(ino);
}
/* alloc/free blocks if needed */
void inode_manager::write_file(uint32_t inum, const char *buf, int size) {
    // invalid input
    if ((inum <= 0) || (inum > INODE_NUM)) {
        printf("write_file: inum out of range: %d\n", inum);
        return;
    }

    if (buf == NULL) {
        printf("write_file: buf pointer is NULL\n");
        return;
    }

  char block[BLOCK_SIZE];
  char indirect[BLOCK_SIZE];
  inode_t * ino = get_inode(inum);
  unsigned int old_block_num = (ino->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  unsigned int new_block_num = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  /* free some blocks */
  if (old_block_num > new_block_num) {
    if (new_block_num > NDIRECT) {
      bm->read_block(ino->blocks[NDIRECT], indirect);
      for (unsigned int i = new_block_num; i < old_block_num; ++i) {
        bm->free_block(*((blockid_t *)indirect + (i - NDIRECT)));
      }
    } else {
      if (old_block_num > NDIRECT) {
        bm->read_block(ino->blocks[NDIRECT], indirect);
        for (unsigned int i = NDIRECT; i < old_block_num; ++i) {
          bm->free_block(*((blockid_t *)indirect + (i - NDIRECT)));
        }
        bm->free_block(ino->blocks[NDIRECT]);
        for (unsigned int i = new_block_num; i < NDIRECT; ++i) {
          bm->free_block(ino->blocks[i]);
        }
      } else {
        for (unsigned int i = new_block_num; i < old_block_num; ++i) {
          bm->free_block(ino->blocks[i]);
        }
      }
    }
  }

  /* new some blocks */
  if (new_block_num > old_block_num) {
    if (new_block_num <= NDIRECT) {
      for (unsigned int i = old_block_num; i < new_block_num; ++i) {
        ino->blocks[i] = bm->alloc_block();
      }
    } else {
      if (old_block_num <= NDIRECT) {
        for (unsigned int i = old_block_num; i < NDIRECT; ++i) {
          ino->blocks[i] = bm->alloc_block();
        }
        ino->blocks[NDIRECT] = bm->alloc_block();

        bzero(indirect, BLOCK_SIZE);
        for (unsigned int i = NDIRECT; i < new_block_num; ++i) {
          *((blockid_t *)indirect + (i - NDIRECT)) = bm->alloc_block();
        }
        bm->write_block(ino->blocks[NDIRECT], indirect);
      } else {
        bm->read_block(ino->blocks[NDIRECT], indirect);
        for (unsigned int i = old_block_num; i < new_block_num; ++i) {
          *((blockid_t *)indirect + (i - NDIRECT)) = bm->alloc_block();
        }
        bm->write_block(ino->blocks[NDIRECT], indirect);
      }
    }
  }

  /* write file content */

  int cur = 0;
  for (int i = 0; i < NDIRECT && cur < size; ++i) {
    if (size - cur > BLOCK_SIZE) {
      bm->write_block(ino->blocks[i], buf + cur);
      cur += BLOCK_SIZE;
    } else {
      int len = size - cur;
      memcpy(block, buf + cur, len);
      bm->write_block(ino->blocks[i], block);
      cur += len;
    }
  }

  if (cur < size) {
    bm->read_block(ino->blocks[NDIRECT], indirect);
    for (unsigned int i = 0; i < NINDIRECT && cur < size; ++i) {
      blockid_t ix = *((blockid_t *)indirect + i);
      if (size - cur > BLOCK_SIZE) {
        bm->write_block(ix, buf + cur);
        cur += BLOCK_SIZE;
      } else {
        int len = size - cur;
        memcpy(block, buf + cur, len);
        bm->write_block(ix, block);
        cur += len;
      }
    }
  }

  /* update inode */
  ino->size = size;
  ino->mtime = std::time(0);
  ino->ctime = std::time(0);
  put_inode(inum, ino);
  free(ino);
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your lab1 code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  inode_t * ino = get_inode(inum);

  if (ino) {
    a.type = ino->type;
    a.atime = ino->atime;
    a.mtime = ino->mtime;
    a.ctime = ino->ctime;
    a.size = ino->size;

    free(ino);
    }

}



void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */

    if ((inum <= 0) || (inum > INODE_NUM)) {
        return;
    }
   inode_t * ino = get_inode(inum);
    unsigned int block_num = (ino->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (block_num <= NDIRECT) {
      for (unsigned int i = 0; i < block_num; ++i) {
        bm->free_block(ino->blocks[i]);
      }
    } else {
      for (int i = 0; i < NDIRECT; ++i) {
        bm->free_block(ino->blocks[i]);
      }
      char indirect[BLOCK_SIZE];
      bm->read_block(ino->blocks[NDIRECT], indirect);
      for (unsigned int i = 0; i < block_num - NDIRECT; ++i) {
        bm->free_block(*((blockid_t *)indirect + i));
      }
      bm->free_block(ino->blocks[NDIRECT]);
    }
    free_inode(inum);
    free(ino);
  
  return;
}
