#include "blockCache.h"
#include "base.h"
#include "disk.h"

bool on_block_evict(const BlockID& lba, std::shared_ptr<BlockEntry>& block)   // 每次写1块到盘上有点慢，之后需要进行优化
{
    if (block->dirty) writeObj(block->data, BLOCK_SIZE, lba, 1); 
    return true;
}
void block_cache_init(size_t capacity)
{
    log_info("init block cache ...");
    BlockCacheManager::instance(capacity).set_eviction_callback(on_block_evict);
}

void read_block(BlockID lba, char* out_buf)  // 读取 cache 中的某一块
{
    auto& cache = BlockCacheManager::instance();

    std::shared_ptr<BlockEntry> block;

    if (cache.get(lba, block))   // cache hit
    {
        log_info("block cache hit");
        memcpy(out_buf, block->data, BLOCK_SIZE);     // 复制
        return;
    }

    // cache miss
    // log_info("block cache miss");

    block = std::make_shared<BlockEntry>();
    block->lba = lba;
    readObj(block->data, BLOCK_SIZE, lba, 1);    // 从盘读入该块
    block->dirty = false;

    cache.put(lba, block);
    
    memcpy(out_buf, block->data, BLOCK_SIZE);
}

void write_block(BlockID lba, const char* in_buf)
{
    auto& cache = BlockCacheManager::instance();

    std::shared_ptr<BlockEntry> block = std::make_shared<BlockEntry>();;
    block->lba = lba;
    memcpy(block->data, in_buf, BLOCK_SIZE);
    block->dirty = true;    

    cache.put(lba, block);
}


void flush_dirty_blocks() 
{
    auto& cache = BlockCacheManager::instance();
    cache.for_each_entry([](std::shared_ptr<BlockEntry>& entry) {
        if (entry->dirty) 
        {
            writeObj(entry->data, BLOCK_SIZE, entry->lba, 1);
            entry->dirty = false;
        }
    });
    log_info("flushed all the dirty blocks to the disk");
}