#ifndef DENTCACHE_H
#define DENTCACHE_H

#include "fshao.h"
#define DENTRY_CACHE_NUM 128

#include <unordered_map>
#include <list>
#include <string>

class PathCache {
public:
    struct Entry {
        std::string path;
        // int inode; 
        Inode *ino;
    };

    PathCache(size_t capacity);

    bool get(const std::string& path, Inode*& out_inode);
    void put(const std::string& path, Inode* inode);
    size_t size() const { return cache_list_.size(); }

private:
    size_t capacity_;
    std::list<Entry> cache_list_;  // LRU 列表
    std::unordered_map<std::string, std::list<Entry>::iterator> cache_map_;
};

#endif