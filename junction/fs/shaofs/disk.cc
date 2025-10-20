#include "disk.h"
#include "base.h"
#include "blockCache.h"
#include "blockpool.h"

SuperBlock sb;

DEFINE_BITMAP(imap, INODENUM);
int imap_size = BITMAP_LONG_SIZE(INODENUM);

DEFINE_BITMAP(gmap, BLOCK_SIZE * 8);


void read_sb()
{
	log_info("Reading SuperBlock ...");
	readObj(&sb, sizeof(sb), 0, 1);
}

void read_bm(unsigned long* bm, u_int64_t nbits, BlockID blockstart, u_int64_t blockcount)  // 将盘上的 bitmap 存储到 bm 中（空间需提前申请）
{
	size_t bm_size = CEIL(nbits, 64) * sizeof(uint64_t);
	readObj(bm, bm_size, blockstart, blockcount);
}
void write_bm(unsigned long* bm, u_int64_t nbits, BlockID blockstart, u_int64_t blockcount) // 将 bm 中的数据写入到盘上
{
	size_t bm_size = CEIL(nbits, 64) * sizeof(uint64_t);
	writeObj(bm, bm_size, blockstart, blockcount);
}


uint64_t extent_size(const iExtent* ext) { return ext->block_count * BLOCK_SIZE; }


// 直接读取一个 extent 可以减少 IO 命令的数目，是否考虑优化下？（现在这个写法其实是逐块读取盘）
void read_extent(const iExtent *ext, uint64_t offset, char* buf, size_t size)   // 从磁盘上读取某个 extent 中从 offset 处长度为 size 的内容
{
    if (ext->block_count == 0 || size == 0 || buf == NULL) return;

	char* tmp = tmp_block_pool->alloc_block();
	size_t taken_size = 0;

	uint64_t start_block_idx =  offset             / BLOCK_SIZE;
	uint64_t end_block_idx   = (offset + size - 1) / BLOCK_SIZE;
    for (uint64_t i = start_block_idx; i <= end_block_idx; i++)        // 逐个读取 block
		if (i == start_block_idx)
		{
			read_block(ext->physical_start + i, tmp);

			size_t block_offset = offset % BLOCK_SIZE;
			size_t block_size = MIN(BLOCK_SIZE - block_offset, size);   // 考虑到只涉及 1 块的情况
			memcpy(buf + taken_size, tmp + block_offset, block_size);
			taken_size += block_size;
		}
		else if (i == end_block_idx)
		{
			read_block(ext->physical_start + i, tmp);

			size_t tail_size = (offset + size) % BLOCK_SIZE; 
			size_t block_size = tail_size > 0 ? tail_size : BLOCK_SIZE;
			memcpy(buf + taken_size, tmp, block_size);
			taken_size += block_size;
		}
		else
		{
			read_block(ext->physical_start + i, buf + taken_size);  // 从 cache 中读取 block
			taken_size += BLOCK_SIZE;
		}

	tmp_block_pool->free_block(tmp);

    // readObj(buf, size, ext->physical_start, ext->block_count);
}
void write_extent(const iExtent *ext, uint64_t offset, const void *data, size_t size)   // 从该 extent 的 offset（B）处起，写入 size 长度数据
{
	size_t total_capacity = extent_size(ext);    // 该 extent 的总容量
	if (total_capacity < offset + size) 
	{
		log_info("not enough space!");
		return;
	}

	char* tmp = tmp_block_pool->alloc_block();
	size_t taken_size = 0;

	uint64_t start_block_idx =  offset             / BLOCK_SIZE;     // 第一个字节所属的块
	uint64_t end_block_idx   = (offset + size - 1) / BLOCK_SIZE;     // 最后一个字节所属的块
	for (size_t i = start_block_idx; i <= end_block_idx; i++)
	{
		size_t block_offset = 0, block_size = BLOCK_SIZE;

		if (i == start_block_idx)
		{
			read_block(ext->physical_start + i, tmp);
			block_offset = offset % BLOCK_SIZE;
			block_size = MIN(BLOCK_SIZE - block_offset, size);   // 考虑到只涉及 1 块的情况
		}
		else if (i == end_block_idx)
		{
			read_block(ext->physical_start + i, tmp);
			size_t tail_size = (offset + size) % BLOCK_SIZE;
			if (tail_size > 0) block_size = tail_size;
		}


		if (data == NULL)   // 若 data 为 NULL，则置 0    （考虑用 spdk_nvme_ns_cmd_write_zeroes 来优化）
		{
			memset(tmp + block_offset, 0, block_size);
		}
		else
		{
			memcpy(tmp + block_offset, data + taken_size, block_size);
		}		

		write_block(ext->physical_start + i, tmp);

		taken_size += block_size;
	}

	tmp_block_pool->free_block(tmp);

	// char *buf = (char*)malloc(total_capacity);
	// if (buf == NULL) 
	// {
	// 	log_info("ERROR malloc");
	// 	return;
	// }
	// readObj(buf, total_capacity, ext->physical_start, ext->block_count);
	// memcpy(buf + offset, data, size);
	// writeObj(buf, total_capacity, ext->physical_start, ext->block_count);
	// free(buf);
}


void test_write_disk()
{
	char str[] = "abcdefg";
	uint64_t before_write = rdtsc();
	writeObj(str, 8, 1000010, 1);
	uint64_t after_write = rdtsc();
	log_info("[writeObj] duration: %lu us", (after_write - before_write) / cycles_per_us);
}
void test_read_disk()
{
	char str[10];
	uint64_t before_read = rdtsc();
	readObj(str, 8, 1000010, 1);
	uint64_t after_read = rdtsc();
	log_info("[readObj] duration: %lu us", (after_read - before_read) / cycles_per_us);
}