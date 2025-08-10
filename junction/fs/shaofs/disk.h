#pragma once
#include "base.h"
// #include "storage.h"
#include <cstdint>
#include <mutex>


#define INODENUM_PER_BLOCK    16     // block_size / inode_size


typedef struct {
	uint32_t       magic_number;                 // FSMAGIC=0x0517
	uint32_t       block_size;                   // 每块（LBA）的大小（B）
    uint64_t       total_blocknum;               // 总块数
	int            inode_size;                   // inode 大小
	int            inode_num;                    // inode 数目

	BlockID        imap_blockstart;              // inode bitmap 起始块
	uint64_t       imap_blocknum;                // inode bitmap 占用的块数

	BlockID        itable_blockstart;            // inode table 起始块
	uint64_t       itable_blocknum;              // inode table 占用的块数

	BlockID        indirect_block_start;         // 第一个 indirect extent block
	uint64_t       indirect_block_num;           // 等于 inode_num

	BlockID        extentTree_blockstart;        // extent tree 起始块
	uint64_t       extentTree_blocknum;          // extent tree 占用的块数
	BlockID        extent_root_block;            // extent tree 的根结点

	BlockID        data_blockstart;              // data 起始块
	uint64_t       data_blocknum;                // data 占用的块数

	int            root_inode;                   // root 目录对应的 inode 号
} SuperBlock;

extern SuperBlock sb;


extern u_int64_t imap[];
extern int imap_size;
extern std::mutex imap_locks[];


typedef struct {
    BlockID physical_start;
    uint64_t block_count;
} Extent;     // disk extent

typedef struct {
	BlockID logical_start, physical_start;
    uint64_t block_count;
} iExtent;    // in-inode extent

typedef struct {
	int         idx;
	uint8_t     used;
	uint8_t     major;                              // Major device number
	uint8_t     minor;                              // Minor device number
	file_type_t type;                               // 文件类型
	uint32_t    nlink;                              // 硬链接数目
	uint64_t    file_size;                          // 文件大小
	iExtent     direct_extents[DIRECT_EXTENT_NUM];  // 直接使用的 extent
	uint64_t    indirect_extent_block;              // 存储间接 extents 的 block 号
	uint64_t    ctime;                              // 创建时间
	uint64_t    mtime;                              // 修改时间
	uint64_t    atime;                              // 访问时间
	char        pad[56];
} DInode;     // disk inode


extern "C" {
    void readObj(void* obj, size_t siz, uint64_t lba_start, uint32_t lba_count);
    void writeObj(void* obj, size_t siz, uint64_t lba_start, uint32_t lba_count);
}

void read_sb();
void read_imap(u_int64_t *bm);
void write_imap(u_int64_t *bm);


uint64_t extent_size(const iExtent* ext);
void* read_extent(const iExtent *ext, uint64_t offset, size_t size);
void write_extent(const iExtent *ext, uint64_t offset, const void *data, size_t size);