#include "disk.h"
#include "blockCache.h"

void read_inode(int idx, DInode *ino)    // 将盘上第 idx 个 inode 的数据写到 ino 中（空间需提前申请）
{
	if (idx < 0 || idx >= sb.inode_num) 
	{
    	log_info("Error: inode index %d out of bounds (max = %d)\n", idx, sb.inode_num - 1);
    	exit(1);
	}

	BlockID blockidx = sb.itable_blockstart + idx / INODENUM_PER_BLOCK;
	int idx2 = idx % INODENUM_PER_BLOCK;

	char* data = (char*)malloc(BLOCK_SIZE);
	read_block(blockidx, data);
    DInode* inode_tbl = reinterpret_cast<DInode*>(data);
	*ino = inode_tbl[idx2];
	free(data);
}
void write_inode(int idx, DInode *ino)    // 将 ino 的数据写到盘上第 idx 个 inode 中
{
    if (idx < 0 || idx >= sb.inode_num) 
	{
    	log_info("Error: inode index %d out of bounds (max = %d)\n", idx, sb.inode_num - 1);
    	exit(1);
	}

	u_int64_t blockidx = sb.itable_blockstart + idx / INODENUM_PER_BLOCK;
    int idx2 = idx % INODENUM_PER_BLOCK;
	
    char* data = (char*)malloc(BLOCK_SIZE);
	read_block(blockidx, data);
    DInode* inode_tbl = reinterpret_cast<DInode*>(data);
    if (inode_tbl[idx2].idx != idx) 
    {
        log_info("Warning: overwriting mismatched inode (expected %d, got %d)", idx, inode_tbl[idx2].idx);
        return;
    }

	inode_tbl[idx2] = *ino;
	write_block(blockidx, (char*)inode_tbl);
}
