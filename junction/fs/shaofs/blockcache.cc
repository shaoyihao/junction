#include "blockcache.h"
#include "page_allocator.h"
#include "fshao.h"

extern "C" {
#include "base/log.h"
}

namespace {

inline BlockCache& cache() {   // 便捷访问
    return BlockCacheSingleton::instance();
}

void on_evict(const BlockID& lba, const BlockCacheEntry& e)
{
    if (e.dirty) writeObj(e.data, LBA_SIZE, lba, 1, 1);   // 写到盘上
    if (e.data) free_page(e.data);                        // 将 page 放回 page_pool
}

}


void block_cache_init(size_t capacity)
{
    page_pool_init(capacity);
    BlockCacheSingleton::instance(capacity).set_eviction_callback(on_evict);
}

bool block_cache_read(BlockID lba, void* out_buf)
{
    BlockCacheEntry entry;
    if (cache().get(lba, entry))     // cache hit
    {                     
        log_info("read cache hit!");
        std::memcpy(out_buf, entry.data, LBA_SIZE);
        return true;
    }

    // cache miss
    log_info("read cache miss!");
    uint8_t* page = alloc_page();
    if (!page) return false;    // pool 耗尽

    readObj(page, LBA_SIZE, lba, 1);   // 读盘

    std::memcpy(out_buf, page, LBA_SIZE);

    BlockCacheEntry new_entry{ lba, /*dirty=*/false, page };
    cache().put(lba, new_entry); 
    return true;
}


bool block_cache_write(BlockID lba, const void* src_buf)
{
    BlockCacheEntry entry;
    if (cache().get(lba, entry))    // cache hit
    {                    
        std::memcpy(entry.data, src_buf, LBA_SIZE);
        entry.dirty = true;
        cache().put(lba, entry);   
        return true;
    }

    // cache miss
    uint8_t* page = alloc_page();
    if (!page) return false;

    std::memcpy(page, src_buf, LBA_SIZE);

    BlockCacheEntry new_entry{ lba, /*dirty=*/true, page };
    cache().put(lba, new_entry);
    return true;
}