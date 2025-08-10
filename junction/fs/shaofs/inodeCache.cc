#include "inodeCache.h"
#include "disk.h"
#include "base.h"
#include <vector>
#include <mutex>
#include <numeric>
#include <random>
#include <algorithm>

int alloc_inode(file_type_t type, std::shared_ptr<MInode>& inode) 
{
    std::vector<int> b(CEIL(INODENUM, 64));
    std::iota(b.begin(), b.end(), 0);

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(b.begin(), b.end(), g);

    for (int i : b)
    // for (int i = 0; i < imap_size; i++)   // 寻找第一个空闲位
    {
        std::lock_guard<std::mutex> lock(imap_locks[i]);

        if (imap[i] == ~0ULL) continue;  

        for (int bit = 0; bit < 64; bit++)
        {
            if ((imap[i] & (1ULL << bit)) == 0) 
            {
                imap[i] |= (1ULL << bit);
				int idx = i * 64 + bit;
				if (idx >= INODENUM) 
                {
                    log_info("ERROR: No free inode!");
                    return -1;
                }
				else    // 找到了一个空闲 inode
				{
					// write_imap(imap);  // 好像可以延时写，后面考虑进行优化

                    // 初始化 inode 内容
                    inode = std::make_shared<MInode>();
                    inode->inum = idx;
                    inode->refcnt = 1;
                    inode->dirty = true;
                    inode->valid = true;
                    spin_lock_init(&inode->lock);

                    inode->disk_inode.idx = idx;
                    inode->disk_inode.used = true;
                    inode->disk_inode.type = type;
                    inode->disk_inode.nlink = 1;
                    inode->disk_inode.file_size = 0;
                    inode->disk_inode.indirect_extent_block = sb.indirect_block_start + idx;
                    
                    auto &cache = InodeCacheManager::instance();
                    cache.put(idx, inode);    // 放到 inode cache 中

					return idx;
				}
            }
        }
    }

    log_info("ERROR: No free inode!");
    return -1;
}
void free_inode(int inum)    // 好像很少有场景需要 free，除非是删除文件（得考虑到 refcnt, TODO）
{
    if (inum < 0 || inum >= INODENUM) return;

    int idx    = inum / 64;
    int offset = inum % 64;

    std::lock_guard<std::mutex> lock(imap_locks[idx]);
    if ((imap[idx] & (1ULL << offset)) == 0)
    {
        log_info("WARNING: inode %d already free", inum);
        return;
    }

    imap[idx] &= ~(1ULL << offset);   // 清除这个bit
    // write_imap(imap);                 // 好像可以延时写，后面考虑进行优化

    // 主动删除 inode cache 中的该 inode
    // auto& cache = InodeCacheManager::instance();
    // cache.erase(inum);
}


bool on_inode_evict(const int& key, std::shared_ptr<MInode>& inode) 
{
    return flush_inode(inode);  // 把 inode 写回磁盘（准确来说是 block cache）
}
void init_inode_cache(size_t capacity) 
{
    log_info("init inode cache ...");
    InodeCacheManager::instance(capacity).set_eviction_callback(on_inode_evict);
}

std::shared_ptr<MInode> get_inode(int inum) 
{
    auto& cache = InodeCacheManager::instance();

    std::shared_ptr<MInode> inode_ptr;

    if (cache.get(inum, inode_ptr))   // cache hit
    {
        log_info("inode cache hit!");
        ref_inode(inode_ptr);
        return inode_ptr;
    }
    else                              // cache miss：从盘读取
    {
        log_info("inode cache miss!");
        
        DInode tmp_disk_inode;  
        read_inode(inum, &tmp_disk_inode);

        inode_ptr = std::make_shared<MInode>();
        inode_ptr->inum = inum;
        inode_ptr->disk_inode = tmp_disk_inode;
        inode_ptr->refcnt = 1;
        inode_ptr->dirty = false;
        inode_ptr->valid = true;
        spin_lock_init(&inode_ptr->lock);

        cache.put(inum, inode_ptr);
        return inode_ptr;
    }
}

void mark_inode_dirty(std::shared_ptr<MInode>& inode)
{
    spin_lock(&inode->lock);
    inode->dirty = true;
    spin_unlock(&inode->lock);
}

bool flush_inode(std::shared_ptr<MInode>& inode) 
{
    spin_lock(&inode->lock);
    if (inode->refcnt > 0)
    {
        log_info("ERROR: flush an inode whose refcnt > 0");   // 理论上 refcnt 为 0 的 inode 会出现在链表尾部，若链表尾部的 refcnt 都不为 0，说明 cache 中所有 inode 的 refcnt 都不会 0，此时应该说是 cache 的容量小了。
        spin_unlock(&inode->lock);
        return false;
    }

    if (inode->dirty == false)
    {
        spin_unlock(&inode->lock);
        return true;
    }

    write_inode(inode->inum, &inode->disk_inode);
    inode->dirty = false;
    spin_unlock(&inode->lock);
    return true;
}

void ref_inode(std::shared_ptr<MInode>& inode) 
{
    spin_lock(&inode->lock);
    inode->refcnt++;
    spin_unlock(&inode->lock);
}

void release_inode(std::shared_ptr<MInode>& inode)     // 不使用的时候要及时release
{
    spin_lock(&inode->lock);
    inode->refcnt--;
    if (inode->refcnt == 0)
    {
        auto& cache = InodeCacheManager::instance();
        cache.move_to_end(inode->inum);
    }
    spin_unlock(&inode->lock);

    inode = nullptr;    // 将这个 inode pointer 置为 nullptr
}

void flush_dirty_inodes()
{
    auto& cache = InodeCacheManager::instance();
    cache.for_each_entry([](std::shared_ptr<MInode>& inode) {
        if (inode->dirty) 
        {
            write_inode(inode->inum, &inode->disk_inode);
            inode->dirty = false;
        }
    });
    log_info("flushed all the dirty inodes to the disk");
}