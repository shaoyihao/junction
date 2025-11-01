#include "inodeCache.h"
#include "disk.h"
#include "base.h"
#include "file.h"
#include "inode.h"
#include "dentry.h"
#include <vector>
#include <numeric>
#include <random>
#include <algorithm>

constexpr size_t NUM_INODE_LOCKS = 1024; // 锁的数量，通常是2的幂
static spinlock_t g_inode_locks[NUM_INODE_LOCKS];

static spinlock_t* get_lock_for_inum(int inum)  // 根据 inode 号获取对应的锁
{
    return &g_inode_locks[static_cast<unsigned int>(inum) % NUM_INODE_LOCKS];
}

int alloc_inode(file_type_t type, MInode*& inode) 
{
    inode = nullptr;

    for (int idx = 0; idx < INODENUM; idx++)
        if (!bitmap_atomic_test_and_set(imap, idx))   // 原子地查找一个盘上未使用的inode
        {
            // 初始化 inode 内容
            // MInode* new_inode = (MInode*)smalloc(sizeof(MInode));
            MInode* new_inode = new MInode;
            if (!new_inode)
            {
                log_info("[alloc_inode()] ERROR: new failed!");
                bitmap_atomic_clear(imap, idx);
                return -1;
            }

            new_inode->inum = idx;
            new_inode->refcnt = 1;     // 新分配的 inode refcnt 为 1
            new_inode->dirty = true;   // 新分配的，需要写回磁盘
            spin_lock_init(&new_inode->lock);
            new_inode->disk_inode.idx                   = idx;
            new_inode->disk_inode.used                  = true;
            new_inode->disk_inode.type                  = type;
            new_inode->disk_inode.nlink                 = 1;
            new_inode->disk_inode.file_size             = 0;
            new_inode->disk_inode.indirect_extent_block = sb.indirect_block_start + idx;
            
            auto &cache = InodeCacheManager::instance();
            cache.put(idx, new_inode);

            inode = new_inode;
			return idx;
        }

    log_info("[ERROR] alloc_inode(): No free inode!");
    return -1;
}
void free_inum(int inum)    // 释放 inode，好像很少有场景需要 free，除非是删除文件
{
    if (inum < 0 || inum >= INODENUM) return;
    bitmap_atomic_clear(imap, inum);
}


void on_inode_evict(const int& key, MInode* inode)    // 将该 inode 从 inodeCache 中删除（即从内存中删除）
{
    if (inode == nullptr) return;
    if (inode->refcnt > 0) 
    {
        log_info("[on_inode_evict] ERROR: evict an Minode whose refcnt > 0. inum=%d, refcnt=%d", key, inode->refcnt);
        return;
    }

    flush_inode(inode);   // 把 inode 写回磁盘（准确来说是 block cache）
    // log_info("Evicting inode %d, deleting memory.", key);
    // sfree(inode); 
    delete inode;
}
void init_inode_cache(size_t capacity) 
{
    log_info("init inode cache ...");
    InodeCacheManager::instance(capacity).set_eviction_callback(on_inode_evict);

    for (size_t i = 0; i < NUM_INODE_LOCKS; ++i) {
        spin_lock_init(&g_inode_locks[i]);
    }
}

MInode* get_inode(int inum) 
{
    thread_t *th = thread_self();
    uint64_t before_getinode = thread_get_total_cycles(th) / cycles_per_us, after_getinode;
    uint64_t before_getinode_tsc = rdtsc(), after_getinode_tsc;

    auto& cache = InodeCacheManager::instance();

    MInode* inode_ptr = nullptr;

    if (cache.get(inum, inode_ptr))   // cache hit
    {
        // log_info("get_inode(%d): inodecache hit!", inum);
        ref_inode(inode_ptr);
        after_getinode_tsc = rdtsc();
        after_getinode = thread_get_total_cycles(th) / cycles_per_us;
        log_info("inodeCache HIT! [get_inode(%d)] duration: %lu us, actual time: %lu", inum, (after_getinode_tsc - before_getinode_tsc) / cycles_per_us, (after_getinode - before_getinode));
        return inode_ptr;
    }

    // cache miss：从盘读取

    // log_info("get_inode(%d): inodecache miss, acquiring spinlock...", inum);
    SpinGuard guard(get_lock_for_inum(inum));

    if (cache.get(inum, inode_ptr))    // 双重检查，在获取锁的期间，可能有另一个线程已经加载了这个 inode，所以需要再次检查缓存。
    {
        // log_info("get_inode(%d): inodecache hit! (double-checked)", inum);
        ref_inode(inode_ptr);
        after_getinode_tsc = rdtsc();
        after_getinode = thread_get_total_cycles(th) / cycles_per_us;
        log_info("inodeCache HIT! [get_inode(%d)] duration: %lu us, actual time: %lu", inum, (after_getinode_tsc - before_getinode_tsc) / cycles_per_us, (after_getinode - before_getinode));
        return inode_ptr;
    }

    // 确认缓存未命中，从磁盘加载
    // log_info("get_inode(%d): confirmed inodecache miss, read inode from the disk, refcount = 1", inum);

    DInode tmp_disk_inode;  
    read_inode(inum, &tmp_disk_inode);
    if (!tmp_disk_inode.used) 
    {
        log_info("[get_inode(%d)] ERROR: Trying to get an unused inode!", inum);
        return nullptr;
    }

    inode_ptr = new MInode;
    if (!inode_ptr)
    {
        log_info("[get_inode(%d)] ERROR: new failed!", inum);
        return nullptr;
    }
    inode_ptr->inum = inum;
    inode_ptr->disk_inode = tmp_disk_inode;
    inode_ptr->refcnt = 1;
    inode_ptr->dirty = false;
    spin_lock_init(&inode_ptr->lock);

    cache.put(inum, inode_ptr);
    after_getinode_tsc = rdtsc();
    after_getinode = thread_get_total_cycles(th) / cycles_per_us;
    log_info("inodeCache MISS, read from disk! [get_inode(%d)] duration: %lu us, actual time: %lu", inum, (after_getinode_tsc - before_getinode_tsc) / cycles_per_us, (after_getinode - before_getinode));
    return inode_ptr;
}

