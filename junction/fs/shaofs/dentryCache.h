#pragma once

#include "cache.h"
#include "inodeCache.h"
#include <string>
#include <vector>

using DentryCacheManager = CacheSingleton<std::string, int>;     // pathname → inodenum

std::string join_path(const std::vector<std::string>& parts, int count);
void init_dentryCache(size_t capacity = DEFAULT_CACHE_SIZE);
std::shared_ptr<MInode> get_inode(const char *pathname);