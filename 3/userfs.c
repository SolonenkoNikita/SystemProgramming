#include "userfs.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

enum 
{
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 100,
};

static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block 
{
    char* memory;
    int occupied;
    struct block* next;
    struct block* prev;
};

struct file 
{
    struct block* block_list;
    struct block* last_block;
    int refs;
    char* name;
    size_t size;
    bool deleted;
    struct file* next;
    struct file* prev;
};

static struct file* file_list = NULL;

struct filedesc 
{
    struct file* file;
    size_t pos;
#if NEED_OPEN_FLAGS
    int flags; 
#endif
};

static struct filedesc** file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;
static const int DEFAULT_FD_CAPACITY = 16;

enum ufs_error_code ufs_errno() 
{
    return ufs_error_code;
}

static struct file* find_file(const char* filename) 
{
    struct file* f = file_list;
    while (f) 
    {
        if (!f->deleted && strcmp(f->name, filename) == 0)
            return f;
        f = f->next;
    }
    return NULL;
}

static struct file* create_file(const char* filename) 
{
    struct file* f = (struct file*)calloc(1, sizeof(struct file));
    if (!f) 
    {
        ufs_error_code = UFS_ERR_NO_MEM;
        return NULL;
    }
    f->name = strdup(filename);
    if (!f->name) 
    {
        free(f);
        ufs_error_code = UFS_ERR_NO_MEM;
        return NULL;
    }
    f->next = file_list;
    if (file_list)
        file_list->prev = f;
    file_list = f;
    return f;
}

static struct filedesc* create_filedesc(struct file* f
#if NEED_OPEN_FLAGS
    , int flags
#endif
) {
    struct filedesc* fd = (struct filedesc*)malloc(sizeof(struct filedesc));
    if (!fd) 
    {
        ufs_error_code = UFS_ERR_NO_MEM;
        return NULL;
    }
    fd->file = f;
    fd->pos = 0;
#if NEED_OPEN_FLAGS
    fd->flags = flags & (UFS_READ_ONLY | UFS_WRITE_ONLY | UFS_READ_WRITE);
    if ((flags & (UFS_READ_ONLY | UFS_WRITE_ONLY)) == 0)
        fd->flags = UFS_READ_WRITE;
#endif
    return fd;
}

static int allocate_filedesc(struct file* f
#if NEED_OPEN_FLAGS
    , int flags
#endif
) {
    for (int i = 0; i < file_descriptor_capacity; ++i) 
    {
        if (!file_descriptors[i]) 
        {
            struct filedesc* fd = create_filedesc(f
#if NEED_OPEN_FLAGS
                    , flags
#endif
                    );
            if (!fd)
                return -1;
            file_descriptors[i] = fd;
            ++f->refs;
            ++file_descriptor_count;
            return i;
        }
    }

    int new_capacity = file_descriptor_capacity == 0 ? DEFAULT_FD_CAPACITY : file_descriptor_capacity * 2;
    struct filedesc** new_array = (struct filedesc**) realloc(file_descriptors, new_capacity * sizeof(struct filedesc*));
    if (!new_array) 
    {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }
    file_descriptors = new_array;
    for (int i = file_descriptor_capacity; i < new_capacity; ++i)
        file_descriptors[i] = NULL;
    file_descriptor_capacity = new_capacity;

    struct filedesc* fd = create_filedesc(f
#if NEED_OPEN_FLAGS
            , flags
#endif
            );
    if (!fd)
        return -1;

    int idx = file_descriptor_capacity / 2;
    file_descriptors[idx] = fd;
    ++f->refs;
    ++file_descriptor_count;
    return idx;
}

static struct block* create_block() 
{
    struct block* blk = (struct block*)calloc(1, sizeof(struct block));
    if (!blk) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return NULL;
    }
    blk->memory = (char*)malloc(BLOCK_SIZE);
    if (!blk->memory) {
        free(blk);
        ufs_error_code = UFS_ERR_NO_MEM;
        return NULL;
    }
    blk->occupied = 0;
    blk->next = NULL;
    blk->prev = NULL;
    return blk;
}

