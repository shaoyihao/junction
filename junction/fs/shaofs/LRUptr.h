#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>
#include <thread>
#include "base.h"

#define DEFAULT_CACHE_SIZE  65536

template <typename ID, typename Entry>
class LRUPtr
{
public:
    explicit LRUPtr(size_t capacity) : lru_capacity(capacity)
    {
        if (capacity <= 0) throw std::invalid_argument("LRU cache capacity must be greater than 0");
        spin_lock_init(&mutex_);
    }

    void set_eviction_callback(std::function<void(const ID&, Entry*)> cb)
    {
        SpinGuard g(&mutex_);
        on_evict = std::move(cb);
    }

    Entry* get(const ID& key)
    {
        SpinGuard g(&mutex_);

        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;   // cache miss

        // cache hit
        auto& entry = it->second;
        touch(entry);
        return entry.dataptr;
    }

    Entry* get_or_create(const ID& key)  // 若不存在则新建并插入；返回原始指针
    {
        bool need_evict = false;
        ID     victim_key;
        Entry* victim_dataptr = nullptr;

        Entry* result = nullptr;

        {
            SpinGuard g(&mutex_);

            auto it = map_.find(key);
            if (it != map_.end())   // cache hit
            {
                auto& entry = it->second;
                touch(entry);
                return entry.dataptr;
            }

            // cache miss -> insert
            if (map_.size() >= lru_capacity)     // need to evict
            {
                victim_key = lru_list.back();
                auto vit = map_.find(victim_key);
                if (on_evict)
                {
                    need_evict = true;
                    victim_dataptr = vit->second.dataptr;
                }

                lru_list.pop_back();
                map_.erase(vit);
            }

            lru_list.push_front(key);
            CacheEntry entry;
            entry.dataptr = new Entry(key);   // 分配新的 Entry
            entry.lru_pos = lru_list.begin();
            map_.emplace(key, std::move(entry));

            result = entry.dataptr;
        }

        // 锁外执行 evict 回调
        if (need_evict) on_evict(victim_key, victim_dataptr);
        return result;
    }

    bool erase(const ID& key)
    {
        SpinGuard g(&mutex_);

        auto it = map_.find(key);
        if (it == map_.end()) return false;

        delete it->second.dataptr;
        lru_list.erase(it->second.lru_pos);
        map_.erase(it);
        return true;
    }

    void for_each_entry(std::function<void(Entry*)> func)
    {
        SpinGuard g(&mutex_);
        for (auto& [key, entry] : map_)
            func(entry.dataptr);
    }

    void move_to_end(const ID& key)
    {
        SpinGuard g(&mutex_);

        auto it = map_.find(key);
        if (it != map_.end())
        {
            auto& entry = it->second;
            lru_list.splice(lru_list.end(), lru_list, entry.lru_pos);
            entry.lru_pos = --lru_list.end();
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
        Entry* dataptr;                                // 存储原始指针
        typename std::list<ID>::iterator lru_pos;
    };

    void touch(CacheEntry& entry)
    {
        lru_list.splice(lru_list.begin(), lru_list, entry.lru_pos);
        entry.lru_pos = lru_list.begin();
    }

    std::unordered_map<ID, CacheEntry> map_;
    std::list<ID> lru_list;

    std::function<void(const ID&, Entry*)> on_evict;
    mutable spinlock_t mutex_;
    size_t lru_capacity;
};


template <typename ID, typename Entry>
class ShardedLRUPtr
{
public:
    explicit ShardedLRUPtr(size_t total_capacity, size_t shard_count = 0)
    {
        if (shard_count == 0)
        {
            shard_count = std::thread::hardware_concurrency();
            if (shard_count == 0) shard_count = 1;
        }
        if (total_capacity <= 0) { throw std::invalid_argument("ShardedLRUPtr cache total capacity must be greater than 0"); }

        this->num_shards = shard_count;
        this->total_capacity = total_capacity;
        this->shard_capacity = CEIL(total_capacity, num_shards);

        shards.reserve(this->num_shards);
        for (size_t i = 0; i < num_shards; i++) shards.emplace_back(this->shard_capacity);
    }

    void set_eviction_callback(std::function<void(const ID&, Entry*)> cb) { for (auto& shard : shards) shard.set_eviction_callback(cb); }

    Entry* get(const ID& key)           { return get_shard(key).get(key);           }
    Entry* get_or_create(const ID& key) { return get_shard(key).get_or_create(key); }
    bool erase(const ID& key)           { return get_shard(key).erase(key);         }
    bool contains(const ID& key)        { return get_shard(key).contains(key);      }
    void move_to_end(const ID& key) { get_shard(key).move_to_end(key); }
    void for_each_entry(std::function<void(Entry*)> func) { for (auto& shard : shards) shard.for_each_entry(func); }

    size_t get_shard_count()    { return num_shards; }
    size_t get_shard_capacity() { return shard_capacity; }
    size_t get_total_capacity() { return total_capacity; }

private:
    LRUPtr<ID, Entry>& get_shard(const ID& key)
    {
        static std::hash<ID> hasher;
        return shards[hasher(key) % num_shards];
    }

    size_t num_shards, total_capacity, shard_capacity;
    std::vector<LRUPtr<ID, Entry>> shards;
};


template <typename Key, typename Entry>
class ShardedLRUPtrSingleton 
{
public:
    ShardedLRUPtrSingleton(const ShardedLRUPtrSingleton&) = delete;
    ShardedLRUPtrSingleton& operator=(const ShardedLRUPtrSingleton&) = delete;

    static ShardedLRUPtr<Key, Entry>& instance(size_t capacity = DEFAULT_CACHE_SIZE, size_t shards = 0)
    {
        static ShardedLRUPtr<Key, Entry> _cache(capacity, shards);
        return _cache;
    }
};