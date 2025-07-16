#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

struct thread_task 
{
	thread_task_f function;
	void* arg;
	pthread_mutex_t mutex;
    pthread_cond_t cond;
    void* result;
    bool is_finished;
    bool is_running;
    bool is_pushed;
    bool is_detached;
    struct thread_pool* pool;
};

struct thread_pool 
{
	pthread_t* threads;
	size_t count;
	size_t max_count;
	size_t running_count;
	volatile bool is_end;
 	pthread_mutex_t mutex;
    pthread_cond_t cond;
	struct task_node* task_queue_head;
    struct task_node* task_queue_tail;
    size_t task_count;
};

struct task_node 
{
    struct thread_task* task;
    struct task_node* next;
};


int thread_pool_new(int max_thread_count, struct thread_pool** pool)
{
	if(max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS)
		return TPOOL_ERR_INVALID_ARGUMENT;
	*pool = (struct thread_pool*) calloc(1, sizeof(struct thread_pool));
	if(!pool)
		return -1;
	(*pool)->threads = (pthread_t*) calloc(max_thread_count, sizeof(pthread_t));
    if (!(*pool)->threads) 
	{
        free(*pool);
        return -1;
    }
	(*pool)->max_count = max_thread_count;
	(*pool)->is_end = false;
	pthread_mutex_init(&(*pool)->mutex, NULL);
	pthread_cond_init(&(*pool)->cond, NULL);
	return 0;
}

int thread_pool_thread_count(const struct thread_pool* pool)
{
	assert(pool);
	return pool->count;
}

int thread_pool_delete(struct thread_pool* pool)
{
	assert(pool);
	pthread_mutex_lock(&pool->mutex);
	if(pool->count > 0 || pool->running_count > 0)
	{
		pthread_mutex_unlock(&pool->mutex);
		return TPOOL_ERR_HAS_TASKS;
	}
	pool->is_end = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    
    for (size_t i = 0; i < pool->count; ++i) 
        pthread_join(pool->threads[i], NULL);
    
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    
    free(pool->threads);
    free(pool);
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)pool;
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

int thread_task_new(struct thread_task** task, thread_task_f function, void* arg)
{
    
    *task = (struct thread_task*) calloc(1, sizeof(struct thread_task));
    if (!(*task)) 
        return -1;
    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->is_finished = false;
    (*task)->is_running = false;
	(*task)->is_pushed = false;
    (*task)->is_detached = false;
    pthread_mutex_init(&(*task)->mutex, NULL);
    pthread_cond_init(&(*task)->cond, NULL);
    return 0;
}

bool thread_task_is_finished(const struct thread_task* task) 
{
	assert(task);
    return task->is_finished;
}

bool thread_task_is_running(const struct thread_task* task) 
{
	assert(task);
    return task->is_running;
}


int
thread_task_join(struct thread_task *task, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#if NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)timeout;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#if NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
