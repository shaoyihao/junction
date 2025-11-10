#pragma once

#include "base.h"
// #include "LRU.h"
#include "LRUptr.h"
#include "blockpool.h"

extern std::unique_ptr<BlockPool> block_pool;

struct BlockEntry
{
    BlockID     lba;
    char*       data;
    bool        valid;    // 是否从盘上读取了数据
    bool        dirty;    // 是否需要写回磁盘
    spinlock_t  mtx;

    // BlockEntry() = default;
    BlockEntry(BlockID lba) : lba(lba), data(block_pool->alloc_block()), dirty(false), valid(false) 
    {
        if (!data) 
        { 
            log_info("[create BlockEntry] ERROR: block pool exhausted");
            throw std::bad_alloc();
        }
        mtx.locked = 0;
    }
};


using BlockCacheManager = ShardedLRUPtrSingleton<BlockID, BlockEntry>;


void block_cache_init(size_t capacity = DEFAULT_CACHE_SIZE);
void read_block(BlockID lba, void* out_buf);
void write_block(BlockID lba, const void* in_buf);
void flush_dirty_blocks();