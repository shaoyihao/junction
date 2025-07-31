#include "disk.h"
#include "base.h"
#include "blockCache.h"

SuperBlock sb;
u_int64_t imap[CEIL(INODENUM, 64)];
int imap_size = CEIL(INODENUM, 64);
std::mutex imap_locks[CEIL(INODENUM, 64)];   // 对 inode bitmap 分段的细粒度锁

void read_sb()
{
	log_info("Reading SuperBlock ...");
	readObj(&sb, sizeof(sb), 0, 1);
}

void read_imap(u_int64_t *bm)       // 将盘上 imap 数据存储到 bm 中（空间需提前申请）
{
	log_info("Reading Inode Bitmap ...");
	size_t bm_size = CEIL(INODENUM, 64) * sizeof(uint64_t);
	readObj(bm, bm_size, sb.imap_blockstart, sb.imap_blocknum);
}
void write_imap(u_int64_t *bm)      // 将 bm 中的数据写入到盘上 imap
{
	log_info("Updating Inode Bitmap ...");
	size_t bm_size = CEIL(INODENUM, 64) * sizeof(uint64_t);
	writeObj(bm, bm_size, sb.imap_blockstart, sb.imap_blocknum);
}



uint64_t extent_size(const iExtent* ext)
{
	return ext->block_count * BLOCK_SIZE;
}


void* read_extent(const iExtent *ext)   // 从磁盘上读取一个 extent 的内容
{
    if (ext->block_count == 0) return NULL;

    size_t size = extent_size(ext);
    char* buf = (char*)malloc(size);
    if (!buf) 
	{ 
		log_info("malloc failed in read_extent"); 
		return NULL; 
	}

	// size_t total_size = 0;
    // for (size_t i = 0; i < ext->block_count; i++)        // 逐个读取 block
	// {
    //     read_block(ext->physical_start + i, buf + total_size);  // 从 cache 中读取 block
    //     total_size += BLOCK_SIZE;
    // }

    readObj(buf, size, ext->physical_start, ext->block_count);

    return buf; // 记得外部 free()
}
void write_extent(const iExtent *ext, uint64_t offset, const void *data, size_t size)   // 从该 extent 的 offset（B）处起，写入 size 长度数据
{
	auto& cache = BlockCacheManager::instance();

	size_t total_capacity = ext->block_count * BLOCK_SIZE;    // 该 extent 的总容量

	if (total_capacity - offset < size) 
	{
		log_info("not enough space!");
		return;
	}

	// uint64_t start_block_idx = offset / BLOCK_SIZE;
	// uint64_t end_block_idx = (offset + size - 1) / BLOCK_SIZE;
	// for (size_t i = start_block_idx; i <= end_block_idx; i++)
	// {
	// 	std::shared_ptr<BlockEntry> block = std::make_shared<BlockEntry>();
	// 	block->lba = ext->physical_start + i;
	// 	block->dirty = true;
	// 	char* buf = block->data;

	// 	size_t block_offset = 0, block_size = BLOCK_SIZE;

	// 	if (i == start_block_idx)
	// 	{
	// 		read_block(ext->physical_start + i, buf);
	// 		block_offset = offset % BLOCK_SIZE;
	// 		block_size = MIN(BLOCK_SIZE - block_offset, size);   // 考虑到只涉及 1 块的情况
	// 	}
	// 	else if (i == end_block_idx)
	// 	{
	// 		read_block(ext->physical_start + i, buf);
	// 		size_t tail_size = (offset + size) % BLOCK_SIZE;
	// 		if (tail_size > 0) block_size = tail_size;
	// 	}

	// 	memcpy(buf + block_offset, data, block_size);

	// 	cache.put(block->lba, block);

	// 	data = (const char*)data + block_size;
	// 	size -= block_size;
	// }

	char *buf = (char*)malloc(total_capacity);
	readObj(buf, total_capacity, ext->physical_start, ext->block_count);
	memcpy(buf + offset, data, size);
	writeObj(buf, total_capacity, ext->physical_start, ext->block_count);
	free(buf);
}