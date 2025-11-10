#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>
#include "base.h"

#define DEFAULT_CACHE_SIZE  65536

template <typename ID, typename Entry>
class LRU
{
public:
    explicit LRU(size_t capacity) : lru_capacity(capacity)   // capacity 必须大于 0
    {
        if (capacity <= 0) throw std::invalid_argument("LRU cache capacity must be greater than 0");
        spin_lock_init(&mutex_);
    }

    void set_eviction_callback(std::function<void(const ID&, Entry)> cb)   // 设置淘汰回调（可选）
    {
        SpinGuard g(&mutex_);        // 因为只在初始化时才执行一次，因此其实可以不上锁
        on_evict = std::move(cb);     
    }

    bool get(const ID& key, Entry& out_val)   // 若存在，则取出（拷贝一份）该 id 对应的 entry；否则返回 false
    {
        SpinGuard g(&mutex_);

        auto it = map_.find(key);
        if (it == map_.end()) return false;   // cache miss

        // cache hit
        auto& entry = it->second;
        touch(entry);
        out_val = entry.data;   // 复制了一份
        return true;
    }

    void put(const ID& key, const Entry& value)  // 将一个 entry 放入 cache，若当前已经存在该 id 则进行覆盖；可能会导致 evict
    {
        bool need_evict = false;
        ID victim_key;
        Entry victim_data;

        {
            SpinGuard g(&mutex_);
            auto it = map_.find(key);
            if (it != map_.end())     // cache hit -> update
            {
                auto& entry = it->second;
                entry.data = value;    // 覆盖（复制）
                touch(entry);
                return;
            }

            // cache miss -> insert
            if (map_.size() >= lru_capacity)     // need to evict first
            {
                victim_key = lru_list.back();
                auto vit = map_.find(victim_key); 
                if (on_evict)
                {
                    need_evict = true;
                    victim_data = vit->second.data;
                }

                lru_list.pop_back();
                map_.erase(vit);
            }

            lru_list.push_front(key);
            CacheEntry entry;
            entry.data = value;   // 复制
            entry.lru_pos = lru_list.begin();
            map_.emplace(key, std::move(entry));   
        }
        
        // 将 on_evict 函数放在锁外进行
        if (need_evict) on_evict(victim_key, std::move(victim_data));
    }

    bool erase(const ID& key)     // 手动删除 cache 中的某个元素（该数据已是无效数据，不写回）
    {
        SpinGuard g(&mutex_);

        auto it = map_.find(key);
        if (it == map_.end()) return false;
        
        lru_list.erase(it->second.lru_pos);
        map_.erase(it);
        return true;
    }

    void for_each_entry(std::function<void(Entry&)> func)    // func 中不应再获取 cache 大锁
    {
        SpinGuard g(&mutex_);
        for (auto& [key, entry] : map_) 
            func(entry.data);
    }

    void move_to_end(const ID& key) 
    {
        SpinGuard g(&mutex_);

        auto it = map_.find(key);
        if (it != map_.end())     // 将该 entry 放到 LRU 链表中的末尾（最先被 evict）
        {
            auto& entry = it->second;
    
            lru_list.splice(lru_list.end(), lru_list, entry.lru_pos);   // 将元素移动到链表末尾
            entry.lru_pos = --lru_list.end();                           // 更新 lru_pos
        }
    }

    bool contains(const ID& key)
    {
        SpinGuard g(&mutex_);
        return map_.find(key) != map_.end();
    }

private:
    struct CacheEntry 
    {
        Entry data;                                // 真正缓存的数据
        typename std::list<ID>::iterator lru_pos;  // 在 LRU 列表中的位置
    };
    void touch(CacheEntry& entry)   // caller 需要加锁 
    {
        lru_list.splice(lru_list.begin(), lru_list, entry.lru_pos);  // 修改 LRUlist
        entry.lru_pos = lru_list.begin();                            // 更新该 entry 在 LRUlist 中的位置
    }
    
    std::unordered_map<ID, CacheEntry> map_;
    std::list<ID> lru_list;

    std::function<void(const ID&, Entry)> on_evict;
    mutable spinlock_t mutex_;
    size_t lru_capacity;
};

template <typename Key, typename Entry>
class LRUSingleton 
{
public:
    LRUSingleton(const LRUSingleton&)            = delete;
    LRUSingleton& operator=(const LRUSingleton&) = delete;

    static LRU<Key, Entry>& instance(size_t capacity = DEFAULT_CACHE_SIZE)   // Note: capacity only takes effect on first call. (thread-safe)
    {
        static LRU<Key, Entry> _cache(capacity);
        return _cache;
    }
};