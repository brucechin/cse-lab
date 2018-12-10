// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server() : nacquire (0) {
    pthread_mutex_init(&map_mutex, NULL);
}

lock_protocol::status lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r) {
	printf("stat request from clt %d\n", clt);
	r = nacquire;
	return lock_protocol::OK;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
	// Your lab2 code goes here
  pthread_mutex_lock(&map_mutex);
  if (locks.find(lid) != locks.end()) {
    while (locks[lid].granted == true) {
        pthread_cond_wait(&locks[lid].wait, &map_mutex);
    }
  }
  locks[lid].granted = true;
  pthread_mutex_unlock(&map_mutex);
  return lock_protocol::OK;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
	// Your lab2 code goes here
  pthread_mutex_lock(&map_mutex);
  locks[lid].granted = false;
  pthread_mutex_unlock(&map_mutex);
  pthread_cond_signal(&locks[lid].wait);
  return lock_protocol::OK;
}
