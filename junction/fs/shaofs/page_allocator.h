#pragma once
#include <cstdint>
#include <cstddef>

void page_pool_init(size_t page_count);
uint8_t* alloc_page();
void free_page(uint8_t* ptr);
void page_pool_destroy();