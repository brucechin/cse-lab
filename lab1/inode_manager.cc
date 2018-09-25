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
   * your lab1 code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
	char buf[BLOCK_SIZE];
	char indirect_buf[BLOCK_SIZE];
	
	// get inode information with inum from disk
	inode_t* inode = get_inode(inum);	
	if (inode == NULL) {
		cout << "Error 04: Read file with not exist inode number." << endl;
		exit(0);
	}
	*size = inode->size;
	char* data_out = (char*)malloc(inode->size);	// free by caller
	*buf_out = data_out;	// bind buf_out with new malloc area
	memset(data_out, 0, inode->size);

	// judge whether need indirect block
	bool need_indirect = (inode->size > (NDIRECT * BLOCK_SIZE));
	uint32_t* indirect_array;
	if (need_indirect) {
		bm->read_block(inode->blocks[NDIRECT], indirect_buf);
		indirect_array = (uint32_t*)indirect_buf;
	}

	// read data to data_out
	uint32_t total_len = 0;	// total read length
	uint32_t remain_len = inode->size; // reamin length to read
	uint32_t total_block = inode->size / BLOCK_SIZE + 1;	// total blocks to read
	if (inode->size % BLOCK_SIZE == 0) total_block -= 1;
	uint32_t read_block = 0;	// now read block number
	uint32_t read_len = 0; // read length once time

	for(total_len = 0; remain_len > 0; remain_len -= read_len, total_len += read_len) {
		read_len = MIN(BLOCK_SIZE, remain_len);
		// for indirect case
		if (read_block >= NDIRECT) {
			bm->read_block(indirect_array[read_block-NDIRECT], buf);
			memcpy(data_out, buf, read_len);
			read_block++;
			data_out += read_len;
		}
		// for direct case
		else {
			bm->read_block(inode->blocks[read_block], buf);
			memcpy(data_out, buf, read_len);
			read_block++;
			data_out += read_len;
		}
	}
	// change time
	inode->atime = time(NULL);
	inode->ctime = time(NULL);
	put_inode(inum, inode);
	free(inode);
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your lab1 code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
		
	inode_t* inode = get_inode(inum);
	bool old_need_indirect = ((inode->size/BLOCK_SIZE) > NDIRECT);
	uint32_t old_size = inode->size;
	uint32_t old_total_blocks = old_size / BLOCK_SIZE + 1;
	if (old_size % BLOCK_SIZE == 0) old_total_blocks -= 1;

	// judge whether larger than MAXFILE
	inode->size = MIN(MAXFILE *BLOCK_SIZE, (unsigned)size);
	
	char indirect_buffer[BLOCK_SIZE];
	uint32_t* indirect_array;
	uint32_t total_blocks = inode->size / BLOCK_SIZE + 1;
	if (inode->size % BLOCK_SIZE == 0) total_blocks -= 1;
	bool need_indirect = (total_blocks > NDIRECT); 
	uint32_t remain_len = inode->size;
	uint32_t total_len = 0;	// total write length
	uint32_t write_len = 0;	// for once write
	uint32_t write_block = 0; // now write block number 
	uint32_t write_id = 0; // write block id
	char write_buffer[BLOCK_SIZE];


	// free old indirect block
	if (old_need_indirect) { 
		uint32_t indirect_block = inode->blocks[NDIRECT];
		char old_buffer[BLOCK_SIZE];
		bm->read_block(indirect_block, old_buffer);
		uint32_t* old_indirect_array = (uint32_t*)old_buffer;
		for (uint32_t i = 0; i < old_total_blocks - NDIRECT; i++)
			bm->free_block(old_indirect_array[i]);	// free indirected's data block
		bm->free_block(indirect_block);						// free indirected's index block
	}
	// free old direct block
	for (uint32_t i = 0; i < old_total_blocks && i < NDIRECT; i++)
		bm->free_block(inode->blocks[i]);
	
	// init indirect block
	if (need_indirect) {
		inode->blocks[NDIRECT] = bm->alloc_block();
		bm->read_block(inode->blocks[NDIRECT], indirect_buffer);
		indirect_array = (uint32_t*)indirect_buffer;
	}

	//begin write
	for (total_len = 0; write_block < total_blocks; total_len += write_len, remain_len -= write_len) {
		write_len = MIN(remain_len, BLOCK_SIZE);
		// for direct case
		if (write_block < NDIRECT) {
			write_id = bm->alloc_block();
			inode->blocks[write_block] = write_id;
		}
		// for indirect case
		else {
			write_id = bm->alloc_block();
			indirect_array[write_block - NDIRECT] = write_id;
			bm->write_block(inode->blocks[NDIRECT], indirect_buffer);	// update indirected index block
		}
		memset(write_buffer, 0, BLOCK_SIZE);
		memcpy(write_buffer, buf, write_len);
		bm->write_block(write_id, write_buffer);
		buf += write_len;
		write_block++;
	}
	// change time
	inode->atime = inode->ctime = inode->mtime = time(NULL);
	put_inode(inum, inode);
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
