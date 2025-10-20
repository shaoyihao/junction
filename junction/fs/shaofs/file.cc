#include "inodeCache.h"
#include "blockCache.h"
#include "disk.h"
#include "base.h"
#include "file.h"
#include "dentry.h"
#include "extent.h"
#include <vector>

void write_file(MInode* ino, uint64_t oft, char* buf, uint64_t size)   // 从 oft 处开始覆盖写入长度为 size 的数据（inode 的锁应当在 caller 持有）
{
	// log_info("write_file() START");

	if (ino == nullptr || size == 0) return;

	uint64_t before_read_extents = rdtsc();
	DInode &di = ino->disk_inode;
	std::vector<iExtent, MyAllocator<iExtent>> exts;
	load_all_extents(di, exts);
	ensure_coverage(exts, CEIL(oft + size, BLOCK_SIZE));
	uint64_t after_read_extents = rdtsc();
	// log_info("[read extents] duration: %lu us", (after_read_extents - before_read_extents) / cycles_per_us);


	uint64_t before_write_extents = rdtsc();
	if (oft > di.file_size)   // 存在洞（oft > file_size），先进行零填充
	{
		uint64_t hole_len = oft - di.file_size;
		write_extentS(exts, di.file_size, NULL, hole_len);
	}

	// log_info("writing to extents of inode %d", ino->inum);
	write_extentS(exts, oft, buf, size);  // 执行数据写入
	// uint64_t after_write_extents = rdtsc();
	// log_info("[write extents] duration: %lu us", (after_write_extents - before_write_extents) / cycles_per_us);

    if (oft + size > di.file_size) di.file_size = oft + size;
	ino->dirty = true;


	// uint64_t before_store_extents = rdtsc();
    store_all_extents(di, exts);
	// uint64_t after_store_extents = rdtsc();
	// log_info("[store_all_extents] duration: %lu us", (after_store_extents - before_store_extents) / cycles_per_us);
	
	auto &cache = InodeCacheManager::instance();
	cache.put(ino->inum, ino); 

	// log_info("write_file() OVER");
}

void read_file(MInode* inode, uint64_t oft, void* buf, uint64_t size)  // 从 offset 处开始读取长度为 size 的数据，存入 buf 中
{
	// log_info("read_file() START");

	if (inode == nullptr || size == 0) return;
	DInode &di = inode->disk_inode;
	if (oft >= di.file_size) return;

	if (oft + size > di.file_size) size = di.file_size - oft;

	// log_info("Reading file[%d] content[%lu ~ %lu) ...", inode->inum, oft, oft + size);

	std::vector<iExtent, MyAllocator<iExtent>> exts;
	load_all_extents(di, exts);

	read_extentS(exts, oft, buf, size);

	// log_info("read_file() OVER");
}

void read_full_file(MInode* inode, void* buf)
{
	// log_info("read_full_file() START");

	if (inode == nullptr || buf == nullptr) return;
	read_file(inode, 0, buf, inode->disk_inode.file_size);

	// log_info("read_full_file() OVER");
}


// void* read_file_content(std::shared_ptr<MInode> inode)    // 读取 inode 对应若干个 LBA 中的数据，返回数据区的起始地址
// {
// 	if (inode == NULL || inode->disk_inode.file_size == 0) return NULL;

// 	log_info("Reading file[%d] content[len: %lu] ...", inode->inum, inode->disk_inode.file_size);
// 	char* buffer = (char*)malloc(inode->disk_inode.file_size);
// 	if (!buffer) 
// 	{
//         log_info("malloc failed in read_file_content");
//         return NULL;
//     }

// 	uint64_t remaining = inode->disk_inode.file_size, offset = 0;
// 	for (int i = 0; i < DIRECT_EXTENT_NUM && remaining > 0; i++)
// 	{
// 		iExtent* ext = &inode->disk_inode.direct_extents[i];
// 		if (ext->block_count == 0) continue;

// 		uint64_t copy_size = MIN(remaining, extent_size(ext));

// 		void* data = read_extent(ext, 0, copy_size);
// 		if (!data) 
// 		{
//     		log_info("read_extent failed");
//     		free(buffer);
//     		return NULL;
// 		}
		
//         memcpy(buffer + offset, data, copy_size);
// 		free(data);

// 		remaining -= copy_size;
//         offset    += copy_size;
// 	}

