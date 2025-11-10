#pragma once

#include "base.h"
#include "inode.h"
#include <memory>


typedef struct {
	int inum;
	file_type_t filetype;   // 4B
	char name[NAMESIZ];
	char pad[249];
} Dirent;


typedef struct {
	MInode* parent_ino;  				 // 父目录inode
	MInode* ino;                         // 成功找到的inode
    char last_name[MAX_PATH_LEN];        // 保存最后未找到的token 或成功找到时该 inode 在 dir 中的硬链接名
    int code;                            // -1表示失败，0表示完全匹配，1表示部分匹配
} IEntry;


int dir_lookup_locked(MInode* dir_inode, const char *name);
int dir_lookup(MInode* dir_inode, const char *name);
void add_dentry_locked(MInode* dir_inode, const char* name, int inum, file_type_t filetype);
void add_dentry(MInode* dir_inode, const char* name, int inum, file_type_t filetype);

void lookup(const char *pathname, IEntry& out_entry);
void delete_dentry(MInode*& dir_inode, char* name);