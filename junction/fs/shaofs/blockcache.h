#pragma once
#include "cache.h" 

using BlockID = uint64_t; 
struct BlockCacheEntry 
{
    BlockID lba;
    bool dirty;
    uint8_t* data;   // data[]
};
using BlockCacheSingleton = CacheSingleton<BlockID, BlockCacheEntry>;
using BlockCache = LRUCache<BlockID, BlockCacheEntry>;

void block_cache_init(size_t capacity);
bool block_cache_read(BlockID lba, void* out_buf);
bool block_cache_write(BlockID lba, const void* src_buf);