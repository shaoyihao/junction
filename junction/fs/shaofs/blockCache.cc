#include "blockCache.h"
#include "base.h"
#include "disk.h"
#include "blockpool.h"

std::unique_ptr<BlockPool> block_pool;         // 供 blockcache 使用
std::unique_ptr<BlockPool> tmp_block_pool;     // 供一般的 malloc(BLOCKSIZE) 使用

void on_block_evict(const BlockID& lba, BlockEntry block)   // 每次写1块到盘上有点慢，之后需要进行优化
{
    if (block.dirty) 
    {
        log_info("flushing block %d to the disk", block.lba);
        writeObj(block.data, BLOCK_SIZE, lba, 1); 
    }
    block_pool->free_block(block.data);   // 归还数据块到池中
}

void block_cache_init(size_t capacity)
{
    log_info("init block cache ...");
    block_pool = std::make_unique<BlockPool>(BLOCK_SIZE, capacity);
    BlockCacheManager::instance(capacity).set_eviction_callback(on_block_evict);

    tmp_block_pool = std::make_unique<BlockPool>(BLOCK_SIZE, 4096);
}

void read_block(BlockID lba, void* out_buf)  // 读取某个 lba 中的数据（可能是 cache 命中，也可能需要从盘读取）
{
    auto& cache = BlockCacheManager::instance();

    BlockEntry block;
    if (!cache.get(lba, block))
    {
        block.lba = lba;
        block.data = block_pool->alloc_block();
        if (!block.data) 
        { 
            log_info("[read_block()] ERROR: block pool exhausted");
            return;
        }
        readObj(block.data, BLOCK_SIZE, lba, 1);    // 从盘读入该块
        block.dirty = false;
        cache.put(lba, block);
    }

    memcpy(out_buf, block.data, BLOCK_SIZE);  // 复制
}

void write_block(BlockID lba, const void* in_buf)   // 将一块数据写到某个 lba 中（先存于 cache 中）
{
    auto& cache = BlockCacheManager::instance();

    BlockEntry block;
    if (!cache.get(lba, block))
    {
        block.lba = lba;
        block.data = block_pool->alloc_block();
        if (!block.data) 
        { 
            log_info("[write_block()] ERROR: block pool exhausted");
            return;
        }
    }
    memcpy(block.data, in_buf, BLOCK_SIZE);   // 复制
    block.dirty = true;    

    cache.put(lba, block);
}


void flush_dirty_blocks() 
{
    auto& cache = BlockCacheManager::instance();
    
    cache.for_each_entry([](BlockEntry& entry) {
        if (entry.dirty) 
        {
            // log_info("flushing block %d to the disk", entry.lba);
            writeObj(entry.data, BLOCK_SIZE, entry.lba, 1);
            entry.dirty = false;
        }
    });
    // log_info("flushed all the dirty blocks to the disk");
}