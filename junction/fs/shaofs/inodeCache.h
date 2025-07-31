#pragma once

#include "base.h"
#include "cache.h"
#include "inode.h"


using InodeCache         = LRUCache      <int, std::shared_ptr<MInode>>;
using InodeCacheManager  = CacheSingleton<int, std::shared_ptr<MInode>>;


int alloc_inode(file_type_t type, std::shared_ptr<MInode>& inode);
void free_inode(int inum);

void init_inode_cache(size_t capacity = DEFAULT_CACHE_SIZE);
std::shared_ptr<MInode> get_inode(int inum);
void mark_inode_dirty(std::shared_ptr<MInode>& inode);
void ref_inode(std::shared_ptr<MInode>& inode);
void release_inode(std::shared_ptr<MInode>& inode);
bool flush_inode(std::shared_ptr<MInode>& inode);
void flush_dirty_inodes();