// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include <set>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"
#include  <pthread.h>


// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 6.
class lock_release_user {
public:
	virtual void dorelease(lock_protocol::lockid_t) = 0;
	virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
public:
	
	enum lock_status{NONE, FREE, LOCKED, ACQUIRING, RELEASING};
	typedef int lock_status_t;
private:
	class lock_release_user *lu;
	int rlock_port;
	std::string hostname;
	std::string id;
	pthread_t default_owner_;
	
	typedef struct lock_info{
		lock_status_t status_;
		pthread_t	owner_;
		pthread_cond_t	cond_;
		int append_;
		bool revoked_;
		bool retry_;
	}lock_info_t;

	std::map<lock_protocol::lockid_t, lock_info_t> locks_;
	pthread_mutex_t locks_mutex_;
	
	//functions below must be called with locks_mutex_ locked 
	lock_status_t get_lock_status(lock_protocol::lockid_t lid);
		//fuctions below must be called after the lock is touched by get_lock_status

	bool lock_should_retry(lock_protocol::lockid_t lid);

	void forget_lock(lock_protocol::lockid_t lid);
	void set_lock_retry(lock_protocol::lockid_t lid);
	#define lock_cond_wait(lid, mutex) pthread_cond_wait(&locks_[(lid)].cond_, &(mutex))  
	#define lock_cond_broadcast(lid) pthread_cond_broadcast(&locks_[(lid)].cond_)
	bool lock(lock_protocol::lockid_t lid);
	bool unlock(lock_protocol::lockid_t lid);
	//functions above must be called with locks_mutex_ locked
public:
  static int last_port;
	lock_client_cache(std::string xdst, class lock_release_user *l = 0);
	virtual ~lock_client_cache() {};
	lock_protocol::status acquire(lock_protocol::lockid_t);
	lock_protocol::status release(lock_protocol::lockid_t);
	rlock_protocol::status revoke_handler(lock_protocol::lockid_t, int &);
	rlock_protocol::status retry_handler(lock_protocol::lockid_t, int &);
};



#endif
