#include "inode_manager.h"

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

  bool has_free_block = false;
  uint32_t block_index, offset;
  char buf[BLOCK_SIZE];

  for(block_index = 0; block_index <= BLOCK_NUM; block_index += BPB){

    d->read_block(BBLOCK(block_index), buf); //read into buf

    for(offset = 0; offset < BPB && block_index + offset < BLOCK_NUM; offset++){ //iterate all bits of buf
      char byte = buf[offset / 8];
      char mask = ((char)1 << (offset % 8));
      if((mask & byte) == 0){// find free block
        buf[offset / 8] = byte | mask;
        d->write_block(BBLOCK(block_index), buf);
        has_free_block = true;
        break;
      }
    }
    
    if(has_free_block) break;

  }

  if(has_free_block){
    uint32_t result = block_index + offset;
    return result;
  }else{
    printf("allock_block: no empty block found\n");
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

  int byte_offset = id % BPB;
  char byte = buf[byte_offset / 8];
  buf[byte_offset / 8] = byte & ~((char)1 << (byte_offset % 8)); // set this block marked free

  d->write_block(BBLOCK(id), buf); //write buf back
  
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
  char* data_out = (char*)malloc(inode->size);
  *buf_out = data_out;
  memset(data_out, 0, inode->size);
  char buf[BLOCK_SIZE];
  char indirect_buf[BLOCK_SIZE / sizeof(blockid_t)];
  //int block_num = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  bool use_indirect = (inode->size > (NDIRECT * BLOCK_SIZE));

  uint32_t* indirect_array;
  if(use_indirect){//load indirect block id
    bm->read_block(inode->blocks[NDIRECT], indirect_buf);
    indirect_array = (uint32_t*)indirect_buf;
  }

  uint32_t remain_len = inode->size, read_len = 0, read_block = 0;

  for(uint32_t total_len = 0; remain_len > 0; remain_len -= read_len, total_len += read_len){
    read_len = MIN(BLOCK_SIZE, remain_len);

    if(read_block >= NDIRECT){//indirect read
      bm->read_block(indirect_array[read_block - NDIRECT], buf);
      memcpy(data_out, buf, read_len);
      read_block++;
      
    }else{//direct read
      bm->read_block(inode->blocks[read_block], buf);
      memcpy(data_out, buf, read_len);
      read_block++;
    }

    data_out += read_len; // update read buffer pointer location
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
  //old file info
  uint32_t old_total_block = inode->size / BLOCK_SIZE + (inode->size % BLOCK_SIZE == 0 ? 0 : 1);
  bool old_use_indirect = (old_total_block > NDIRECT);


  inode->size = MIN(MAXFILE * BLOCK_SIZE, (unsigned)size);
  //write file info
  char indirect_buf[BLOCK_SIZE], write_buf[BLOCK_SIZE];
  uint32_t* indirect_array;
  uint32_t remain_len = inode->size, write_len = 0, write_block = 0, total_block = inode->size / BLOCK_SIZE + (inode->size % BLOCK_SIZE == 0 ? 0 : 1);
  uint32_t total_write_len = 0;
  bool use_indirect = (total_block > NDIRECT);

  
  if(old_use_indirect){//free old indirect blocks
    uint32_t indirect_block = inode->blocks[NDIRECT];
    char old_indirect_buf[BLOCK_SIZE]; // read indirect blocks
    bm->read_block(indirect_block,old_indirect_buf);
    uint32_t* old_indirect_array = (uint32_t*) old_indirect_buf;
    for(uint32_t i = 0; i < old_total_block - NDIRECT; i++) bm->free_block(old_indirect_array[i]);
    bm->free_block(indirect_block);
  }

  for(uint32_t i = 0; i < MIN(old_total_block,NDIRECT); i++) bm->free_block(inode->blocks[i]); //free old direct blocks;


  if(use_indirect){//reinit indirect blocks for later write
    inode->blocks[NDIRECT] = bm->alloc_block();
    bm->read_block(inode->blocks[NDIRECT], indirect_buf);
    indirect_array = (uint32_t*)indirect_buf;
  }

  uint32_t alloc_id;
  for(; write_block < total_block; total_write_len += write_len, remain_len -= write_len){
    write_len = MIN(remain_len, BLOCK_SIZE);

    if(write_block < NDIRECT){//direct write
      alloc_id = bm->alloc_block();
      inode->blocks[write_block] = alloc_id;
    }else{//indirect write
      alloc_id = bm->alloc_block();
      indirect_array[write_block - NDIRECT] = alloc_id;
      bm->write_block(inode->blocks[NDIRECT], indirect_buf);
    }

    memset(write_buf, 0, BLOCK_SIZE);
    memcpy(write_buf, buf, write_len);
    bm->write_block(alloc_id, write_buf);
    buf += write_len;// update pointer location
    write_block++;
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
