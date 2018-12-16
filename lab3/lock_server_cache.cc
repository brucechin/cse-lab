// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"

lock_server_cache::lock_server_cache()
{
	pthread_mutex_init(&locks_mutex_, NULL) == 0;
	pthread_cond_init(&locks_cond_, NULL) == 0;
}



bool lock_server_cache::get_lock(std::string cid, lock_protocol::lockid_t lid){
	bool ret = true;
	locks_iterator_t it = locks_.find(lid);
	if (it == locks_.end() 
		&& 
		(waiting_list_[lid].empty() || cid.compare(waiting_list_[lid].front()) == 0)){
		locks_[lid] = cid;
		ret = true;
	}else{
		ret = false;
	}
	return ret;
}

bool lock_server_cache::drop_lock(std::string cid, lock_protocol::lockid_t lid){
	bool ret = true;
	locks_iterator_t it = locks_.find(lid);
	if (it == locks_.end()){
		ret = false;
	}else{
		if (cid.compare(it->second) == 0){
			locks_.erase(it);
			ret = true;
		}else{
			ret = false;
		}
	}
	return ret;
}
int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&locks_mutex_);
	if (!get_lock(id, lid)){
		if (waiting_list_[lid].empty()){
//			tprintf("send revoke to %s for lock %lld\n", locks_[lid].c_str(), lid);
			rlock_protocol::status rst;
			do{
				rst = reversed_rpc(rlock_protocol::revoke, locks_[lid], lid);
			}while(!(rst == rlock_protocol::OK || rst == rlock_protocol::OK_FREE));
//			tprintf("revoke %s lock %lld\n", locks_[lid].c_str(), lid);
			if (rst == rlock_protocol::OK_FREE){
				drop_lock(locks_[lid], lid);
				get_lock(id, lid);
//				tprintf("%s got_lock %lld\n", id.c_str(), lid);
	
			}else{
				rst == rlock_protocol::OK;
				ret = lock_protocol::RETRY;
				r = lock_protocol_failure_code::LOCK_BUSY;
//				tprintf("lock owner is %s\n", locks_[lid].c_str());
				waiting_list_[lid].push(id);
						
//				tprintf("%s fail_to_got_lock %lld\n", id.c_str(), lid);

			}
		}else{
//			tprintf("%s is pushed into waiting list\n", id.c_str());
//			tprintf("firt candidate is %s\n", waiting_list_[lid].front().c_str());
			waiting_list_[lid].push(id);
			ret = lock_protocol::RETRY;
			r = lock_protocol_failure_code::NOT_NEXT;
		}
	}else{
		if (!waiting_list_[lid].empty()){
			waiting_list_[lid].front().compare(id) == 0;
			waiting_list_[lid].pop();
//			tprintf("%s popped from waiting list of lock %lld\n", id.c_str(), lid);
			//need be revoked if queue is already not empty
//			tprintf("send revoke to %s for lock %lld\n", locks_[lid].c_str(), lid);
			rlock_protocol::status rst;
			do{
				rst = reversed_rpc(rlock_protocol::revoke, locks_[lid], lid);
			}while(!(rst == rlock_protocol::OK || rst == rlock_protocol::OK_FREE));

		}
//		tprintf("%s got_lock %lld\n", id.c_str(), lid);
	}
	pthread_mutex_unlock(&locks_mutex_);
	return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&locks_mutex_);
	if (!drop_lock(id, lid)){
		ret = lock_protocol::RETRY; 

//		tprintf("%s fail to released %lld\n", id.c_str(), lid);
	}else{
//		tprintf("%s released %lld\n", id.c_str(), lid);
		std::queue<std::string>* queue = &waiting_list_[lid];
		if(!queue->empty()){
//			tprintf("send retry to %s for lock %lld\n", queue->front().c_str(), lid);
			rlock_protocol::status rst;
			do{
				rst = reversed_rpc(rlock_protocol::retry, queue->front(), lid);
			}while(rst != rlock_protocol::OK);
			if(queue->size() > 1){
//				tprintf("send revoke to %s for lock %lld\n", queue->front().c_str(), lid);
				do{
					rst = reversed_rpc(rlock_protocol::revoke, queue->front(), lid);
				}while(rst != rlock_protocol::OK);
			}
		}

	}
	pthread_mutex_unlock(&locks_mutex_);
	return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
//	tprintf("stat request\n");
	r = nacquire;
	return lock_protocol::OK;
}

lock_protocol::status lock_server_cache::reversed_rpc(unsigned int proc, std::string cid, lock_protocol::lockid_t lid){
//	tprintf("cid = %s\n", cid.c_str());
	handle h(cid);
	lock_protocol::status ret = lock_protocol::RPCERR;
	rpcc* rrpc = h.safebind();
	if (rrpc != NULL){
		int r;
		ret = rrpc->call(proc, lid, r);
	}
	return ret;
}