void mark_inode_dirty(MInode* inode)
{
    if (inode == nullptr) return; // 安全检查

    SpinGuard g(&inode->lock);
    inode->dirty = true;
}

void flush_inode(MInode* inode)    // 将 Minode flush 到 blockCache 中（尽管可能 refcnt>0）
{
    if (inode == nullptr) return;       // 安全检查

    SpinGuard g(&inode->lock);
    if (inode->dirty == false) return;  // 若不脏即不用 flush

    // log_info("flushing inode %d to the blockcache", inode->inum);
    write_inode(inode->inum, &inode->disk_inode);
    inode->dirty = false;
}

void ref_inode(MInode* inode) 
{
    if (inode == nullptr) return;
    
    SpinGuard g(&inode->lock);
    inode->refcnt++;
    // log_info("increase ref count of inode [%d], current refcount: %d", inode->inum, inode->refcnt);
}

void release_inode(MInode*& inode)     // 减少一个内存 inode 的引用
{
    if (inode == nullptr) return;

    {
        SpinGuard g(&inode->lock);
        inode->refcnt--;
        // log_info("decrease ref count of inode [%d], current refcount: %d", inode->inum, inode->refcnt);
        if (inode->refcnt)
        {
            inode = nullptr;
            return;
        }
    }

    // inode->refcnt == 0，下面判断是否删除该 inode&file
    
    SpinGuard guard(get_lock_for_inum(inode->inum));
    if (inode->refcnt > 0)
    {
        // log_info("[release_inode] double check not pass, reject to release");
        return;
    }

    auto& cache = InodeCacheManager::instance();
    if (inode->disk_inode.nlink != 0)
    {    
        cache.move_to_end(inode->inum);
    }
    else   // 硬链接为0，删除文件
    {
        // log_info("inode[%d]: refcnt=0 and nlink=0. Deleting.", inode->inum);
        truncate_inode_data_locked(inode, 0);
        free_inum(inode->inum);
        cache.erase(inode->inum);
        // sfree(inode);
        delete inode;
    }

    inode = nullptr;    // 将这个 inode pointer 置为 nullptr
}

void unlink_inode(MInode* dirinode, char *name, MInode* inode) 
{
    // log_info("unlink_inode() START for name '%s' in dir inode %d", name, dirinode->inum);

    if (inode == nullptr || dirinode == nullptr) 
    {
        log_info("[unlink_inode()] ERROR: inode or dirinode is nullptr");
        return;   
    }

    if (inode->disk_inode.type == DIRECTORY)   // 删除目录应使用 rmdir() 或 remove()
    {
        log_info("unlink_inode() ERROR: cannot unlink a directory"); 
        return;
    }

    delete_dentry(dirinode, name);

    {
        SpinGuard g(&inode->lock);
        inode->disk_inode.nlink--;
        inode->dirty = true;
        // log_info("[unlink_inode(%d)] current hardlink = %d", inode->inum, inode->disk_inode.nlink);
    }

    // log_info("unlink_inode() OVER");
}

void flush_dirty_inodes()
{
    auto& cache = InodeCacheManager::instance();
    cache.for_each_entry([](MInode* inode) {
        flush_inode(inode);
    });
    // log_info("flushed all the dirty inodes to the disk");
}


void print_inode(MInode *inode)
{
    log_info("----MInode [%d]:\nfilesize: %lu\nhard link: %u\nrefcnt: %d\ndirty:%d\n-----\n", inode->inum, inode->disk_inode.file_size, inode->disk_inode.nlink, inode->refcnt, inode->dirty);
}