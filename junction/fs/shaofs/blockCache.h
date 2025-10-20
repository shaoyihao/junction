#pragma once

#include "base.h"
#include "LRU.h" 

typedef struct BlockEntry
{
    BlockID lba;
    char* data; 
    bool dirty;
} BlockEntry;


using BlockCacheManager = LRUSingleton<BlockID, BlockEntry>;


void block_cache_init(size_t capacity = DEFAULT_CACHE_SIZE);
void read_block(BlockID lba, void* out_buf);
void write_block(BlockID lba, const void* in_buf);
void flush_dirty_blocks();