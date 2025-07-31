#pragma once
#include "inodeCache.h"

void* read_file_content(std::shared_ptr<MInode> inode);
std::shared_ptr<MInode> create_file(std::shared_ptr<MInode> ino, const char* filename);
size_t append_content(std::shared_ptr<MInode> inode, const void *data, size_t siz);