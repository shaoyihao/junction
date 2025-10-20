#pragma once

#include "LRU.h"
#include "inodeCache.h"
#include <string>
#include <vector>

using DentryCacheManager = LRUSingleton<std::string, int>;     // pathname → inodenum

std::string join_path(const std::vector<std::string>& parts, int count);
void init_dentryCache(size_t capacity = DEFAULT_CACHE_SIZE);
MInode* get_inode_by_pathname(const char *pathname);