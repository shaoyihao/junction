#pragma once
#include "base.h"
// #include "storage.h"
#include <cstdint>


#define INODENUM_PER_BLOCK    16     // block_size / inode_size
#define DATABLOCKS_PERGROUP   (BMAPNUM_PERGROUP * BLOCK_SIZE * 8)  // 4096*8=32768
#define TOTALBLOCKS_PERGROUP  (BMAPNUM_PERGROUP + DATABLOCKS_PERGROUP) 


typedef struct {
	uint32_t       magic_number;                 // FSMAGIC=0x0517junction/fs/shaofs
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

	int            group_num;                    // group 数目
	BlockID        gmap_blockstart;              // group bitmap 起始块
	uint64_t       gmap_blocknum;                // group bitmap 占用的块数（很难不是1）

	int            root_inode;                   // root 目录对应的 inode 号
} SuperBlock;

extern SuperBlock sb;

DECLARE_BITMAP(imap, INODENUM);
extern int imap_size;

DECLARE_BITMAP(gmap, BLOCK_SIZE * 8);


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
	#include "runtime/storage.h"
    // void readObj(void* obj, size_t siz, uint64_t lba_start, uint32_t lba_count);
    // void writeObj(void* obj, size_t siz, uint64_t lba_start, uint32_t lba_count);
}

void read_sb();
void read_bm(unsigned long* bm, u_int64_t nbits, BlockID blockstart, u_int64_t blockcount);
void write_bm(unsigned long* bm, u_int64_t nbits, BlockID blockstart, u_int64_t blockcount);


uint64_t extent_size(const iExtent* ext);
void read_extent(const iExtent *ext, uint64_t offset, char* buf, size_t size);
void write_extent(const iExtent *ext, uint64_t offset, const void *data, size_t size);


void test_write_disk();
void test_read_disk();