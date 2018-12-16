#include "namenode.h"
#include "extent_client.h"
#include "lock_client_cache.h"
#include <sys/stat.h>
#include <unistd.h>
#include "threader.h"

using namespace std;

void NameNode::init(const string &extent_dst, const string &lock_dst) {
  ec = new extent_client(extent_dst);
  lc = new lock_client_cache(lock_dst);
  yfs = new yfs_client(extent_dst, lock_dst);

  /* Add your init logic here */
  counter = 0;
  NewThread(this, &NameNode::CountBeat);
}
void NameNode::CountBeat(){
  while(true){
    this->counter++;
    sleep(1);
  }
}
list<NameNode::LocatedBlock> NameNode::GetBlockLocations(yfs_client::inum ino) {

  list<NameNode::LocatedBlock> list_block;
  list<blockid_t> block_ids;
  long long size = 0;
  ec->get_block_ids(ino, block_ids);
  extent_protocol::attr attr;
  ec->getattr(ino, attr);
  int cnt = 0;
  for(blockid_t blockid : block_ids){
    cnt++;
    LocatedBlock lb(blockid, size, cnt < block_ids.size() ? BLOCK_SIZE : (attr.size - size), GetDatanodes());
    list_block.push_back(lb);
    size += BLOCK_SIZE;
  }
  return list_block;
}

bool NameNode::Complete(yfs_client::inum ino, uint32_t new_size) {

  bool rst = !ec->complete(ino, new_size);
  if(rst) lc->release(ino);
  return rst;
}

NameNode::LocatedBlock NameNode::AppendBlock(yfs_client::inum ino) {
  blockid_t bid;
  extent_protocol::attr attr;
  ec->getattr(ino, attr);
  ec->append_block(ino, bid);
  modified_blocks.insert(bid);
  int size;
  LocatedBlock rst(bid, attr.size, (attr.size % BLOCK_SIZE) ? attr.size % BLOCK_SIZE : BLOCK_SIZE, GetDatanodes());
  return rst;

}

bool NameNode::Rename(yfs_client::inum src_dir_ino, string src_name, yfs_client::inum dst_dir_ino, string dst_name) {
  //printf("rename : %s to %s\n",src_name,dst_name);
  //fflush(stdout);
  string src_buf, dst_buf;
  ec->get(src_dir_ino, src_buf);
  ec->get(dst_dir_ino, dst_buf);
  bool flag = false;
  int pos = 0, dst_ino;
  const char* p;
  while(src_buf.size() > pos){
    p = src_buf.c_str() + pos;
    if(strcmp(src_name.c_str(), p) == 0){// find the src ino
      flag = true; 
      dst_ino = *(uint32_t *)(p + strlen(p) + 1); // assign src ino to dst ino
      src_buf.erase(pos, strlen(p) + sizeof(uint) + 1);//delete src ino reference
      break;
    }
    pos += strlen(p) + sizeof(uint) + 1;
  }

  if(flag){
    if(src_dir_ino == dst_dir_ino) dst_buf = src_buf; // when in the same diretory
    
    dst_buf += dst_name;
    dst_buf.resize(dst_buf.size() + 5, 0);
    *(uint32_t *)(dst_buf.c_str() + dst_buf.size() -4) = dst_ino;
    
    ec->put(src_dir_ino, src_buf);
    ec->put(dst_dir_ino, dst_buf);
  }
  return flag;
}

bool NameNode::Mkdir(yfs_client::inum parent, string name, mode_t mode, yfs_client::inum &ino_out) {
  //printf("mkdir : %s\n",name);
  fflush(stdout);
  bool rst = !yfs->mkdir(parent, name.c_str(), mode, ino_out);
  return res;
}

bool NameNode::Create(yfs_client::inum parent, string name, mode_t mode, yfs_client::inum &ino_out) {
  //printf("create : %s\n",name);
  fflush(stdout);
  bool rst = !yfs->create(parent, name.c_str(), mode, ino_out);
  if(rst) lc->acquire(ino_out);
  return rst;
}

bool NameNode::Isfile(yfs_client::inum ino) {
  //printf("isfile? \n");
  fflush(stdout);
  extent_protocol::attr tmp;
  if(ec->getattr(ino, tmp) != extent_protocol::OK){
    printf("error getting attr\n");
    return false;
  }

  if(tmp.type == extent_protocol::T_FILE) return true;
  else if (tmp.type == extent_protocol::T_SYMLK) {
      return false;
  } 
  return false;
}

bool NameNode::Isdir(yfs_client::inum ino) {
  //printf("isdir? \n");
  //fflush(stdout);
  extent_protocol::attr tmp;
  if(ec->getattr(ino, tmp) != extent_protocol::OK){
    printf("error getting attr\n");
    return false;
  }

  if(tmp.type == extent_protocol::T_DIR) return true;

  return false;
}

bool NameNode::Getfile(yfs_client::inum ino, yfs_client::fileinfo &info) {
  extent_protocol::attr tmp;
  if (ec->getattr(ino, tmp) != extent_protocol::OK) return false;
  info.atime = tmp.atime;
  info.mtime = tmp.mtime;
  info.ctime = tmp.ctime;
  info.size = tmp.size;

  return true;
}

bool NameNode::Getdir(yfs_client::inum ino, yfs_client::dirinfo &info) {
  extent_protocol::attr tmp;
  if (ec->getattr(ino, tmp) != extent_protocol::OK) return false;
  info.atime = tmp.atime;
  info.mtime = tmp.mtime;
  info.ctime = tmp.ctime;

  return true;
  
}

bool NameNode::Readdir(yfs_client::inum ino, std::list<yfs_client::dirent> &dir) {
  //printf("readdir\n");
  //fflush(stdout);
  string buf;
  ec->get(ino, buf);
  int pos = 0;
  while(pos < buf.size()){//read all info of the directory stored in this inode
      struct yfs_client::dirent tmp;
      tmp.name = string(buf.c_str()+pos);
      tmp.inum = *(uint32_t *)(buf.c_str() + pos + tmp.name.size() + 1);
      dir.push_back(tmp);
      pos += tmp.name.size() + 1 + sizeof(uint32_t);
  }
  return true;
}

bool NameNode::Unlink(yfs_client::inum parent, string name, yfs_client::inum ino) {
  //printf("unlink %s\n",name);
  string buf;
  ec->get(parent, buf);
  int pos = 0;
  while(pos < buf.size()){
    const char* p = buf.c_str() + pos;
    if(strcmp(p, name.c_str()) == 0){
      uint32_t ino = *(uint32_t *)(p + strlen(p) + 1);
      buf.erase(pos, strlen(p) + 1 + sizeof(uint32_t));
      ec->put(parent, buf);
      ec->remove(ino);
      return true;
    }
    pos += strlen(p) + 1 + sizeof(uint32_t);
  }
  return false;
}

void NameNode::DatanodeHeartbeat(DatanodeIDProto id) {
  int m = 0;
  for (auto i : datanodes){
    m = max(m, i.second);
  }
  datanodes[id] = this->counter;
}

void NameNode::RegisterDatanode(DatanodeIDProto id) {
  if (this->counter > 5){
    for(auto b : modified_blocks){
      ReplicateBlock(b, master_datanode, id);
    }
  }
  datanodes.insert(make_pair(id, this->counter));
}

list<DatanodeIDProto> NameNode::GetDatanodes() {
  list<DatanodeIDProto> l;
  for (auto i : datanodes){
    if (i.second >= this->counter - 3){
      l.push_back(i.first);
    }
  }
  return l;
}
