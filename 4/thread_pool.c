#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <errno.h>

struct thread_task 
{
    thread_task_f function;
    void* arg;
    void* result;
    bool is_finished;
    bool is_running;
    bool is_pushed;
    bool is_detached;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct thread_pool* pool;
    struct thread_task* next;
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
    struct thread_task* task_queue_head;
    struct thread_task* task_queue_tail;
    size_t task_count;
};

int thread_pool_new(int max_thread_count, struct thread_pool** pool) 
{
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS)
        return TPOOL_ERR_INVALID_ARGUMENT;
    
    *pool = (struct thread_pool*)calloc(1, sizeof(struct thread_pool));
    if (!*pool)
        return -1;
    
    (*pool)->threads = (pthread_t*)calloc(max_thread_count, sizeof(pthread_t));
    if (!(*pool)->threads) 
    {
        free(*pool);
        return -1;
    }
    
    (*pool)->max_count = max_thread_count;
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
    if (pool->running_count > 0 || pool->task_count > 0) 
    {
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_HAS_TASKS;
    }
    
    pool->is_end = true;
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

static void* worker_thread(void* arg) 
{
    struct thread_pool* pool = (struct thread_pool*)arg;
    while (true) 
    {
        pthread_mutex_lock(&pool->mutex);
        
        while (pool->task_count == 0 && !pool->is_end)
            pthread_cond_wait(&pool->cond, &pool->mutex);
        
        if (pool->is_end && pool->task_count == 0) 
        {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        struct thread_task* task = pool->task_queue_head;
        if (!task) 
        {
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }
        
        pool->task_queue_head = task->next;
        if (!pool->task_queue_head)
            pool->task_queue_tail = NULL;
        --pool->task_count;
        
        task->is_running = true;
        ++pool->running_count;
        pthread_mutex_unlock(&pool->mutex);
        
        void* result = task->function(task->arg);
        
        pthread_mutex_lock(&task->mutex);
        task->result = result;
        task->is_finished = true;
        task->is_running = false;
        pthread_cond_broadcast(&task->cond);
        bool is_detached = task->is_detached;
        pthread_mutex_unlock(&task->mutex);
        
        pthread_mutex_lock(&pool->mutex);
        --pool->running_count;
        if (is_detached) 
        {
            pthread_mutex_unlock(&pool->mutex);
            thread_task_delete(task);
            pthread_mutex_lock(&pool->mutex);
        }
        pthread_mutex_unlock(&pool->mutex);
    }
    return NULL;
}

int thread_pool_push_task(struct thread_pool* pool, struct thread_task* task) 
{
    assert(pool);
    assert(task);
    pthread_mutex_lock(&pool->mutex);
    if (pool->task_count >= TPOOL_MAX_TASKS) {
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }
    
    pthread_mutex_lock(&task->mutex);
    if (task->is_pushed) {
        pthread_mutex_unlock(&task->mutex);
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    
    task->next = NULL;
    task->is_pushed = true;
    task->pool = pool;
    task->is_finished = false;  
    task->is_running = false;
    pthread_mutex_unlock(&task->mutex);
    
    if (!pool->task_queue_tail)
    {
        pool->task_queue_head = task;
        pool->task_queue_tail = task;
    } 
    else 
    {
        pool->task_queue_tail->next = task;
        pool->task_queue_tail = task;
    }
    ++pool->task_count;
    
    if (!pool->is_end && pool->count < pool->max_count && 
        pool->running_count + pool->task_count > pool->count) 
    {
        if (pthread_create(&pool->threads[pool->count], NULL, worker_thread, pool) == 0)
            ++pool->count;
    }
    
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

int thread_task_new(struct thread_task** task, thread_task_f function, void* arg) 
{
    assert(task);
    *task = (struct thread_task*)calloc(1, sizeof(struct thread_task));
    if (!*task)
        return -1;
    
    (*task)->function = function;
    (*task)->arg = arg;
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

int thread_task_join(struct thread_task* task, void** result) 
{
    assert(task);
    pthread_mutex_lock(&task->mutex);
    if (!task->is_pushed) 
    {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    
    while (!task->is_finished)
        pthread_cond_wait(&task->cond, &task->mutex);
    
    if (result)
        *result = task->result;

    task->is_pushed = false;
    task->is_finished = false;
    task->is_running = false;
    
    pthread_mutex_unlock(&task->mutex);
    return 0;
}

int thread_task_delete(struct thread_task* task) 
{
    assert(task);
    pthread_mutex_lock(&task->mutex);
    if (!task->is_finished && task->is_pushed)
    {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_IN_POOL;
    }
    pthread_mutex_unlock(&task->mutex);
    
    pthread_mutex_destroy(&task->mutex);
    pthread_cond_destroy(&task->cond);
    free(task);
    return 0;
}

#if NEED_DETACH
int thread_task_detach(struct thread_task* task) 
{
    assert(task);
    pthread_mutex_lock(&task->mutex);
    if(!task->is_pushed)
    {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    if (task->is_finished) 
    {
        pthread_mutex_unlock(&task->mutex);
        thread_task_delete(task);
        return 0;
    }
    task->is_detached = true;
    pthread_mutex_unlock(&task->mutex);
    return 0;
}
#endif

#if NEED_TIMED_JOIN
int thread_task_timed_join(struct thread_task* task, double timeout, void** result) 
{
    assert(task);
    pthread_mutex_lock(&task->mutex);
    if (!task->is_pushed) 
    {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    if (!task->is_finished) 
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)timeout;
        ts.tv_nsec += (long)((timeout - (time_t)timeout) * 1e9);
        if (ts.tv_nsec >= 1000000000L) 
        {
            ++ts.tv_sec;
            ts.tv_nsec -= 1000000000L;
        }
        
        int res = pthread_cond_timedwait(&task->cond, &task->mutex, &ts);
        if (res == ETIMEDOUT) 
        {
            pthread_mutex_unlock(&task->mutex);
            return TPOOL_ERR_TIMEOUT;
        }
    }
    
    if (result)
        *result = task->result;

    task->is_pushed = false;
    task->is_finished = false;
    task->is_running = false;
    
    pthread_mutex_unlock(&task->mutex);
    return 0;
}
#endif