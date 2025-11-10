#pragma once

#include "base.h"
#include "LRUptr.h"
#include "inode.h"

using InodeCacheManager = ShardedLRUPtrSingleton<int, MInode>;


int alloc_inode(file_type_t type, MInode*& inode, int inum = -1);

void init_inode_cache(size_t capacity = DEFAULT_CACHE_SIZE);
MInode* get_inode(int inum);
void mark_inode_dirty(MInode* inode);
void ref_inode(MInode* inode);
void release_inode(MInode*& inode);
void unlink_inode(MInode* dirinode, char *name, MInode* inode);
void flush_inode(MInode* inode);
void flush_dirty_inodes();
void print_inode(MInode *inode);