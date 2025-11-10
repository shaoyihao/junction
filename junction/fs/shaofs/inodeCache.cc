#include "inodeCache.h"
#include "disk.h"
#include "base.h"
#include "file.h"
#include "inode.h"
#include "dentry.h"

int alloc_inode(file_type_t type, MInode*& inode, int inum) 
{
    inode = nullptr;
    if (inum == -1) inum = alloc_inum();    // 没有给定 inum，则分配一个
    if (inum == -1) return -1;

    if (!bitmap_test(imap, inum))  // 确保 inum 在 inode_bitmap 中已经置位   （double check）
    {
        log_info("[alloc_inode()] ERROR: inum %d is not in inode_bitmap!", inum);
        return -1;
    }

    // 在 inode cache 中创建一个 entry
    auto &cache = InodeCacheManager::instance();
    MInode* new_inode = cache.get_or_create(inum);
    if (!new_inode)
    {
        log_info("[alloc_inode()] ERROR: get_or_create failed!");
        free_inum(inum);
        return -1;
    }

    SpinGuard g(&new_inode->lock);  // 对 inode 加锁后初始化内容
    new_inode->dirty = true;   // 新分配的，需要写回磁盘
    new_inode->valid = true;   
    new_inode->disk_inode.idx                   = inum;
    new_inode->disk_inode.used                  = true;
    new_inode->disk_inode.type                  = type;
    new_inode->disk_inode.nlink                 = 1;      // 总是因为 create_file() 而创建，因此 nlink 初始时总为 1
    new_inode->disk_inode.file_size             = 0;
    new_inode->disk_inode.indirect_extent_block = sb.indirect_block_start + inum;

    inode = new_inode;
	return inum;
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
    delete inode;
}
void init_inode_cache(size_t capacity) 
{
    log_info("init inode cache ...");
    auto& cache = InodeCacheManager::instance(capacity);
    cache.set_eviction_callback(on_inode_evict);
    log_info("inode cache initialized with %zu shards, each shard has %zu capacity, total capacity is %zu", cache.get_shard_count(), cache.get_shard_capacity(), cache.get_total_capacity());
}

MInode* get_inode(int inum) 
{
    // log_info("get_inode(%d)", inum);
    // thread_t *th = thread_self();
    // uint64_t before_getinode = thread_get_total_cycles(th) / cycles_per_us, after_getinode;
    // uint64_t before_getinode_tsc = rdtsc(), after_getinode_tsc;

    auto& cache = InodeCacheManager::instance();

    MInode* inode_ptr = cache.get_or_create(inum);

    {
        SpinGuard g(&inode_ptr->lock);
        if (!inode_ptr->valid)  // 从磁盘加载
        {
            read_inode(inum, &inode_ptr->disk_inode);
            if (!inode_ptr->disk_inode.used) 
            {
                log_info("[get_inode(%d)] ERROR: Trying to get an unused inode!", inum);
                return nullptr;
            }
            inode_ptr->valid = true;
            inode_ptr->dirty = false;
        }
        inode_ptr->refcnt++;
    }

    // after_getinode_tsc = rdtsc();
    // after_getinode = thread_get_total_cycles(th) / cycles_per_us;
    // log_info("inodeCache MISS, read from disk! [get_inode(%d)] duration: %lu us, actual time: %lu", inum, (after_getinode_tsc - before_getinode_tsc) / cycles_per_us, (after_getinode - before_getinode));

    if (!inode_ptr) 
    {
        log_info("fail to get_inode(%d)", inum);   
    }
    return inode_ptr;    // 返回 inode pointer
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

    SpinGuard g(&inode->lock);
    inode->refcnt--;
    // log_info("decrease ref count of inode [%d], current refcount: %d", inode->inum, inode->refcnt);
    if (inode->refcnt)
    {
        inode = nullptr;
        return;
    }

    // inode->refcnt == 0，下面判断是否删除该 inode&file

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