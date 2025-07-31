#pragma once

#include "base.h"
#include "cache.h" 
#include <mutex>

typedef struct BlockEntry
{
    BlockID lba;
    char data[BLOCK_SIZE]; 
    bool dirty;

    BlockEntry() = default;
    BlockEntry(BlockID blkid) : lba(blkid), dirty(false) {}
} BlockEntry;


using BlockCacheManager = CacheSingleton<BlockID, std::shared_ptr<BlockEntry>>;


void block_cache_init(size_t capacity = DEFAULT_CACHE_SIZE);
void read_block(BlockID lba, char* out_buf);
void write_block(BlockID lba, const char* in_buf);
void flush_dirty_blocks();