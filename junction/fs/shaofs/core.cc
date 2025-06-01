#include "fshao.h"
#include "dentryCacheManager.h"
#include <string>
#include <vector>

extern "C" {
#include "base/log.h"
}

// void read_superblock(SuperBlock *sb)   // 将 superblock 数据存储到 sb 中（空间需提前申请）
// {
// 	log_info("Reading SuperBlock ...\n");
// 	readObj(sb, sizeof(SuperBlock), 0, 1);
// 	log_info("END.\n");
// }
void read_inode(int idx, Inode *ino)    // 将盘上第 idx 个 inode 的数据写到 ino 中（空间需提前申请）
{
	// SuperBlock sb;
	// read_superblock(&sb);

	u_int64_t blockidx = sb.inode_table_block_start + idx / INODENUM_PER_BLOCK;
	Inode inode_tbl[INODENUM_PER_BLOCK];
	
	log_info("Reading Inode %4d ...\n", idx);
	readObj(inode_tbl, sizeof(inode_tbl), blockidx, 1);
	log_info("END.\n");
	int idx2 = idx % INODENUM_PER_BLOCK;
	*ino = inode_tbl[idx2];
}
void* read_extent_content(Extent *ext)    // 读取 ext 对应若干个 LBA 中的数据，返回数据区的起始地址
{
	if (ext->block_count == 0) return NULL;

	size_t siz = ext->block_count * LBA_SIZE;
	void* data = malloc(siz);
	readObj(data, siz, ext->physical_start, ext->block_count);
	return data;
}
inline uint64_t extent_size(Extent *ext)
{
	return ext->block_count * LBA_SIZE;
}
void* read_file_content(Inode *inode)    // 读取 inode 对应若干个 LBA 中的数据，返回数据区的起始地址
{
	if (inode == NULL) return NULL;

	log_info("Reading file[%lu] content...\n", inode->idx);
	char* buffer = (char*)malloc(inode->file_size);

	uint64_t remaining = inode->file_size, offset = 0;
	for (int i = 0; i < DIRECT_EXTENT_NUM && remaining > 0; i++)
	{
		Extent *ext = &inode->direct_extents[i];
		if (ext->block_count == 0) continue;
		void* data = read_extent_content(ext);

		uint64_t copy_size = MIN(remaining, extent_size(ext));
    memcpy(buffer + offset, data, copy_size);
		free(data);

		remaining -= copy_size;
    offset    += copy_size;
	}

	if (remaining > 0 && inode->indirect_extent_block != 0) 
	{
    Extent* indirect_extents = (Extent*)malloc(LBA_SIZE);
		readObj(indirect_extents, LBA_SIZE, inode->indirect_extent_block, 1);
		
    int indirect_num = LBA_SIZE / sizeof(Extent);
    for (int i = 0; i < indirect_num && remaining > 0; i++) 
		{
      Extent *ext = &indirect_extents[i];
			if (ext->block_count == 0) continue;
			void* data = read_extent_content(ext);

      uint64_t copy_size = MIN(remaining, extent_size(ext));
      memcpy(buffer + offset, data, copy_size);
			free(data);

      offset += copy_size;
      remaining -= copy_size;
    }
  }
	log_info("END.\n");

	return buffer;
}

std::string join_path(const std::vector<std::string>& parts, int count) 
{
    if (count == 0) return "/";

    std::string path;
    for (int i = 0; i < count; ++i) path = path + "/" + parts[i];
    return path;
}

