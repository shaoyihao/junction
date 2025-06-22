#include "page_allocator.h"
#include "fshao.h"
#include <vector>
#include <stack>
#include <cstdlib>
#include <iostream>

const size_t PAGE_SIZE = LBA_SIZE;   // 

static uint8_t* pool_base = nullptr;
static size_t total_pages = 0;
static std::stack<uint8_t*> free_pages;   // 每一页（的首地址）

void page_pool_init(size_t page_count) 
{
    pool_base = (uint8_t*)malloc(page_count * PAGE_SIZE);
    total_pages = page_count;
    for (size_t i = 0; i < page_count; ++i) free_pages.push(pool_base + i * PAGE_SIZE);
}

uint8_t* alloc_page() 
{
    if (free_pages.empty()) 
    {
        std::cerr << "[ERROR] PagePool Out of memory!\n";
        return nullptr;
    }

    uint8_t* page = free_pages.top();
    free_pages.pop();
    return page;
}

void free_page(uint8_t* ptr) 
{
    free_pages.push(ptr);
}

void page_pool_destroy() 
{
    free_pages = std::stack<uint8_t*>();
    free(pool_base);   // free申请的所有空间
    pool_base = nullptr;
    total_pages = 0;
}