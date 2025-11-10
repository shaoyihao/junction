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
// void read_extent(const iExtent *ext, uint64_t offset, char* buf, size_t size)   // 从磁盘上读取某个 extent 中从 offset 处长度为 size 的内容
// {
//     if (ext->block_count == 0 || size == 0 || buf == NULL) return;

// 	log_info("[read_extent()] START");

// 	thread_t *th = thread_self();
// 	uint64_t before_read_extent_tsc = rdtsc();
// 	uint64_t before_read_extent = thread_get_total_cycles(th) / cycles_per_us;

// 	char* tmp = tmp_block_pool->alloc_block();
// 	size_t taken_size = 0;

// 	uint64_t start_block_idx =  offset             / BLOCK_SIZE;
// 	uint64_t end_block_idx   = (offset + size - 1) / BLOCK_SIZE;
//     for (uint64_t i = start_block_idx; i <= end_block_idx; i++)        // 逐个读取 block
// 		if (i == start_block_idx)
// 		{
// 			read_block(ext->physical_start + i, tmp);

// 			size_t block_offset = offset % BLOCK_SIZE;
// 			size_t block_size = MIN(BLOCK_SIZE - block_offset, size);   // 考虑到只涉及 1 块的情况
// 			memcpy(buf + taken_size, tmp + block_offset, block_size);
// 			taken_size += block_size;
// 		}
// 		else if (i == end_block_idx)
// 		{
// 			read_block(ext->physical_start + i, tmp);

// 			size_t tail_size = (offset + size) % BLOCK_SIZE; 
// 			size_t block_size = tail_size > 0 ? tail_size : BLOCK_SIZE;
// 			memcpy(buf + taken_size, tmp, block_size);
// 			taken_size += block_size;
// 		}
// 		else
// 		{
// 			read_block(ext->physical_start + i, buf + taken_size);  // 从 cache 中读取 block
// 			taken_size += BLOCK_SIZE;
// 		}

// 	tmp_block_pool->free_block(tmp);

// 	uint64_t after_read_extent = thread_get_total_cycles(th) / cycles_per_us;
// 	uint64_t after_read_extent_tsc = rdtsc();
// 	log_info("[read_extent()] duration: %lu us, actual time: %lu us", (after_read_extent_tsc - before_read_extent_tsc) / cycles_per_us, after_read_extent - before_read_extent);
//     // readObj(buf, size, ext->physical_start, ext->block_count);
// }

void whattoread(uint64_t current_idx, uint64_t start_block_idx, uint64_t end_block_idx, uint64_t offset, size_t size, size_t& block_offset, size_t& block_size)
{
	block_offset = 0;
	block_size   = BLOCK_SIZE;

    if (current_idx == start_block_idx)    // 首块
    {   
        block_offset = offset % BLOCK_SIZE;
        block_size = MIN(BLOCK_SIZE - block_offset, size);   // 考虑到只涉及 1 块的情况
    }
    else if (current_idx == end_block_idx) // 尾块
    {
        size_t tail_size = (offset + size) % BLOCK_SIZE;
        block_size = tail_size > 0 ? tail_size : BLOCK_SIZE;
    }
}

void read_extent(const iExtent *ext, uint64_t offset, char* buf, size_t size)
{
    if (ext == nullptr || ext->block_count == 0 || size == 0 || buf == NULL) return;

    auto& cache = BlockCacheManager::instance();
    size_t taken_size = 0;       // 已经复制到 buf 中的字节数

    uint64_t start_block_idx = offset / BLOCK_SIZE;
    uint64_t   end_block_idx = (offset + size - 1) / BLOCK_SIZE;

    uint64_t current_idx = start_block_idx;
    while (current_idx <= end_block_idx)
    {
        uint64_t current_lba = ext->physical_start + current_idx;
        BlockEntry* block = cache.get_or_create(current_lba);
		spin_lock(&block->mtx);
        if (block->valid)    // Cache 命中
        {
            size_t of, sz;
            whattoread(current_idx, start_block_idx, end_block_idx, offset, size, of, sz);  // 计算需要拷贝的块的内容
            memcpy(buf + taken_size, (char*)block->data + of, sz);
            taken_size += sz;
            spin_unlock(&block->mtx);
            current_idx++; // 处理下一个 block
        }
        else    // Cache 未命中
        {
            // 我们在 current_idx 处发现了一个 invalid block，并且我们正持有它的锁。现在向后扫描，找到所有连续的 invalid block，并锁住它们。
            std::vector<BlockEntry*> miss_run_blocks;
            miss_run_blocks.push_back(block); // 第一个 miss 的块（已锁住）

            uint64_t run_start_idx = current_idx;
            uint64_t run_start_lba = current_lba;
            uint32_t run_length = 1;

            uint64_t scan_idx = current_idx + 1;
            while (scan_idx <= end_block_idx)
            {
                uint64_t scan_lba = ext->physical_start + scan_idx;
                BlockEntry* scan_block = cache.get_or_create(scan_lba);

                spin_lock(&scan_block->mtx);
                
                if (scan_block->valid)  // 这是一个 valid block，"run" 在此结束
                {
                    spin_unlock(&scan_block->mtx);
                    break;
                }
                
                miss_run_blocks.push_back(scan_block);  // 这是一个 invalid block，将其添加到 run 中
                run_length++;
                scan_idx++;
            }
            // 此时，我们持有了 run 中所有 block 的锁
            
            size_t run_size_bytes = (size_t)run_length * BLOCK_SIZE;
            char* run_buffer = new(std::nothrow) char[run_size_bytes]; 
            if (!run_buffer)   // 错误处理：释放所有锁并中止（或者回退到逐块读取）
            {
                log_err("Failed to allocate run buffer for extent read");
                for (BlockEntry* b : miss_run_blocks) spin_unlock(&b->mtx);
                return; 
            }

            readObj(run_buffer, run_size_bytes, run_start_lba, run_length);  // 执行批量磁盘读取 (在 I/O 期间持有所有锁)

            // 填充 cache 并拷贝到用户 buf (我们仍持有锁)
            for (uint32_t k = 0; k < run_length; k++)
            {
                BlockEntry* block_to_fill = miss_run_blocks[k];
                uint64_t block_idx_in_extent = run_start_idx + k;

                // 填充 cache
                memcpy(block_to_fill->data, run_buffer + k * BLOCK_SIZE, BLOCK_SIZE);
                block_to_fill->dirty = false;
                block_to_fill->valid = true;

                // 拷贝到用户 buf
                size_t of, sz;
                whattoread(block_idx_in_extent, start_block_idx, end_block_idx, offset, size, of, sz);
                memcpy(buf + taken_size, run_buffer + k * BLOCK_SIZE + of, sz);
                taken_size += sz;

                spin_unlock(&block_to_fill->mtx);
            }

            delete[] run_buffer;

            current_idx += run_length;
        } 
    }
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