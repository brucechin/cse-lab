#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include <queue>


#include <map>
#include <set>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
 private:
  int nacquire;
	std::map<lock_protocol::lockid_t, std::string> locks_;
	typedef std::map<lock_protocol::lockid_t, std::string>::iterator locks_iterator_t;
	pthread_mutex_t	locks_mutex_;
	pthread_cond_t	locks_cond_;

	std::map<lock_protocol::lockid_t, std::queue<std::string> > waiting_list_;
	//must be called with locks_mutex_ locked
	bool get_lock(std::string cid, lock_protocol::lockid_t lid);
	//must be called with locks_mutex_ locked
	bool drop_lock(std::string cid, lock_protocol::lockid_t lid);

 public:
  lock_server_cache();
  ~lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
  lock_protocol::status reversed_rpc(unsigned int proc, std::string cid, lock_protocol::lockid_t lid);
};



#endif
