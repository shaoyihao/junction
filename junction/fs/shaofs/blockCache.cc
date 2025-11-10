#include "blockCache.h"
#include "base.h"
#include "disk.h"
#include "blockpool.h"

std::unique_ptr<BlockPool> block_pool;         // 供 blockcache 使用
std::unique_ptr<BlockPool> tmp_block_pool;     // 供一般的 malloc(BLOCKSIZE) 使用  （定义在此文件中只是为了方便）

void on_block_evict(const BlockID& lba, BlockEntry* block)   // 每次写1块到盘上好像有点慢？
{
    if (!block) return;
    
    {
        SpinGuard g(&block->mtx);
        if (block->dirty) 
        {
            log_info("flushing block %d to the disk", block->lba);
            writeObj(block->data, BLOCK_SIZE, lba, 1); 
        }
    }
    
    block_pool->free_block(block->data);   // 归还数据块到池中
    delete block;   // 释放 BlockEntry 对象
}

void block_cache_init(size_t capacity)
{
    log_info("init block cache ...");
    block_pool = std::make_unique<BlockPool>(BLOCK_SIZE, capacity);
    auto& cache = BlockCacheManager::instance(capacity);
    cache.set_eviction_callback(on_block_evict);
    log_info("block cache initialized with %zu shards, each shard has %zu capacity, total capacity is %zu", cache.get_shard_count(), cache.get_shard_capacity(), cache.get_total_capacity());

    tmp_block_pool = std::make_unique<BlockPool>(BLOCK_SIZE, 4096);
}


// 这个函数应该用的比较少？只作为一个工具函数。毕竟为什么不直接以 extent 为单位读呢？
void read_block(BlockID lba, void* out_buf)  // 读取某个 lba 中的数据（可能是 cache 命中，也可能需要从盘读取）
{
    // uint64_t before_readblock_tsc = rdtsc();
    // thread_t *th = thread_self();
    // uint64_t before_readblock = thread_get_total_cycles(th) / cycles_per_us;

    auto& cache = BlockCacheManager::instance();
    BlockEntry* block = cache.get_or_create(lba);

    {
        SpinGuard g(&block->mtx);
        if (!block->valid)
        {
            readObj(block->data, BLOCK_SIZE, lba, 1);    
            block->dirty = false;
            block->valid = true;
        }
    }

    // uint64_t before_memcpy_tsc = rdtsc();
    memcpy(out_buf, block->data, BLOCK_SIZE);  // 复制
    // uint64_t after_memcpy_tsc = rdtsc();

    // uint64_t after_readblock_tsc = rdtsc();
    // uint64_t after_readblock = thread_get_total_cycles(th) / cycles_per_us;
    // log_info("[readblock(%lu)] duration: %lu us, actual time: %lu us", lba, (after_readblock_tsc - before_readblock_tsc) / cycles_per_us, after_readblock - before_readblock);
}

void write_block(BlockID lba, const void* in_buf)   // 将一块数据写到某个 lba 中（先存于 cache 中）
{
    auto& cache = BlockCacheManager::instance();

    BlockEntry* block = cache.get_or_create(lba);
    
    {
        SpinGuard g(&block->mtx);
        memcpy(block->data, in_buf, BLOCK_SIZE);   // 复制
        block->dirty = true;    
    }
}


void flush_dirty_blocks() 
{
    auto& cache = BlockCacheManager::instance();
    
    cache.for_each_entry([](BlockEntry* entry) {
        if (entry->dirty)   // 每次 writeObj 一次就 yield 一次，应该优化为发送多个 IO 请求后再 yield
        {
            // log_info("flushing block %d to the disk", entry->lba);
            writeObj(entry->data, BLOCK_SIZE, entry->lba, 1);
            entry->dirty = false;
        }
    });
    // log_info("flushed all the dirty blocks to the disk");
}