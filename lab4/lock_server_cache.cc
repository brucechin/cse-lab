// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"

lock_server_cache::lock_server_cache():
  nacquire (0)
{
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
}

int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&mutex);
  if (lock[lid].empty()){
    lock[lid].push(id);
    wait_set[lid].insert(id);
    r = lock_protocol::OK;
  }else if (find(wait_set[lid].begin(), wait_set[lid].end(), id) == wait_set[lid].end()){
    ret = lock_protocol::RETRY;

    lock[lid].push(id);
    wait_set[lid].insert(id);
    if (lock[lid].size() == 2){
      std::string t = lock[lid].front();
      handle h(t);
      rpcc *cl = h.safebind();
      int tr = 9;
      int revoke_ret;
      pthread_mutex_unlock(&mutex);
      revoke_ret = cl->call(rlock_protocol::revoke, lid, tr);
      pthread_mutex_lock(&mutex);
      if (revoke_ret == 1) {
        wait_set[lid].erase(lock[lid].front());
        lock[lid].pop();
        if (lock[lid].size() > 1) ret = 2;
        else ret = 0;
        pthread_mutex_unlock(&mutex);
        cl->call(rlock_protocol::revoke, lid, tr);
        pthread_mutex_lock(&mutex);
      }
      pthread_mutex_unlock(&mutex);
      return ret;
    }
  }else{
    r = lock_protocol::OK;
  }
  pthread_mutex_unlock(&mutex);
  return ret;
}


int lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&mutex);
  if (!lock[lid].empty() && lock[lid].front() == id){
    lock[lid].pop();
    wait_set[lid].erase(id);
  }
  else{
    pthread_mutex_unlock(&mutex);
    return ret;
  }
  if (!lock[lid].empty()){
    std::string t = lock[lid].front();
    int state = lock[lid].size() > 1;
    handle h(t);
    rpcc *cl = h.safebind();
    int tr = 9;
    pthread_mutex_unlock(&mutex);
    cl->call(rlock_protocol::retry, lid, state, tr);
    return ret;
  }
  pthread_mutex_unlock(&mutex);
  return ret;
}


lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
//	tprintf("stat request\n");
	r = nacquire;
	return lock_protocol::OK;
}

