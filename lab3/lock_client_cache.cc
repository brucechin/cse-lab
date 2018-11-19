// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"
#include <pthread.h>

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{

  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);
  VERIFY(pthread_mutex_init(&locks_mutex_, NULL) == 0);
  default_owner_ = pthread_self();
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
	pthread_mutex_lock(&locks_mutex_);
	if (!lock(lid)){
		++locks_[lid].append_;
		while(true){
			if(!lock_should_retry(lid)){
				lock_cond_wait(lid, locks_mutex_);
			}
			if (lock(lid)){
				break;
			}
		}
		--locks_[lid].append_;
	}
	pthread_mutex_unlock(&locks_mutex_);
	return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&locks_mutex_);
	if (unlock(lid)){
		lock_cond_broadcast(lid);
	}else{
		ret = lock_protocol::RETRY;
	}
	pthread_mutex_unlock(&locks_mutex_);
	return ret;

}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, int &)
{
	int ret = rlock_protocol::OK;
	lock_status_t status;
	pthread_mutex_lock(&locks_mutex_);
	status = get_lock_status(lid);
//	tprintf("%s got revoke %lld lock is %x or has appending %d\n", id.c_str(), lid, status, locks_[lid].append_);
	if (status == FREE && !(locks_[lid].append_ > 0)){
//		tprintf("%s got revoke %lld lock is free \n", id.c_str(), lid);
		ret = rlock_protocol::OK_FREE;
		locks_[lid].status_ = NONE;
//		tprintf("%s released lock %lld upon revoke\n", id.c_str(), lid);
	}else{
//		tprintf("%s got revoke %lld lock is not free \n", id.c_str(), lid);
		locks_[lid].revoked_ = true;
		
	}
	pthread_mutex_unlock(&locks_mutex_);
	return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, int &)
{
	int ret = rlock_protocol::OK;
	pthread_mutex_lock(&locks_mutex_);
	lock_cond_broadcast(lid);
	locks_[lid].retry_ = true;
//	tprintf("%s got retry %lld\n", id.c_str(), lid);
	pthread_mutex_unlock(&locks_mutex_);
	return ret;
}


lock_client_cache::lock_status_t 
lock_client_cache::get_lock_status(lock_protocol::lockid_t lid){
	if (locks_.find(lid) == locks_.end()){
		locks_[lid].status_ = NONE;
		locks_[lid].owner_ = default_owner_;
		VERIFY(pthread_cond_init(&locks_[lid].cond_, NULL) == 0);
		locks_[lid].append_ = 0;
		locks_[lid].revoked_ = false;
		locks_[lid].retry_ = false;
	}
	return locks_[lid].status_;
}




bool lock_client_cache::lock_should_retry(lock_protocol::lockid_t lid){
	bool ret = locks_[lid].retry_;
	locks_[lid].retry_ = false;
	return ret;
}



void lock_client_cache::forget_lock(lock_protocol::lockid_t lid){
	VERIFY(locks_[lid].append_ == 0);
	locks_[lid].status_ = NONE;
	locks_[lid].owner_ = default_owner_;
//	tprintf("%s reset lock %lld before %d now 0 by thread %u\n",id.c_str(), lid, locks_[lid].append_, (unsigned int)pthread_self());
	locks_[lid].append_ = 0;
	locks_[lid].revoked_ = false;
	locks_[lid].retry_ = false;
	
}

bool lock_client_cache::lock(lock_protocol::lockid_t lid){
//	tprintf("%s try get lock %lld locally\n", id.c_str(), lid);
	bool ret = false;
	bool missed = false;
	lock_status_t status = get_lock_status(lid);
	switch(status){
	case FREE:
		locks_[lid].status_ = LOCKED;
    locks_[lid].owner_ = pthread_self();
		ret = true;
		break;
	case LOCKED:
		break;
	case ACQUIRING:
		break;
	case RELEASING:
		break;
	case NONE:
		missed = true;
		locks_[lid].status_ = ACQUIRING;
		locks_[lid].owner_ = pthread_self();
		break;
	}

	if (missed){
		lock_protocol::status rst;
		int r;
//		int i = 0;
		pthread_mutex_unlock(&locks_mutex_);
		do{
//			tprintf("%s try get lock %lld for %d time\n", id.c_str(), lid, ++i);
			rst = cl->call(lock_protocol::acquire, lid, id, r);
		}while(!(rst == lock_protocol::OK || rst == lock_protocol::RETRY));
		pthread_mutex_lock(&locks_mutex_);
		if (rst == lock_protocol::OK){
			locks_[lid].status_ = LOCKED;
			locks_[lid].owner_ = pthread_self();
			ret = true;
		}else if(rst == lock_protocol::RETRY){
			locks_[lid].status_ = NONE;
			locks_[lid].owner_ = pthread_self();
			ret = false;
		}
//		tprintf("%s lock %lld %s r = %x\n", id.c_str(), lid, ret?"success":"fail", r);
	}
	return ret;
}

bool lock_client_cache::unlock(lock_protocol::lockid_t lid){
	bool ret = false;
	bool to_revoke = false;
	lock_status_t status = get_lock_status(lid);
	switch(status){
	case LOCKED:
    if (pthread_equal(locks_[lid].owner_, pthread_self())){
			if (locks_[lid].revoked_ && !(locks_[lid].append_ > 0)){
				locks_[lid].status_ = RELEASING;
				to_revoke = true;
			}else{
				locks_[lid].status_ = FREE;
				ret = true;
			}
		}
		break;
	case RELEASING:
		break;
	case FREE:
	case ACQUIRING:
	case NONE:
		//should not be here
		VERIFY(0);
		break;
	}

	if (to_revoke){
		lock_protocol::status rst;
		int r;
//		int i = 0;
		pthread_mutex_unlock(&locks_mutex_);
		do{
//			tprintf("%s try release lock %lld for %d time\n", id.c_str(), lid, ++i);
			rst = cl->call(lock_protocol::release, lid, id, r);
		}while(rst != lock_protocol::OK);
		pthread_mutex_lock(&locks_mutex_);
		VERIFY(rst == lock_protocol::OK);
		forget_lock(lid);
		ret = true;
//		tprintf("%s unlock %lld %s \n", id.c_str(), lid, ret?"success":"fail");
	}
//	tprintf("%s unlock %lld", id.c_str(), lid);
//	printf(" %s\n", ret?"success":"fail");
	return ret;
}


