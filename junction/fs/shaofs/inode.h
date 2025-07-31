#pragma once

#include "disk.h"

typedef struct {
    uint32_t    dev;
    int         inum;               // inode 号（与 idx 相同）
    DInode      disk_inode;         // 盘上结构
    int         refcnt;             // 使用计数
    bool        dirty;              // 是否修改过   
    bool        valid;              // 是否有效（已从盘上加载了数据）
    spinlock_t  lock;    
} MInode;

void read_inode(int idx, DInode *ino);
void write_inode(int idx, DInode *ino);