#ifndef inode_h
#define inode_h

#include <stdint.h>
#include <pthread.h>
#include "extent_protocol.h" // TODO: delete it

#define DISK_SIZE  1024*1024*16
#define BLOCK_SIZE 512
#define BLOCK_NUM  (DISK_SIZE/BLOCK_SIZE)

typedef uint32_t blockid_t;

// disk layer -----------------------------------------

class disk {
 private:
  unsigned char blocks[BLOCK_NUM][BLOCK_SIZE];

 public:
  disk();
  void read_block(uint32_t id, char *buf);
  void write_block(uint32_t id, const char *buf);
};

// block layer -----------------------------------------

typedef struct superblock {
  uint32_t size;
  uint32_t nblocks;
  uint32_t ninodes;
} superblock_t;

class block_manager {
 private:
  disk *d;
  std::map <uint32_t, int> using_blocks;
  pthread_mutex_t bitmap_mutex; 
 public:
  block_manager();
  struct superblock sb;

  uint32_t alloc_block();
  void free_block(uint32_t id);
  void read_block(uint32_t id, char *buf);
  void write_block(uint32_t id, const char *buf);
};

// inode layer -----------------------------------------

// begin from 1
#define INODE_NUM  1024

// Inodes per block.
#define IPB           1
//(BLOCK_SIZE / sizeof(struct inode))
// IPB=1 is important for thread-safe

// reserved blocks
#define RESERVED_BLOCK(ninodes, nblocks)     (2 + ((nblocks) + BPB - 1)/BPB + ((ninodes) + IPB - 1)/IPB)

// Block containing inode i
#define IBLOCK(i, nblocks)     (2 + ((nblocks) + BPB - 1)/BPB + ((i)-1)/IPB)

// Bitmap bits per block
#define BPB           (BLOCK_SIZE*8)

// Block containing bit for block b
#define BBLOCK(b) ((b)/BPB + 2)

#define NDIRECT 32
#define NINDIRECT (BLOCK_SIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)


typedef struct inode {
  short type;
  unsigned int size;
  unsigned int atime;
  unsigned int mtime;
  unsigned int ctime;
  blockid_t blocks[NDIRECT+1];   // Data block addresses
} inode_t;

class inode_manager {
 private:
  block_manager *bm;
  std::map <uint32_t, int> using_ino;
  struct inode* get_inode(uint32_t inum);
  pthread_mutex_t inodes_mutex; 
  void put_inode(uint32_t inum, struct inode *ino);

 public:
  inode_manager();
  uint32_t alloc_inode(uint32_t type);
  void free_inode(uint32_t inum);
  void read_file(uint32_t inum, char **buf, int *size);
  void write_file(uint32_t inum, const char *buf, int size);
  void remove_file(uint32_t inum);
  void getattr(uint32_t inum, extent_protocol::attr &a);
  void append_block(uint32_t inum, blockid_t &bid);
  void get_block_ids(uint32_t inum, std::list<blockid_t> &block_ids);
  void read_block(blockid_t bid, char block[BLOCK_SIZE]);
  void write_block(blockid_t bid, const char block[BLOCK_SIZE]);
  void complete(uint32_t inum, uint32_t size);
};

#endif

