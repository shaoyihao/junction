#ifndef SHAO
#define SHAO

#include <stdint.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../runtime/defs.h"
#include "base/lock.h"
}



void read_inode(int idx, Inode *ino);
size_t append_content(Inode *inode, const void *data, size_t siz);
void allocBlocks(uint64_t k, uint64_t blocks[]);
void* read_extent_content(Extent *ext);
void* read_file_content(Inode *inode);
IEntry lookup(const char *pathname);
void block_pool_create(struct LBAPool* q);



#endif