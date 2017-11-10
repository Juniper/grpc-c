/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include "thread_pool.h"

/*
 * Thread function that will wait for work and execute callbacks from callback
 * queue when notified
 */
static void 
gc_thread_func (void *arg)
{
    grpc_c_thread_pool_t *pool;
    struct grpc_c_thread_t *gcthread;

    if (arg == NULL) {
	gpr_log(GPR_ERROR, "Uninitialized pool");
	return;
    }

    gcthread = (struct grpc_c_thread_t *) arg;
    pool = gcthread->gct_pool;

    gpr_mu_lock(&pool->gctp_lock);
    for(;;) {
	/*
	 * Wait for someone to notify if we don't have any callbacks yet
	 */
	if (pool->gctp_shutdown == 0 
	    && pool->gctp_callbacks_head.tqh_first == NULL) {
	    if (pool->gctp_wait_threads >= pool->gctp_max_threads) {
		break;
	    }

	    pool->gctp_wait_threads++;
	    gpr_cv_wait(&pool->gctp_cv, &pool->gctp_lock, 
			gpr_inf_future(GPR_CLOCK_REALTIME));
	    pool->gctp_wait_threads--;
	}

	/*
	 * Pop one callback out from queue and execute it
	 */
	if (pool->gctp_shutdown == 0 && 
	    pool->gctp_callbacks_head.tqh_first != NULL) {
	    struct grpc_c_thread_callback_t *cb = pool->gctp_callbacks_head.tqh_first;
	    TAILQ_REMOVE(&pool->gctp_callbacks_head, 
			 pool->gctp_callbacks_head.tqh_first, gctc_callbacks);
	    gpr_mu_unlock(&pool->gctp_lock);
	    cb->gctc_func(cb->gctc_arg);
	    gpr_free(cb);
	    gpr_mu_lock(&pool->gctp_lock);
	}

	if (pool->gctp_shutdown) break;
    }
    gpr_mu_unlock(&pool->gctp_lock);

    pool->gctp_nthreads--;

    /*
     * Notify shutdown condition variable when shutdown flag is set and we
     * don't have any threads left
     */
    if (pool->gctp_shutdown && pool->gctp_nthreads == 0) {
	gpr_cv_signal(&pool->gctp_shutdown_cv);
    }

    /*
     * Add this thread to list of dead threads so we can join them
     */
    TAILQ_INSERT_TAIL(&pool->gctp_dead_threads, gcthread, gct_threads);
}

/*
 * Create a structure to hold details about pool of threads. Takes the maximum
 * number of threads in pool as argument
 */
grpc_c_thread_pool_t * 
grpc_c_thread_pool_create (int n)
{
    grpc_c_thread_pool_t *pool = gpr_malloc(sizeof(grpc_c_thread_pool_t));
    if (pool == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for thread pool");
    } else {
	bzero(pool, sizeof(grpc_c_thread_pool_t));
	pool->gctp_max_threads = n;
	gpr_mu_init(&pool->gctp_lock);
	gpr_cv_init(&pool->gctp_cv);
	gpr_cv_init(&pool->gctp_shutdown_cv);
	TAILQ_INIT(&pool->gctp_callbacks_head);
	TAILQ_INIT(&pool->gctp_dead_threads);
    }

    return pool;
}

/*
 * Joins a threads and frees associated memory
 */
static void
gc_delete_threads (grpc_c_thread_pool_t *pool)
{
    struct grpc_c_thread_t *thread;

    while (pool->gctp_dead_threads.tqh_first != NULL) {
	gpr_thd_join(pool->gctp_dead_threads.tqh_first->gct_thread);
	thread = pool->gctp_dead_threads.tqh_first;
	TAILQ_REMOVE(&pool->gctp_dead_threads, 
		     pool->gctp_dead_threads.tqh_first, gct_threads);
	gpr_free(thread);
    }

}

/*
 * Adds a new job to the pool of threads. Creates one if necessary
 */
int 
grpc_c_thread_pool_add (grpc_c_thread_pool_t *pool, 
			grpc_c_callback_func_t *func, void *arg)
{
    struct grpc_c_thread_callback_t *callback;

    if (pool == NULL) {
	gpr_log(GPR_ERROR, "Uninitialized pool");
	return 1;
    }
    
    /*
     * Add callback function and arguments to the queue
     */
    gpr_mu_lock(&pool->gctp_lock);
    callback = malloc(sizeof(struct grpc_c_thread_callback_t));
    if (callback == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for thread callback");
	return 1;
    }

    callback->gctc_func = func;
    callback->gctc_arg = arg;

    TAILQ_INSERT_TAIL(&pool->gctp_callbacks_head, callback, gctc_callbacks);
    gpr_mu_unlock(&pool->gctp_lock);

    /*
     * Create a new thread if it is not already created or there are no free
     * threads. Else notify one of the waiting threads
     */
    if (pool->gctp_wait_threads == 0) {
	struct grpc_c_thread_t *gcthread = malloc(sizeof(struct grpc_c_thread_t));
	if (gcthread == NULL) {
	    gpr_log(GPR_ERROR, "Failed to allocate memory to create thread");
	    return 1;
	}
	pool->gctp_nthreads++;
	gcthread->gct_pool = pool;
	gpr_mu_init(&gcthread->gct_lock);
	gpr_thd_options toptions = gpr_thd_options_default();
	if (!gpr_thd_new(&gcthread->gct_thread, gc_thread_func, 
			 (void *)gcthread, &toptions)) {
	    gpr_log(GPR_ERROR, "Failed to create thread");
	    return 1;
	}
    } else {
	gpr_cv_signal(&pool->gctp_cv);
    }

    /*
     * Join all finished threads and delete them
     */
    gc_delete_threads(pool);

    return 0;
}

/*
 * Shutdown thread pool
 */
void
grpc_c_thread_pool_shutdown (grpc_c_thread_pool_t *pool)
{
    if (!pool) return;

    gpr_mu_lock(&pool->gctp_lock);
    pool->gctp_shutdown = 1;
    gpr_cv_broadcast(&pool->gctp_cv);

    while (pool->gctp_nthreads != 0) {
	gpr_cv_wait(&pool->gctp_shutdown_cv, &pool->gctp_lock, 
		    gpr_inf_future(GPR_CLOCK_REALTIME));
    }

    gc_delete_threads(pool);
    gpr_mu_unlock(&pool->gctp_lock);
}
