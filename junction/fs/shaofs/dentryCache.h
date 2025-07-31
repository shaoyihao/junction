#pragma once

#include "cache.h"
#include "inodeCache.h"
#include <string>

using DentryCacheManager = CacheSingleton<std::string, int>;     // pathname → inodenum

void init_dentryCache(size_t capacity = DEFAULT_CACHE_SIZE);
std::shared_ptr<MInode> get_inode(const char *pathname);