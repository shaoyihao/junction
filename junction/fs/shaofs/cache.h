#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>
#include <mutex>
#include "base.h"

#define DEFAULT_CACHE_SIZE  1024

template <typename ID, typename Entry>
class LRUCache 
{
public:
    explicit LRUCache(size_t capacity) : cache_capacity(capacity) 
    {
        if (cache_capacity == 0) 
            throw std::invalid_argument("capacity must be > 0");
    }

    void set_eviction_callback(std::function<bool(const ID&, Entry&)> cb)   // 设置淘汰回调，可选
    {
        // std::lock_guard<std::mutex> lock(mutex_);
        on_evict = std::move(cb);     // 因为只在初始化时才执行一次，因此就不必上锁了
    }

    bool get(const ID& key, Entry& out_val)   // 若存在，则取出（拷贝一份）该 id 对应的 entry；否则返回 false
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
        auto& entry = *(it->second);
        touch(key, entry);
        out_val = entry.data;   // 复制了一份
        return true;
    }

    void put(const ID& key, const Entry& value)  // 将一个 entry 放入 cache，若当前已经存在该 id 则进行覆盖；可能会导致 evict
    {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = cache_map.find(key);
        if (it != cache_map.end())     // cache hit -> update
        {
            auto& entry = *(it->second);
            touch(key, entry);
            entry.data = value;    // 覆盖（复制）
            return;
        }

        // cache miss -> insert

        ID    victim_key{};
        Entry victim_val{};
        if (cache_map.size() >= cache_capacity)     // evict
        {
            if (on_evict) 
            {
                if (!on_evict(victim_key, victim_val))   // 失败则直接返回   （on_evict 函数中不应再获取 cache 的大锁）
                {
                    log_info("fail to evict");
                    return;    
                }
            }

            victim_key = lru_list.back(); 

            auto vit = cache_map.find(victim_key); 
            victim_val = vit->second->data;
            
            lru_list.pop_back();
            cache_map.erase(vit);
        }

        lru_list.push_front(key);
        auto entry = std::make_unique<CacheEntry>(value);    // 复制
        entry->lru_pos = lru_list.begin();
        cache_map[key] = std::move(entry);        
    }

    bool erase(const ID& key)     // 手动删除 cache 中的某个元素
    {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = cache_map.find(key);
        if (it == cache_map.end()) return false;

        Entry val = it->second->data;
        lru_list.erase(it->second->lru_pos);
        cache_map.erase(it);
        
        lock.unlock();
        if (on_evict) on_evict(key, val);
        return true;
    }

    void for_each_entry(std::function<void(Entry&)> func) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [key, entry] : cache_map) func((*entry).data);
    }

    void move_to_end(const ID& key) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_map.find(key);
        if (it != cache_map.end()) 
        {
            auto& entry = *(it->second);
    
            lru_list.splice(lru_list.end(), lru_list, entry.lru_pos);   // 将元素移动到链表末尾
            entry.lru_pos = --lru_list.end();   // 更新 lru_pos
        }
    }

    bool contains(const ID& key) const { std::lock_guard<std::mutex> lock(mutex_); return cache_map.find(key) != cache_map.end(); }
    size_t size()                const { std::lock_guard<std::mutex> lock(mutex_); return cache_map.size(); }
    size_t capacity()            const { return cache_capacity; }

    size_t hits()                const { std::lock_guard<std::mutex> lock(mutex_); return hits_;   }
    size_t misses()              const { std::lock_guard<std::mutex> lock(mutex_); return misses_; }
    double hit_rate()            const { std::lock_guard<std::mutex> lock(mutex_); size_t total = hits_ + misses_; return total == 0 ? 0.0 : static_cast<double>(hits_) / total; }

private:
    struct CacheEntry 
    {
        Entry data;                                // 真正缓存的数据
        typename std::list<ID>::iterator lru_pos;  // 在 LRU 列表中的位置
    };
    void touch(const ID& key, CacheEntry& entry)   // 此处 key 其实没有用到；caller 需要加锁
    {
        lru_list.splice(lru_list.begin(), lru_list, entry.lru_pos);
        entry.lru_pos = lru_list.begin();
    }

    size_t cache_capacity;
    std::unordered_map<ID, std::unique_ptr<CacheEntry>> cache_map;
    std::list<ID> lru_list;
    std::function<bool(const ID&, Entry&)> on_evict;
    mutable std::mutex mutex_;
    mutable size_t hits_ = 0, misses_ = 0;
};


template <typename Key, typename Entry>
class CacheSingleton 
{
public:
    CacheSingleton(const CacheSingleton&)            = delete;
    CacheSingleton& operator=(const CacheSingleton&) = delete;

    static LRUCache<Key, Entry>& instance(size_t capacity = DEFAULT_CACHE_SIZE)   // Note: capacity only takes effect on first call.
    {
        static LRUCache<Key, Entry> _cache(capacity);
        return _cache;
    }
};