IEntry lookup(const char *pathname)
{
  IEntry res;
  PathCache& cache = PathCacheManager::instance();

  char path_copy[MAX_PATH_LEN];
  strncpy(path_copy, pathname, MAX_PATH_LEN);

  std::vector<std::string> parts;    // 对 pathname 进行拆分
  char* token = strtok(path_copy, "/");
  while (token) 
  { 
    parts.push_back(token); 
    token = strtok(NULL, "/"); 
  }

  int prefix_hit_index = -1;  // 表示命中到哪一层（parts.size() 表示完整路径，0 表示 "/"）
  Inode* base_inode = nullptr;
  for (int i = parts.size(); i >= 0; --i)    // 从完整路径开始递减搜索
  {
    std::string probe_path = join_path(parts, i);
    if (cache.get(probe_path.c_str(), base_inode))  // cache hit
    {
      log_info("cache hit!!!");
      prefix_hit_index = i;
      break;
    }
  }

  if (prefix_hit_index == -1)   // 根目录都没命中，手动读出来
  {   
      base_inode = new Inode;
      read_inode(ROOT_INO, base_inode);
      cache.put("/", base_inode);  // 存入 cache 中
      prefix_hit_index = 0;
  }

  // 基于 base_inode 搜索完整路径对应的 Inode
  Inode* current_inode = base_inode;
  for (int i = prefix_hit_index; i < parts.size(); ++i)    // 在 dentries 中搜索 parts[i]
  {
      if (current_inode->type != DIRECTORY) 
      {
          res.code = -1;
          res.ino = current_inode;
          strncpy(res.last_name, parts[i].c_str(), MAX_PATH_LEN);
          return res;
      }

      Dirent* entries = (Dirent*)read_file_content(current_inode);
      int entry_count = current_inode->file_size / sizeof(Dirent);
      bool found = false;
      uint64_t target_inum;
      for (int j = 0; j < entry_count; ++j) 
          if (strcmp(entries[j].name, parts[i].c_str()) == 0) 
          {
              target_inum = entries[j].inum;
              found = true;
              break;
          }

      if (found == false)   // 该目录下不存在 parts[i]
      {
        if (i == parts.size() - 1) res.code = 1;   // 仅是最后一个token不匹配（可能是新建文件）
        else                       res.code = -1;  // 不合法路径
        res.ino = current_inode;
        strncpy(res.last_name, parts[i].c_str(), MAX_PATH_LEN);
        return res;
      }

      // 该目录下存在 parts[i]，向下一级
      current_inode = new Inode;
      read_inode(target_inum, current_inode);
      std::string full_path = join_path(parts, i + 1);  // 构造完整路径
      cache.put(full_path.c_str(), current_inode);      // 存入 cache 中
  }

  // 完全匹配
  res.code = 0;
  res.ino = current_inode;
  return res;
}

void read_imap(u_int64_t *bm)       // 将盘上 imap 数据存储到 bm 中（空间需提前申请）
{
	log_info("Reading Inode Bitmap...\n");
	readObj(bm, DIV_UP(INODENUM, 64) * sizeof(u_int64_t), sb.imap_block_start, sb.imap_block_num);
	log_info("END.\n");
}
void write_imap(u_int64_t *bm)      // 将 bm 中的数据写入到盘上 imap
{
	log_info("Updating Inode Bitmap...\n");
	writeObj(bm, DIV_UP(INODENUM, 64) * sizeof(u_int64_t), sb.imap_block_start, sb.imap_block_num, 1);
	log_info("END.\n");
}
int alloc_inode()                   // 分配空闲inode并更新 inode bitmap
{
	u_int64_t bm[DIV_UP(INODENUM, 64)];
	read_imap(bm);
	// print_bitmap(bm, INODENUM);
	int bm_size = DIV_UP(INODENUM, 64);
	for (int i = 0; i < bm_size; i++)
	{
		if (bm[i] == UINT64_MAX) continue;  // 没有空位

		for (int bit = 0; bit < 64; bit++)
		{
			if ((bm[i] & ((uint64_t)1 << bit)) == 0)  // 找到一个空闲 inode
			{
				bm[i] |= ((uint64_t)1 << bit);
				int idx = i * 64 + bit;
				if (idx >= INODENUM) return -1;
				else 
				{
					write_imap(bm);    // 写回 imap
					return idx;
				}
			}
		}
	}
	return -1;
}
void free_inode(int ino)
{
	if (ino < 0 || ino >= INODENUM) return;

	int idx    = ino / 64;
  int offset = ino % 64;

	u_int64_t bm[DIV_UP(INODENUM, 64)];
	read_imap(bm);
	bm[idx] &= ~((uint64_t)1 << offset);
	write_imap(bm);
}
void write_inode(int idx, Inode *ino)    // 将 ino 的数据写到盘上第 idx 个 inode 中
{
	u_int64_t blockidx = sb.inode_table_block_start + idx / INODENUM_PER_BLOCK;
	Inode inode_tbl[INODENUM_PER_BLOCK];
	readObj(inode_tbl, sizeof(inode_tbl), blockidx, 1);
	int idx2 = idx % INODENUM_PER_BLOCK;
	inode_tbl[idx2] = *ino;

	log_info("Writing Inode %4d ...", idx);
	writeObj(inode_tbl, sizeof(inode_tbl), blockidx, 1, 1);
	log_info("END.");
}

