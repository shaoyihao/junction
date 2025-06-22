#include "dentryCache.h"

PathCache::PathCache(size_t capacity) : capacity_(capacity) {}

bool PathCache::get(const std::string& path, Inode*& out_inode)   // 查询缓存
{
    auto it = cache_map_.find(path);
    if (it == cache_map_.end()) return false;

    cache_list_.splice(cache_list_.begin(), cache_list_, it->second);   // 命中：把这个条目移到链表头（表示最近使用）
    out_inode = it->second->ino;
    return true;
}

void PathCache::put(const std::string& path, Inode* inode)    // 加入缓存
{
    auto it = cache_map_.find(path);
    if (it != cache_map_.end())    // 已经有了，更新 inode 并移到头部
    {
        it->second->ino = inode;
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        return;
    }

    // 不存在，插入新条目

    if (cache_list_.size() >= capacity_)   // 淘汰最久未用的（链表尾） 
    {
        auto last = cache_list_.end(); --last;
        cache_map_.erase(last->path);
        cache_list_.pop_back();
        delete last->ino;
    }

    cache_list_.push_front({path, inode});
    cache_map_[path] = cache_list_.begin();
}