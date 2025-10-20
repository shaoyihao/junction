#pragma once

#include "base.h"
#include <cstdlib>
#include <vector>

class BlockPool {
public:
    BlockPool(size_t block_size, size_t count, void* base_addr = nullptr) : block_size(block_size), count(count), external_memory(base_addr != nullptr)  // 仅初始化时执行一次
    {
        mtx.locked = 0;
        
        if (external_memory) data = (char*)base_addr;  // 使用外部传入的内存地址
        else 
        {
            data = (char*)malloc(block_size * count);  // 内部分配
            if (!data) throw std::bad_alloc();
        }

        for (size_t i = 0; i < count; i++) free_list.push_back(data + i * block_size);   // 初始化空闲块指针列表

        log_info("BlockPool initialized: %zu blocks, block size=%zu (%zu KB total)", count, block_size, (block_size * count) / 1024);
    }

    ~BlockPool() 
    {
        if (!external_memory) free(data);  // 只有内部分配的才需要释放
    }

    char* alloc_block() 
    {
        SpinGuard g(&mtx);

        if (free_list.empty()) return NULL;
        char* blk = free_list.back();
        free_list.pop_back();
        return blk;
    }

    void free_block(char* blk) 
    {
        SpinGuard g(&mtx);
        free_list.push_back(blk);
    }

    size_t blocksize()       { return block_size;       }
    size_t blockcount()      { return count;            }
    size_t free_blocks_cnt() { SpinGuard g(&mtx); return free_list.size(); }

private:
    size_t              block_size;
    size_t              count;
    char*               data;
    bool                external_memory;   // 是否使用外部传入的内存空间
    std::vector<char*>  free_list;         // 空闲块列表
    spinlock_t          mtx;
};

extern std::unique_ptr<BlockPool> tmp_block_pool;