void write_to_extent(const Extent *ext, uint64_t offset, const void *data, size_t size)
{
	size_t total_capacity = ext->block_count * LBA_SIZE;

	if (total_capacity - offset < size) 
	{
		log_info("not enough space\n");
		return;
	}

	char *buf = (char*)malloc(total_capacity);
	readObj(buf, total_capacity, ext->physical_start, ext->block_count);
	memcpy(buf + offset, data, size);

  // Dirent *d = (Dirent *)buf;
  // for (int i = 0; i < 3; i++)
  // {
  //   Dirent ent = d[i];
  //   log_info("%-15d %-20s %-10lu\n", ent.filetype, ent.name, ent.inum);
  // }

	writeObj(buf, total_capacity, ext->physical_start, ext->block_count, 1);
	free(buf);
}
// void append_content(Inode *inode, void *data, size_t siz)
// {
// 	uint64_t last_block   = inode->file_size / LBA_SIZE;      // 文件最后一块的逻辑号
// 	uint64_t block_offset = inode->file_size % LBA_SIZE;      // 文件内容在最后一块中的偏移

// 	Extent* last_ext = NULL;   // 文件最后一块所属的 extent
// 	for (int i = 0; i < DIRECT_EXTENT_NUM; i++)
// 	{
// 		Extent *ext = &inode->direct_extents[i];
// 		if (ext->block_count == 0) continue; 

// 		if (last_block >= ext->logical_start && last_block < ext->logical_start + ext->block_count) 
// 		{
// 			last_ext = ext;
// 			break;
// 		}
// 	}

// 	uint64_t extent_used_size;  // 该extent已使用的容量
// 	if (block_offset != 0) extent_used_size = (last_block - last_ext->logical_start) * LBA_SIZE + block_offset;
// 	else                   extent_used_size = (last_block - last_ext->logical_start + 1) * LBA_SIZE;

//   log_info("used_size: %lu", extent_used_size);

// 	uint64_t remaining_bytes = last_ext->block_count * LBA_SIZE - extent_used_size;
// 	uint64_t writesize = MIN(siz, remaining_bytes);
// 	write_to_extent(last_ext, extent_used_size, data, writesize);
// 	if (writesize != siz)   // 开辟一个新的extent
// 	{
		
// 	}

// 	inode->file_size += siz;
// 	write_inode(inode->idx, inode);
// }
void allocBlocks(uint64_t k, uint64_t blocks[])   // 将分配的空闲块号存于 blocks 中
{
	FreeBlockStk stk;
	log_info("Reading FreeBlockStk ...\n");
	readObj(&stk, sizeof(FreeBlockStk), sb.free_stk_block_start, sb.free_stk_block_num);
	log_info("END.\n");

	nextGroup g;
	for (int i = 0; i < k; i++)
	{
		if (stk.top == 1) readObj(&g, sizeof(g), stk.blocks[0], 1);
		blocks[i] = stk.blocks[stk.top - 1];
		log_info("alloc block %lu\n", blocks[i]);
		stk.top--;
		if (stk.top == 0)
		{
			if (g.cnt == -1) 
			{
				// TODO
				log_info("All blocks have been used once. Need Reformat.\n");
				return;
			}

			for (int j = g.cnt - 1; j >= 0; j--)
				stk.blocks[stk.top++] = g.blocks[j];
		}
	}

	log_info("Write FreeBlockStk Back ...");
	writeObj(&stk, sizeof(stk), sb.free_stk_block_start, sb.free_stk_block_num, 1);
	log_info("END.");
}

