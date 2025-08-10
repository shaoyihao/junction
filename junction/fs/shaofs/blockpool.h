#pragma once

#include "base.h"
#include <cstdlib>
#include <vector>
#include <mutex>

class BlockPool {
public:
    BlockPool(size_t block_size, size_t capacity, void* base_addr = nullptr) : block_size(block_size), capacity(capacity), external_memory(base_addr != nullptr)  // 仅初始化时执行一次
    {
        if (external_memory) data = (char*)base_addr;  // 使用外部传入的内存地址
        else 
        {
            data = (char*)malloc(block_size * capacity);  // 内部分配
            if (!data) throw std::bad_alloc();
        }

        for (size_t i = 0; i < capacity; i++) free_list.push_back(data + i * block_size);   // 初始化空闲块指针列表

        log_info("BlockPool initialized: %zu blocks, block size=%zu (%zu KB total)", capacity, block_size, (block_size * capacity) / 1024);
    }

    ~BlockPool() 
    {
        if (!external_memory) free(data);  // 只有内部分配的才需要释放
    }

    char* alloc_block() 
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (free_list.empty()) return NULL;

        char* blk = free_list.back();
        free_list.pop_back();
        return blk;
    }

    void free_block(char* blk) 
    {
        std::lock_guard<std::mutex> lock(mtx);

        free_list.push_back(blk);
    }

    size_t block_size_bytes() const { return block_size;       }
    size_t capacity_blocks()  const { return capacity;         }
    size_t free_blocks_cnt()  const 
    { 
        std::lock_guard<std::mutex> lock(mtx);
        return free_list.size(); 
    }

private:
    size_t              block_size;
    size_t              capacity;
    char*               data;
    bool                external_memory;   // 是否使用外部传入的内存
    std::vector<char*>  free_list;         // 空闲块列表
    mutable std::mutex  mtx;
};