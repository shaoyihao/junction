#pragma once
#include "inodeCache.h"

void write_file(MInode* ino, uint64_t oft, char* buf, uint64_t size);
void read_file(MInode* inode, uint64_t oft, void* buf, uint64_t size);
void read_full_file(MInode* inode, void* buf);
MInode* create_file(MInode* ino, const char* filename, file_type_t filetype);
void append_content(MInode* inode, const void *data, size_t siz);
void truncate_inode_data_locked(MInode*& inode, uint64_t start_offset);


void final_flush();