size_t append_content(Inode *inode, const void *data, size_t siz)
{
    if (siz == 0) return 0;

    uint64_t last_block   = inode->file_size / LBA_SIZE;    // 文件最后一块的逻辑号
    uint64_t block_offset = inode->file_size % LBA_SIZE;    // 文件内容在最后一块中的偏移

    Extent *last_ext = NULL;   // 文件最后一块所属的 extent
    for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
    {
        Extent *ext = &inode->direct_extents[i];
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
      if (block_offset != 0) extent_used_size = (last_block - last_ext->logical_start) * LBA_SIZE + block_offset;
     	else                   extent_used_size = (last_block - last_ext->logical_start + 1) * LBA_SIZE;
      // log_info("used_size: %lu", extent_used_size);
      uint64_t remaining_bytes = last_ext->block_count * LBA_SIZE - extent_used_size;
      written = MIN(siz, remaining_bytes);
      write_to_extent(last_ext, extent_used_size, data, written);
    }

    if (last_ext == NULL) log_info("This file has no extent yet.");

    if (written < siz)   // 没写完，分配新 extent（可能是这个文件的首个 extent，若last_ext==NULL，就说明当前文件为空）
    {
      log_info("create a new extent");
      uint64_t blocks_needed = DIV_UP(siz - written, LBA_SIZE);
      uint64_t blocks_alloc  = ALIGN(blocks_needed, EXTENT_BLOCK_MIN_NUM);
      log_info("need %lu, alloc %lu", blocks_needed, blocks_alloc);

      u_int64_t blocks[blocks_alloc];
      allocBlocks(blocks_alloc, blocks);

      Extent* new_ext = NULL;
      for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
      {
        if (inode->direct_extents[i].block_count == 0) 
        {
          new_ext = &inode->direct_extents[i];  // 选择一个未使用的extent
          new_ext->logical_start  = (last_ext ? last_ext->logical_start + last_ext->block_count : 0);   // 可能是该文件的首个extent
          new_ext->physical_start = blocks[0];
          new_ext->block_count    = blocks_alloc;
          break;
        }
      }
      // TODO: 可能在间接块中

      if (!new_ext) 
      {
        // TODO
        log_warn("No free extent slot in inode!");
        return -1;
      }
      log_info("logical start: %lu, physical start: %lu, count: %lu", new_ext->logical_start, new_ext->physical_start, new_ext->block_count);

      write_to_extent(new_ext, 0, (char*)data + written, siz - written);

      // char* written_data = (char*)read_extent_content(new_ext);
      // log_info("data: %s", written_data);
    }

    inode->file_size += siz;
    // log_info("file size: %lu", inode->file_size);
	  write_inode(inode->idx, inode);
    return siz;
}

void create_file(Inode *ino, const char *filename)   // 在目录 ino 下创建一个新文件
{
  if (ino->type != DIRECTORY) return;

  // 分配 inode
  int inum = alloc_inode();    
  log_info("new ino num: %d", inum);
  Inode inode = {.idx=inum, .used=1, .type=REGULAR, .file_size=0, .indirect_extent_block=0};
  write_inode(inum, &inode);

  // 添加目录项
  Dirent new_ent = {.inum=inum, .filetype=inode.type};
  strncpy(new_ent.name, filename, DIRSIZ);
  append_content(ino, &new_ent, sizeof(Dirent));

  log_info("successfully created a file!");
}

void print_file_content(Inode *ino)
{

}