#pragma once

#include "base.h"
#include "inodeCache.h"


typedef struct {
	int inum;
	char name[DIRSIZ];
	file_type_t filetype;   // 4B
} Dirent;


typedef struct {
	std::shared_ptr<MInode> ino;     // 成功找到的inode，或者父目录inode
    char last_name[MAX_PATH_LEN];   // 保存最后未找到的token
    int code;                       // -1表示失败，0表示完全匹配，1表示部分匹配（此时ino为父目录结点）
} IEntry;



IEntry lookup(const char *pathname);