#pragma once
#include "base.h"

extern int core_to_group[];

void init_core_to_group();
bool groupValid(unsigned int coreid);
bool set_newgroup(unsigned int coreid);
void get_group_by_gid(int groupid, BlockID* bitmap, BlockID* datablock_start);
void get_group_by_blkid(BlockID blk, int* groupid, BlockID* bitmap, BlockID* datablock_start);