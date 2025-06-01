#ifndef DENTRY_CACHE_MANAGER_H
#define DENTRY_CACHE_MANAGER_H

#include "dentryCache.h"

class PathCacheManager {
public:
    static PathCache& instance() {
        static PathCache cache_(DENTRY_CACHE_NUM); // 全局唯一实例，线程安全
        return cache_;
    }

private:
    PathCacheManager() = default;
    ~PathCacheManager() = default;
    PathCacheManager(const PathCacheManager&) = delete;
    PathCacheManager& operator=(const PathCacheManager&) = delete;
};

#endif