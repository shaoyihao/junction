#include "group.h"
#include "base.h"
#include "disk.h"
#include "blockCache.h"

int core_to_group[1000];

void init_core_to_group()
{
	for (int i = 0; i < 1000; i++) core_to_group[i] = -1;
}

void get_group_by_gid(int groupid, BlockID* bitmap, BlockID* datablock_start)
{
	if (bitmap == nullptr || datablock_start == nullptr)
	{
		log_info("[Error] get_group_by_gid(): Invalid memory pointer");
		exit(1);
	}
	if (groupid < 0 || groupid >= sb.group_num) { log_info("Error: Invalid groupid %d\n", groupid); exit(1); }
	
	BlockID group0_start_block = sb.gmap_blockstart + sb.gmap_blocknum;
	BlockID group_start_block  = group0_start_block + (uint64_t)groupid * TOTALBLOCKS_PERGROUP;

	*bitmap          = group_start_block;
	*datablock_start = group_start_block + BMAPNUM_PERGROUP;
}

void get_group_by_blkid(BlockID blk, int* groupid, BlockID* bitmap, BlockID* datablock_start)  // 传入一个物理块号，获取该物理块所属的组的信息
{
	if (groupid == nullptr || bitmap == nullptr || datablock_start == nullptr)
	{
		log_info("[Error] get_group_by_blkid(): Invalid memory pointer");
		exit(1);
	}

	BlockID group0_start_block = sb.gmap_blockstart + sb.gmap_blocknum;
	if (blk < group0_start_block)
	{
		log_info("[Error] get_group_by_blkid(): BlockID %llu is before the start of group managed area.\n", blk);
		exit(1);
	}

	uint64_t blk_off = blk - group0_start_block;
	int gid = blk_off / TOTALBLOCKS_PERGROUP;
	if (gid >= sb.group_num) 
	{
        log_info("[Error] get_group_by_blkid(): Calculated groupid %d for blk %llu is out of bounds.\n", gid, blk);
        exit(1);
    }
	*groupid = gid;
	get_group_by_gid(gid, bitmap, datablock_start);
}

bool groupUsable(unsigned int coreid)
{
	if (core_to_group[coreid] == -1) return false;

	BlockID bm_start, datablock_start;
	get_group_by_gid(core_to_group[coreid], &bm_start, &datablock_start);


	// 因为 BMAPNUM_PERGROUP 一般为 1，所以直接读取一个 block 即可  （不过可能需要将代码写得更通用些，考虑到 BMAPNUM_PERGROUP 可能大于 1 的情况）
	BlockEntry* block;
	read_block(bm_start, block);
	unsigned long* bm = reinterpret_cast<unsigned long*>(block->data);
	if (bitmap_popcount(bm, DATABLOCKS_PERGROUP) == DATABLOCKS_PERGROUP) return false;
	return true;


	// // unsigned long bm[BLOCK_SIZE / sizeof(unsigned long)];     // 每次都读一下，会不会有点慢？考虑是否再维护一个 group descriptor 数组
	// size_t bm_size_bytes = BMAPNUM_PERGROUP * BLOCK_SIZE;
    // // unsigned long* bm = (unsigned long*)smalloc(bm_size_bytes);
	// char* raw_buffer = new char[bm_size_bytes];
    // unsigned long* bm = reinterpret_cast<unsigned long*>(raw_buffer);
    // if (!bm) 
    // {
    //     log_info("[ERROR] new() failed in alloc_extents_from_group");
    //     return 0;
    // }
	// read_block(bm_start, bm);
	// if (bitmap_popcount(bm, DATABLOCKS_PERGROUP) == DATABLOCKS_PERGROUP) return false;

	// delete[] raw_buffer;
	// return true;
}

bool set_newgroup(unsigned int coreid)
{
	for (int idx = 0; idx < sb.group_num; idx++)
        if (!bitmap_atomic_test_and_set(gmap, idx))
		{
			core_to_group[coreid] = idx;
			return true;
		}
		
	return false;
}