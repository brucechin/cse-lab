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
  if(id > BLOCK_NUM || id < 0 || buf == NULL){ 
    return;
  }

  memcpy(buf, blocks[id - 1], BLOCK_SIZE); 
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (id <= 0 || id > BLOCK_NUM || buf == NULL) {
      return;
    }
    memcpy(blocks[id - 1], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */

  char buf[BLOCK_SIZE];
  blockid_t cur = 0;
  while (cur < sb.nblocks) {
    read_block(BBLOCK(cur), buf);
    for (int i = 0; i < BLOCK_SIZE && cur < sb.nblocks; ++i) {
      unsigned char mask = 0x80;
      while (mask > 0 && cur < sb.nblocks) {
        if ((buf[i] & mask) == 0) {
          buf[i] = buf[i] | mask;
          write_block(BBLOCK(cur), buf);
          return cur;
        }
        mask = mask >> 1;
        ++cur;
      }
    }
  }
  exit(0);
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
  read_block(BBLOCK(id), buf);

  int index = (id % BPB) >> 3;
  unsigned char mask = 0xFF ^ (1 << (7 - ((id % BPB) & 0x7)));
  buf[index] = buf[index] & mask;

  write_block(BBLOCK(id), buf);

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

  if(type == 0){
    return 0; //invalid type
  }

  struct inode * inode;
  char buf[BLOCK_SIZE];
  uint32_t inum;

  for(inum = 1; inum <= INODE_NUM; inum++){
    bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
    inode = (struct inode*)buf + (inum - 1) % IPB;
    if(inode->type == 0) break;
  }

  if(inum > INODE_NUM) return 0; //no empty inode left

  unsigned int now = (unsigned int)time(NULL);
  inode->atime = now;
  inode->ctime = now;
  inode->mtime = now;
  inode->type = type;
  inode->size = 0;
  
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

  return;
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
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  
  if(inum <= 0 || inum > INODE_NUM || buf_out == NULL || size == NULL) return;
  

  struct inode* inode = get_inode(inum);

  if(inode == NULL){
    printf("read_file: inode not found %d\n",inum);
    return;
  }
  *size = inode->size;
  char* buf = (char*)malloc(inode->size);// read all buffer
  *buf_out = buf;
  char block[BLOCK_SIZE];

  unsigned int cur = 0;
  for (int i = 0; i < NDIRECT && cur < inode->size; ++i) {
    if (inode->size - cur > BLOCK_SIZE) {
      bm->read_block(inode->blocks[i], buf + cur);
      cur += BLOCK_SIZE;
    } else {// less than one full block to read
      int len = inode->size - cur;
      bm->read_block(inode->blocks[i], block);
      memcpy(buf + cur, block, len);
      cur += len;
    }
  }

  if (cur < inode->size) { //need read from indirect blocks
    char indirect[BLOCK_SIZE];
    bm->read_block(inode->blocks[NDIRECT], indirect);
    for (unsigned int i = 0; i < NDIRECT && cur < inode->size; i++) {
      blockid_t ix = *((blockid_t *)indirect + i);//get the i-th indirect block id
      if (inode->size - cur > BLOCK_SIZE) {//read full block
        bm->read_block(ix, buf + cur);
        cur += BLOCK_SIZE;
      } else {
        int len = inode->size - cur;
        bm->read_block(ix, block);
        memcpy(buf + cur, block, len);
        cur += len;
      }
    }
  }

  unsigned int now = (unsigned int)time(NULL);
  inode->atime = now;
  inode->ctime = now;
  put_inode(inum, inode);
  free(inode);
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */

  if(inum <= 0 || inum > INODE_NUM || buf == NULL || size < 0) return;

  struct inode* inode = get_inode(inum);

  if(inode == NULL){
    printf("write_file: inode not found %d\n",inum);
    return;
  }
  
  char block[BLOCK_SIZE];
  char indirect[BLOCK_SIZE];
  unsigned int old_block_num = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  unsigned int new_block_num = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  
  if (old_block_num > new_block_num) {//free extra blocks
    if (new_block_num > NDIRECT) {
      bm->read_block(inode->blocks[NDIRECT], indirect);
      for (unsigned int i = new_block_num; i < old_block_num; ++i) {
        bm->free_block(*((blockid_t *)indirect + (i - NDIRECT)));
      }
    } else {
      if (old_block_num > NDIRECT) {
        bm->read_block(inode->blocks[NDIRECT], indirect);
        for (unsigned int i = NDIRECT; i < old_block_num; ++i) {
          bm->free_block(*((blockid_t *)indirect + (i - NDIRECT)));
        }
        bm->free_block(inode->blocks[NDIRECT]);
        for (unsigned int i = new_block_num; i < NDIRECT; ++i) {
          bm->free_block(inode->blocks[i]);
        }
      } else {
        for (unsigned int i = new_block_num; i < old_block_num; ++i) {
          bm->free_block(inode->blocks[i]);
        }
      }
    }
  }

  if (new_block_num > old_block_num) { // need allocate more blocks for new file
    if (new_block_num <= NDIRECT) {
      for (unsigned int i = old_block_num; i < new_block_num; ++i) {
        inode->blocks[i] = bm->alloc_block();
      }
    } else {
      if (old_block_num <= NDIRECT) {
        for (unsigned int i = old_block_num; i < NDIRECT; ++i) {
          inode->blocks[i] = bm->alloc_block();
        }
        inode->blocks[NDIRECT] = bm->alloc_block();

        bzero(indirect, BLOCK_SIZE);
        for (unsigned int i = NDIRECT; i < new_block_num; ++i) {
          *((blockid_t *)indirect + (i - NDIRECT)) = bm->alloc_block();
        }
        bm->write_block(inode->blocks[NDIRECT], indirect);
      } else {
        bm->read_block(inode->blocks[NDIRECT], indirect);
        for (unsigned int i = old_block_num; i < new_block_num; ++i) {
          *((blockid_t *)indirect + (i - NDIRECT)) = bm->alloc_block();
        }
        bm->write_block(inode->blocks[NDIRECT], indirect);
      }
    }
  }

  int cur = 0;
  for (int i = 0; i < NDIRECT && cur < size; ++i) { // similar to read file part
    if (size - cur > BLOCK_SIZE) {
      bm->write_block(inode->blocks[i], buf + cur);
      cur += BLOCK_SIZE;
    } else {
      int len = size - cur;
      memcpy(block, buf + cur, len);
      bm->write_block(inode->blocks[i], block);
      cur += len;
    }
  }

  if (cur < size) {
    bm->read_block(inode->blocks[NDIRECT], indirect);
    for (unsigned int i = 0; i < NDIRECT && cur < size; ++i) {
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
  
  unsigned int now = (unsigned int)time(NULL);
  inode->atime = now;
  inode->ctime = now;
  inode->mtime = now;
  put_inode(inum,inode);
  free(inode);
   
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */

  if ((inum <= 0) || (inum > INODE_NUM)) return;

  struct inode* inode = get_inode(inum);

  if(inode == NULL) return;

  a.type  = inode->type;
  a.atime = inode->atime;
  a.mtime = inode->mtime;
  a.ctime = inode->ctime;
  a.size  = inode->size;

  free(inode);
  
  
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  
  return;
}