// 	if (remaining > 0)    // 需要读取 indirect extent 
// 	{
// 		log_info("reading indirect extent ...");

//         iExtent* indirect_extents = (iExtent*)malloc(BLOCK_SIZE);
// 		read_block(inode->disk_inode.indirect_extent_block, indirect_extents);
// 		// readObj(indirect_extents, BLOCK_SIZE, inode->disk_inode.indirect_extent_block, 1);
		
//         int indirect_num = BLOCK_SIZE / sizeof(iExtent);
//         for (int i = 0; i < indirect_num && remaining > 0; i++) 
// 		{
//             iExtent* ext = &indirect_extents[i];
// 			if (ext->block_count == 0) continue;

// 			uint64_t copy_size = MIN(remaining, extent_size(ext));
// 			void* data = read_extent(ext, 0, copy_size);
//             memcpy(buffer + offset, data, copy_size);
// 			free(data);

//             offset    += copy_size;
//             remaining -= copy_size;
//         }
//     }

// 	return buffer;
// }

void append_content(MInode* inode, const void *data, size_t siz)  // 将数据尾加到某个 inode 对应的文件中（inode 的锁应当在 caller 持有）
{
	// log_info("append_content() START");

	// uint64_t before_write_file = rdtsc();
	write_file(inode, inode->disk_inode.file_size, (char*)data, siz);
	// uint64_t after_write_file = rdtsc();
	// log_info("[write_file] duration: %lu us", (after_write_file - before_write_file) / cycles_per_us);

	// log_info("append_content() OVER");
}


// 其实不应该有这个函数，有个 write_file() 就够了其实
// size_t append_content(std::shared_ptr<MInode> inode, const void *data, size_t siz)    
// {
//     if (siz == 0) return 0;

// 	// spin_lock(&inode->lock);
//     uint64_t last_block   = inode->disk_inode.file_size / BLOCK_SIZE;    // 文件最后一块的逻辑号
//     uint64_t block_offset = inode->disk_inode.file_size % BLOCK_SIZE;    // 文件内容在最后一块中的偏移

//     iExtent *last_ext = NULL;   // 文件最后一块所属的 extent
//     for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
//     {
//         iExtent *ext = &inode->disk_inode.direct_extents[i];
//         if (ext->block_count == 0) continue;

//         if (last_block >= ext->logical_start && last_block < ext->logical_start + ext->block_count) 
//         {
//             last_ext = ext;
//             break;
//         }
//     }
//     // TODO: 可能在间接块中

//     uint64_t written = 0;

//     if (last_ext != NULL)   // 先尝试往当前 extent 写入
//     {
// 		log_info("write into the last extent");
// 		uint64_t extent_used_size;  // 该extent已使用的容量
// 		if (block_offset != 0) extent_used_size = (last_block - last_ext->logical_start) * BLOCK_SIZE + block_offset;
//      	else                   extent_used_size = (last_block - last_ext->logical_start + 1) * BLOCK_SIZE;

// 		uint64_t remaining_bytes = extent_size(last_ext) - extent_used_size;
// 		written = MIN(siz, remaining_bytes);
// 		write_extent(last_ext, extent_used_size, data, written);
//     }
// 	else log_info("This file has no extent yet.");

//     if (written < siz)   // 没写完，分配新 extent（可能是这个文件的首个 extent，若last_ext==NULL，就说明当前文件为空）
//     {
//     	log_info("create a new extent");
// 		Extent ext;
// 		bool ret = alloc_extent(CEIL(siz - written, BLOCK_SIZE), &ext);   // 分配一个 extent（要不要额外多分配一些块？）
// 		if (!ret)
// 		{
// 			log_info("Error: fail to allocate a new extent");
// 			spin_unlock(&inode->lock);
// 			return -1;
// 		}

// 		iExtent* new_ext = NULL;
// 		for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
// 			if (inode->disk_inode.direct_extents[i].block_count == 0)
// 			{
// 				new_ext = &inode->disk_inode.direct_extents[i];
// 				new_ext->logical_start  = (last_ext ? last_ext->logical_start + last_ext->block_count : 0);   // 可能是该文件的首个extent
// 				new_ext->physical_start = ext.physical_start;
//           		new_ext->block_count    = ext.block_count;
// 				break;
// 			}
		
// 		// TODO: 可能在间接块中

// 		if (!new_ext) 
// 		{
//         	// TODO
//     		log_warn("No free extent slot in inode!");
// 			// spin_unlock(&inode->lock);
//         	return -1;
//     	}
// 		log_info("logical start: %lu, physical start: %lu, count: %lu", new_ext->logical_start, new_ext->physical_start, new_ext->block_count);

//     	write_extent(new_ext, 0, (char*)data + written, siz - written);
//     }

//     inode->disk_inode.file_size += siz;
// 	inode->dirty = true;
// 	auto &cache = InodeCacheManager::instance();
//     cache.put(inode->inum, inode); 
// 	// spin_unlock(&inode->lock);
//     return siz;
// }

MInode* create_file(MInode* dirino, const char* filename, file_type_t filetype)   // 在目录 ino 下创建一个新文件，返回新创建的 inode
{
	SpinGuard g(&dirino->lock);  // 锁住目录 inode，因为当前线程需要修改该 inode 的内容

	if (!dirino || dirino->disk_inode.type != DIRECTORY)
	{
		log_info("Error: inode is not a directory.");
		return nullptr;
	}

	// Dirent* entries = (Dirent*)smalloc(dirino->disk_inode.file_size);
	char* raw_buffer = new char[dirino->disk_inode.file_size];
    Dirent* entries = reinterpret_cast<Dirent*>(raw_buffer);
	read_full_file(dirino, entries);
	// log_info("read file[%d] content OVER", dirino->inum);

	int entry_count = dirino->disk_inode.file_size / sizeof(Dirent);
	for (int i = 0; i < entry_count; i++)
		if (strcmp(entries[i].name, filename) == 0)
		{
			log_info("Error: '%s' already exists.\n", filename);
			// sfree(entries);
			delete[] raw_buffer;
			return nullptr;
		}
	// sfree(entries);
	delete[] raw_buffer;

	MInode* inode;
	int inum = alloc_inode(filetype, inode);     // 分配（并初始化）一个 inode
	if (inum == -1) 
	{
		log_info("Error: failed to allocate inode.");
		return nullptr;
	}
	// else log_info("alloc a new inode: %d", inum);

	// 添加目录项
	Dirent new_ent = {.inum=inum, .filetype=inode->disk_inode.type};
	strncpy(new_ent.name, filename, NAMESIZ - 1);
	new_ent.name[NAMESIZ - 1] = '\0';
	append_content(dirino, &new_ent, sizeof(Dirent));

	// flush_inode(inode);

  	// log_info("successfully created a file!");
  	return inode;
}


void truncate_inode_data_locked(MInode*& inode, uint64_t start_offset)   // 释放该 inode 拥有的数据块
{
	// log_info("truncate_inode_data() START");

	if (start_offset >= inode->disk_inode.file_size) return;

	DInode &di = inode->disk_inode;
	std::vector<iExtent, MyAllocator<iExtent>> exts;
	load_all_extents(di, exts);
	// log_info("extent num: %d", exts.size());

	uint64_t current_offset = 0;
	bool boundary_found = false;
	for (auto &e : exts)
	{
		uint64_t extent_size_bytes = extent_size(&e);
		if (boundary_found == false && start_offset < current_offset + extent_size_bytes)
		{
			boundary_found = true;  // 找到了边界 extent，它包含了 start_offset
			uint64_t offset_in_extent = start_offset - current_offset;     // 该 extent 中这些字节需要保留
			uint32_t blocks_to_keep = CEIL(offset_in_extent, BLOCK_SIZE);  // 这些字节占用的块数
			free_oneextent(e, blocks_to_keep);
		}
		else if (boundary_found) free_oneextent(e, 0);  // 这个 extent 完全位于截断点之后，需要整个释放

		current_offset += extent_size_bytes;
	}

	store_all_extents(di, exts);
	di.file_size = start_offset;
	inode->dirty = true;

	// log_info("truncate_inode_data() OVER");
}

void final_flush()
{
	// uint64_t before_flush = rdtsc();
	write_bm(imap, INODENUM, sb.imap_blockstart, sb.imap_blocknum);
    flush_dirty_inodes();
    flush_dirty_blocks();    // 需要放到最后
	// uint64_t after_flush = rdtsc();
	// log_info("[flush] duration: %lu us", (after_flush - before_flush) / cycles_per_us);
}