#include "corobus.h"

#include "libcoro.h"
#include "rlist.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct data_vector 
{
	unsigned* data;
	size_t size;
	size_t capacity;
};

/**
 * simple function for vector (there's non-like stl content)
 */

static void data_vector_init(struct data_vector* vector, size_t capacity)
{
	assert(vector != NULL);
    vector->capacity = capacity;
    vector->size = 0;
    vector->data = (unsigned*) malloc(capacity * sizeof(unsigned));
}

static void data_vector_destroy(struct data_vector* vector)
{
	assert(vector != NULL);
    free(vector->data);
}

static void data_vector_push_back(struct data_vector* vector, unsigned data)
{
    assert(vector->size < vector->capacity);
    vector->data[vector->size++] = data;
}

static unsigned data_vector_pop_front(struct data_vector* vector)
{
    assert(vector->size > 0);
    unsigned data = vector->data[0];
    memmove(vector->data, vector->data + 1, --vector->size * sizeof(unsigned));
    return data;
}

/**
 * One coroutine waiting to be woken up in a list of other
 * suspended coros.
 */
struct wakeup_entry 
{
	struct rlist base;
	struct coro* coro;
};

/** A queue of suspended coros waiting to be woken up. */
struct wakeup_queue 
{
	struct rlist coros;
};

#if 0 /* Uncomment this if want to use */

/** Suspend the current coroutine until it is woken up. */
static void
wakeup_queue_suspend_this(struct wakeup_queue *queue)
{
	struct wakeup_entry entry;
	entry.coro = coro_this();
	rlist_add_tail_entry(&queue->coros, &entry, base);
	coro_suspend();
	rlist_del_entry(&entry, base);
}

#endif

static void wakeup_queue_wakeup_first(struct wakeup_queue* queue)
{
	assert(queue != NULL);
    if (!rlist_empty(&queue->coros)) 
	{
        struct wakeup_entry* entry = rlist_first_entry(&queue->coros, struct wakeup_entry, base);
        coro_wakeup(entry->coro);
    }
}

struct coro_bus_channel 
{
	/** Channel max capacity.*/
	size_t size_limit;

	/** Coroutines waiting until the channel is not full. */
	struct wakeup_queue send_queue;

	/** Coroutines waiting until the channel is not empty. */
	struct wakeup_queue recv_queue;

	/** Message queue. */
	struct data_vector data;
};

struct coro_bus 
{
	struct coro_bus_channel **channels;
	int channel_count;
};

static enum coro_bus_error_code global_error = CORO_BUS_ERR_NONE;

enum coro_bus_error_code coro_bus_errno(void)
{
	return global_error;
}

void coro_bus_errno_set(enum coro_bus_error_code err)
{
	global_error = err;
}

struct coro_bus* coro_bus_new(void)
{
	struct coro_bus* bus = (struct coro_bus*) malloc(sizeof(*bus));
    bus->channel_count = 0;
    bus->channels = NULL;
	coro_bus_errno_set(CORO_BUS_ERR_NONE); 
	return bus;
}

void coro_bus_delete(struct coro_bus* bus)
{
	assert(bus);
	for (int i = 0; i < bus->channel_count; ++i) 
	{
        if (bus->channels[i]) 
		{
            data_vector_destroy(&bus->channels[i]->data);
            free(bus->channels[i]);
        }
    }
    free(bus->channels);
    free(bus);
    coro_bus_errno_set(CORO_BUS_ERR_NONE);
}

static void init(struct coro_bus* bus, size_t size_limit, int i) 
{
	assert(bus);
	struct coro_bus_channel* ch = (struct coro_bus_channel*)malloc(sizeof(*ch));
	assert(ch);
	data_vector_init(&ch->data, size_limit);
	ch->size_limit = size_limit;
	rlist_create(&ch->send_queue.coros);
	rlist_create(&ch->recv_queue.coros);
	bus->channels[i] = ch;
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
}

