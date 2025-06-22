#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>
#include <mutex>

template <typename ID, typename Entry>
class LRUCache   // thread-safe
{
public:
    explicit LRUCache(size_t capacity) : cache_capacity(capacity) {
        if (cache_capacity == 0) 
            throw std::invalid_argument("capacity must be > 0");
    }

    void set_eviction_callback(std::function<void(const ID&, const Entry&)> cb)   // 设置淘汰回调，可选
    {
        std::lock_guard<std::mutex> lock(mutex_);

        on_evict = std::move(cb);
    }

    bool get(const ID& key, Entry& out_value) 
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_map.find(key);
        if (it == cache_map.end())   // cache miss
        {
            misses_++;
            return false; 
        }

        // cache hit
        hits_++;
        touch(it->first, *(it->second));
        out_value = it->second->data;
        return true;
    }
    bool peek(const ID& key, Entry& out_value) const    // 不会修改 LRU 顺序
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_map.find(key);
        if (it == cache_map.end()) return false;
        out_value = it->second->value;
        return true;
    }

    void put(const ID& key, const Entry& value) 
    {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = cache_map.find(key);
        if (it != cache_map.end())     // cache hit -> update
        {
            it->second->data = value;
            touch(key, *(it->second));
            return;
        }

        // cache miss -> insert

        bool evicted = false;
        ID victim_key{};
        Entry victim_val{};
        if (cache_map.size() >= cache_capacity)     // evict
        {
            victim_key = lru_list.back(); 
            lru_list.pop_back();
            victim_val = cache_map[victim_key]->data;
            cache_map.erase(victim_key);
            evicted = true;
        }

        lru_list.push_front(key);
        auto entry = std::make_unique<CacheEntry>(value);
        entry->lru_pos = lru_list.begin();
        cache_map[key] = std::move(entry);

        lock.unlock();   // 释放锁，从而在 on_evict 中可以获取到锁
        if (evicted && on_evict) on_evict(victim_key, victim_val);
    }

    bool contains(const ID& key) const { std::lock_guard<std::mutex> lock(mutex_); return cache_map.find(key) != cache_map.end(); }
    size_t size()                const { std::lock_guard<std::mutex> lock(mutex_); return cache_map.size(); }
    size_t capacity()            const { return cache_capacity; }

    size_t hits()                const { std::lock_guard<std::mutex> lock(mutex_); return hits_;   }
    size_t misses()              const { std::lock_guard<std::mutex> lock(mutex_); return misses_; }
    double hit_rate()            const { std::lock_guard<std::mutex> lock(mutex_); const size_t total = hits_ + misses_; return total == 0 ? 1.0 : static_cast<double>(hits_) / total; }

private:
    struct CacheEntry 
    {
        Entry data;                                // 真正缓存的数据
        typename std::list<ID>::iterator lru_pos;  // 在 LRU 列表中的位置

        explicit CacheEntry(const Entry& val) : data(val) {}
    };
    void touch(const ID& key, CacheEntry& entry) 
    {
        lru_list.erase(entry.lru_pos);
        lru_list.push_front(key);
        entry.lru_pos = lru_list.begin();
    }

    size_t cache_capacity;
    std::unordered_map<ID, std::unique_ptr<CacheEntry>> cache_map;
    std::list<ID> lru_list;
    std::function<void(const ID&, const Entry&)> on_evict;
    mutable std::mutex mutex_;
    mutable size_t hits_ = 0, misses_ = 0;
};


template <typename Key, typename Entry>
class CacheSingleton 
{
public:
    CacheSingleton(const CacheSingleton&)            = delete;
    CacheSingleton& operator=(const CacheSingleton&) = delete;

    static LRUCache<Key, Entry>& instance(std::size_t capacity = 0)   // 全局唯一访问点
    {
        static LRUCache<Key, Entry> _cache(capacity ? capacity : default_capacity());
        return _cache;
    }

    static void set_default_capacity(std::size_t cap) { default_capacity() = cap; }

private:
    CacheSingleton() = default;

    static std::size_t& default_capacity()
    {
        static std::size_t cap = 1024;   // 默认为1024（静态变量，可作为左值被修改）
        return cap;
    }
};