int ufs_open(const char* filename, int flags) 
{
    struct file* f = find_file(filename);
    if (!f) 
    {
        if (!(flags & UFS_CREATE)) 
        {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
        f = create_file(filename);
        if (!f)
            return -1;
    }
    return allocate_filedesc(f
#if NEED_OPEN_FLAGS
        , flags
#endif
        );
}

ssize_t ufs_write(int fd, const char* buf, size_t size) 
{
    if (fd < 0 || fd >= file_descriptor_capacity || !file_descriptors[fd]) 
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc* desc = file_descriptors[fd];
    struct file* file = desc->file;

#if NEED_OPEN_FLAGS
    if ((desc->flags & (UFS_WRITE_ONLY | UFS_READ_WRITE)) == 0) 
    {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }
#endif

    if (size + desc->pos > MAX_FILE_SIZE) 
    {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    size_t total_written = 0, offset = desc->pos % BLOCK_SIZE;
    struct block* current = file->block_list;

    for (size_t pos = desc->pos; current && pos >= BLOCK_SIZE;) 
    {
        current = current->next;
        pos -= BLOCK_SIZE;
    }

    if (!current) 
    {
        if (file->last_block)
            current = file->last_block;
        else 
        {
            current = create_block();
            if (!current)
                return -1;
            file->block_list = current;
            file->last_block = current;
        }
        offset = current->occupied;
    }

    while (total_written < size) 
    {
        size_t space_left = BLOCK_SIZE - offset;
        size_t to_write = size - total_written < space_left ?
            size - total_written : space_left;

        memcpy(current->memory + offset, buf + total_written, to_write);
        total_written += to_write;
        desc->pos += to_write;
        if (desc->pos > file->size)
            file->size = desc->pos;

        current->occupied = offset + to_write > (size_t)current->occupied ?
            offset + to_write : (size_t) current->occupied;

        offset = 0;
        if (to_write < space_left)
            break;

        struct block* new_block = create_block();
        if (!new_block)
            break;
        new_block->prev = current;
        current->next = new_block;
        file->last_block = new_block;
        current = new_block;
    }

    return total_written;
}

ssize_t ufs_read(int fd, char* buf, size_t size) 
{
    if (fd < 0 || fd >= file_descriptor_capacity || !file_descriptors[fd]) 
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc* desc = file_descriptors[fd];
    struct file* file = desc->file;

#if NEED_OPEN_FLAGS
    if ((desc->flags & (UFS_READ_ONLY | UFS_READ_WRITE)) == 0) 
    {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }
#endif

    if (desc->pos >= file->size)
        return 0;

    size_t total_read = 0, offset = desc->pos % BLOCK_SIZE;
    struct block* current = file->block_list;

    for (size_t pos = desc->pos; current && pos >= BLOCK_SIZE;) 
    {
        current = current->next;
        pos -= BLOCK_SIZE;
    }

    if (!current) 
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    while (total_read < size && current) 
    {
        size_t available_data = current->occupied - offset;
        size_t to_read = size - total_read < available_data ?
            size - total_read : available_data;

        if (to_read == 0)
            break;

        memcpy(buf + total_read, current->memory + offset, to_read);
        total_read += to_read;
        desc->pos += to_read;
        offset = desc->pos % BLOCK_SIZE;

        if (offset == 0)
            current = current->next;
    }

    return total_read;
}

int ufs_close(int fd) 
{
    if (fd < 0 || fd >= file_descriptor_capacity || !file_descriptors[fd]) 
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc* desc = file_descriptors[fd];
    struct file* file = desc->file;

    free(desc);
    file_descriptors[fd] = NULL;
    --file->refs;
    --file_descriptor_count;

    if (file->deleted && file->refs <= 0) 
    {
        struct block* b = file->block_list;
        while (b) 
        {
            struct block* next = b->next;
            free(b->memory);
            free(b);
            b = next;
        }
        if (file->prev)
            file->prev->next = file->next;
        if (file->next)
            file->next->prev = file->prev;
        if (file == file_list)
            file_list = file->next;
        free(file->name);
        free(file);
    }

    return 0;
}

int ufs_delete(const char* filename) 
{
    struct file* f = find_file(filename);
    if (!f) 
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    f->deleted = true;

    if (f->refs <= 0) 
    {
        struct block* b = f->block_list;
        while (b) 
        {
            struct block* next = b->next;
            free(b->memory);
            free(b);
            b = next;
        }
        if (f->prev)
            f->prev->next = f->next;
        if (f->next)
            f->next->prev = f->prev;
        if (f == file_list)
            file_list = f->next;
        free(f->name);
        free(f);
    }

    return 0;
}

#if NEED_RESIZE
int ufs_resize(int fd, size_t new_size) 
{
    if (fd < 0 || fd >= file_descriptor_capacity || !file_descriptors[fd]) 
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc* desc = file_descriptors[fd];
    struct file* file = desc->file;

#if NEED_OPEN_FLAGS
    if ((desc->flags & (UFS_WRITE_ONLY | UFS_READ_WRITE)) == 0) 
    {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }
#endif

    if (new_size == file->size)
        return 0;

    if (new_size > file->size) 
    {
        size_t needed_bytes = new_size - file->size;
        size_t needed_blocks = needed_bytes / BLOCK_SIZE;
        if (needed_bytes % BLOCK_SIZE != 0)
            needed_blocks++;

        struct block* current = file->last_block;
        for (size_t i = 0; i < needed_blocks; ++i) 
        {
            struct block* new_block = create_block();
            if (!new_block)
                return -1;
            new_block->prev = current;
            current->next = new_block;
            file->last_block = new_block;
            current = new_block;
        }
    } 
    else 
    {
        size_t target_block_index = new_size / BLOCK_SIZE;
        size_t target_offset = new_size % BLOCK_SIZE;

        struct block* current = file->block_list;
        for (size_t i = 0; i < target_block_index && current; ++i)
            current = current->next;

        if (!current)
            return -1;

        current->occupied = target_offset;

        struct block* to_free = current->next;
        current->next = NULL;
        file->last_block = current;

        while (to_free) 
        {
            struct block* next = to_free->next;
            free(to_free->memory);
            free(to_free);
            to_free = next;
        }
    }

    if (desc->pos > new_size)
        desc->pos = new_size;

    file->size = new_size;
    return 0;
}
#endif

void ufs_destroy(void) 
{
    struct file* f = file_list;
    while (f) 
    {
        struct file* next = f->next;
        struct block* blk = f->block_list;
        while (blk) 
        {
            struct block* next_blk = blk->next;
            free(blk->memory);
            free(blk);
            blk = next_blk;
        }
        free(f->name);
        free(f);
        f = next;
    }
    file_list = NULL;

    for (int i = 0; i < file_descriptor_capacity; ++i)
        if (file_descriptors[i])
            free(file_descriptors[i]);

    free(file_descriptors);
    file_descriptors = NULL;
    file_descriptor_count = 0;
    file_descriptor_capacity = 0;
}