int coro_bus_channel_open(struct coro_bus* bus, size_t size_limit)
{
	assert(bus);
    for (int i = 0; i < bus->channel_count; ++i) 
	{
        if (!bus->channels[i]) 
		{
			init(bus, size_limit, i);
            return i;
        }
    }
    bus->channels = realloc(bus->channels, (++bus->channel_count) * sizeof(void*));
    int index = bus->channel_count - 1;
	init(bus, size_limit, index);
    return index;
}

void coro_bus_channel_close(struct coro_bus* bus, int channel)
{
	assert(bus);
	if(channel < 0 || channel >= bus->channel_count || !bus->channels[channel])
	{
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return;
	}
	struct coro_bus_channel* ch = bus->channels[channel];
	while (!rlist_empty(&ch->send_queue.coros)) 
	{
        struct wakeup_entry* entry = rlist_first_entry(&ch->send_queue.coros, struct wakeup_entry, base);
		rlist_del_entry(entry, base);
        coro_wakeup(entry->coro);
    }

    while (!rlist_empty(&ch->recv_queue.coros)) 
	{
        struct wakeup_entry* entry = rlist_first_entry(&ch->recv_queue.coros, struct wakeup_entry, base);
		rlist_del_entry(entry, base);
        coro_wakeup(entry->coro);
    }
	data_vector_destroy(&ch->data);
    free(ch);
    bus->channels[channel] = NULL;
    coro_bus_errno_set(CORO_BUS_ERR_NONE);
}

int coro_bus_send(struct coro_bus* bus, int channel, unsigned data)
{
	while(true) 
	{
		int response = coro_bus_try_send(bus, channel, data);
		if(response == 0) 
			return 0;
		if(coro_bus_errno() == CORO_BUS_ERR_NO_CHANNEL)
			return -1;
		struct coro_bus_channel* ch = bus->channels[channel];
		struct wakeup_entry entry;
		entry.coro = coro_this();
		rlist_add_tail_entry(&ch->send_queue.coros, &entry, base);
        coro_suspend();
        rlist_del_entry(&entry, base);
	}
	return 0;
}

int coro_bus_try_send(struct coro_bus* bus, int channel, unsigned data)
{
	assert(bus);
	if (channel < 0 || channel >= bus->channel_count || !bus->channels[channel]) 
	{
        coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
        return -1;
    }
	struct coro_bus_channel* ch = bus->channels[channel];
	if (ch->data.size >= ch->data.capacity) 
	{
        coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
        return -1;
    }
	data_vector_push_back(&ch->data, data);
    wakeup_queue_wakeup_first(&ch->recv_queue);
    coro_bus_errno_set(CORO_BUS_ERR_NONE);
	return 0;
}

int coro_bus_try_recv(struct coro_bus* bus, int channel, unsigned* data)
{
	assert(bus);
    if (channel < 0 || channel >= bus->channel_count || !bus->channels[channel]) 
	{
        coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
        return -1;
    }
    struct coro_bus_channel* ch = bus->channels[channel];
    if (ch->data.size == 0) 
	{
        coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
        return -1;
    }
    *data = data_vector_pop_front(&ch->data);
    wakeup_queue_wakeup_first(&ch->send_queue);
    coro_bus_errno_set(CORO_BUS_ERR_NONE);
    return 0;
}

int coro_bus_recv(struct coro_bus *bus, int channel, unsigned *data)
{
    while (true) 
	{
        int rc = coro_bus_try_recv(bus, channel, data);
        if (rc == 0)
            return 0;
		if(coro_bus_errno() == CORO_BUS_ERR_NO_CHANNEL) 
			return -1;
        struct coro_bus_channel *ch = bus->channels[channel];
        struct wakeup_entry entry;
        entry.coro = coro_this();
        rlist_add_tail_entry(&ch->recv_queue.coros, &entry, base);
        coro_suspend();
        rlist_del_entry(&entry, base);
    }
	return 0;
}

#if NEED_BROADCAST

int
coro_bus_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif

#if NEED_BATCH

int
coro_bus_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif
