#include "inodeCache.h"
#include "disk.h"
#include "base.h"
#include "file.h"
#include "dentry.h"
#include "extent.h"


void* read_file_content(std::shared_ptr<MInode> inode)    // 读取 inode 对应若干个 LBA 中的数据，返回数据区的起始地址
{
	if (inode == NULL || inode->disk_inode.file_size == 0) return NULL;

	log_info("Reading file[%d] content[len: %lu] ...", inode->inum, inode->disk_inode.file_size);
	char* buffer = (char*)malloc(inode->disk_inode.file_size);
	if (!buffer) 
	{
        log_info("malloc failed in read_file_content");
        return NULL;
    }

	uint64_t remaining = inode->disk_inode.file_size, offset = 0;
	for (int i = 0; i < DIRECT_EXTENT_NUM && remaining > 0; i++)
	{
		iExtent* ext = &inode->disk_inode.direct_extents[i];
		if (ext->block_count == 0) continue;

		uint64_t copy_size = MIN(remaining, extent_size(ext));

		void* data = read_extent(ext, 0, copy_size);
		if (!data) 
		{
    		log_info("read_extent failed");
    		free(buffer);
    		return NULL;
		}
		
        memcpy(buffer + offset, data, copy_size);
		free(data);

		remaining -= copy_size;
        offset    += copy_size;
	}

	if (remaining > 0)    // 需要读取 indirect extent 
	{
		log_info("reading indirect extent ...");

        iExtent* indirect_extents = (iExtent*)malloc(BLOCK_SIZE);
		readObj(indirect_extents, BLOCK_SIZE, inode->disk_inode.indirect_extent_block, 1);
		
        int indirect_num = BLOCK_SIZE / sizeof(iExtent);
        for (int i = 0; i < indirect_num && remaining > 0; i++) 
		{
            iExtent* ext = &indirect_extents[i];
			if (ext->block_count == 0) continue;

			uint64_t copy_size = MIN(remaining, extent_size(ext));
			void* data = read_extent(ext, 0, copy_size);
            memcpy(buffer + offset, data, copy_size);
			free(data);

            offset    += copy_size;
            remaining -= copy_size;
        }
    }

	return buffer;
}

size_t append_content(std::shared_ptr<MInode> inode, const void *data, size_t siz)    // 将数据尾加到某个 inode 对应的文件中
{
    if (siz == 0) return 0;

	spin_lock(&inode->lock);
    uint64_t last_block   = inode->disk_inode.file_size / BLOCK_SIZE;    // 文件最后一块的逻辑号
    uint64_t block_offset = inode->disk_inode.file_size % BLOCK_SIZE;    // 文件内容在最后一块中的偏移

    iExtent *last_ext = NULL;   // 文件最后一块所属的 extent
    for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
    {
        iExtent *ext = &inode->disk_inode.direct_extents[i];
        if (ext->block_count == 0) continue;

        if (last_block >= ext->logical_start && last_block < ext->logical_start + ext->block_count) 
        {
            last_ext = ext;
            break;
        }
    }
    // TODO: 可能在间接块中

    uint64_t written = 0;

    if (last_ext != NULL)   // 先尝试往当前 extent 写入
    {
		log_info("write into the last extent");
		uint64_t extent_used_size;  // 该extent已使用的容量
		if (block_offset != 0) extent_used_size = (last_block - last_ext->logical_start) * BLOCK_SIZE + block_offset;
     	else                   extent_used_size = (last_block - last_ext->logical_start + 1) * BLOCK_SIZE;

		uint64_t remaining_bytes = extent_size(last_ext) - extent_used_size;
		written = MIN(siz, remaining_bytes);
		write_extent(last_ext, extent_used_size, data, written);
    }
	else log_info("This file has no extent yet.");

    if (written < siz)   // 没写完，分配新 extent（可能是这个文件的首个 extent，若last_ext==NULL，就说明当前文件为空）
    {
    	log_info("create a new extent");
		Extent ext;
		bool ret = alloc_extent(siz - written, &ext);   // 分配一个 extent
		if (!ret)
		{
			log_info("Error: fail to allocate a new extent");
			spin_unlock(&inode->lock);
			return -1;
		}

		iExtent* new_ext = NULL;
		for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
			if (inode->disk_inode.direct_extents[i].block_count == 0)
			{
				new_ext = &inode->disk_inode.direct_extents[i];
				new_ext->logical_start  = (last_ext ? last_ext->logical_start + last_ext->block_count : 0);   // 可能是该文件的首个extent
				new_ext->physical_start = ext.physical_start;
          		new_ext->block_count    = ext.block_count;
				break;
			}
		
		// TODO: 可能在间接块中

		if (!new_ext) 
		{
        	// TODO
    		log_warn("No free extent slot in inode!");
			spin_unlock(&inode->lock);
        	return -1;
    	}
		log_info("logical start: %lu, physical start: %lu, count: %lu", new_ext->logical_start, new_ext->physical_start, new_ext->block_count);

    	write_extent(new_ext, 0, (char*)data + written, siz - written);
    }

    inode->disk_inode.file_size += siz;
	inode->dirty = true;
	auto &cache = InodeCacheManager::instance();
    cache.put(inode->inum, inode); 
	spin_unlock(&inode->lock);
    return siz;
}

std::shared_ptr<MInode> create_file(std::shared_ptr<MInode> ino, const char* filename)   // 在目录 ino 下创建一个新文件，返回新创建的 inode
{
	if (!ino || ino->disk_inode.type != DIRECTORY)
	{
		log_info("Error: inode is not a directory.");
		return nullptr;
	}

	spin_lock(&ino->lock);    // 锁住目录 inode，因为当前线程需要修改该 inode 的内容

	Dirent* entries = (Dirent*)read_file_content(ino);
	
	if (entries == NULL) log_info("fail to read file[%d] content", ino->inum);
	else log_info("read file[%d] content OVER", ino->inum);

	int entry_count = ino->disk_inode.file_size / sizeof(Dirent);
	for (int i = 0; i < entry_count; i++)
		if (strcmp(entries[i].name, filename) == 0)
		{
			log_info("Error: file '%s' already exists.\n", filename);
			return nullptr;
		}

	std::shared_ptr<MInode> inode;
	int inum = alloc_inode(REGULAR, inode);     // 分配（并初始化）一个 inode
	if (inum == -1) 
	{
		log_info("Error: failed to allocate inode.");
		return nullptr;
	}
	else log_info("alloc a new inode: %d", inum);

	// 添加目录项
	Dirent new_ent = {.inum=inum, .filetype=inode->disk_inode.type};
	strncpy(new_ent.name, filename, DIRSIZ);
	append_content(ino, &new_ent, sizeof(Dirent));

	spin_unlock(&ino->lock);

	// flush_inode(inode);

  	log_info("successfully created a file!");
  	return inode;
}