#pragma once

#include "base.h"
#include "disk.h"

typedef enum {
    INVALID = 0,
    LEAF    = 1,
    INDEX   = 2,
} ExtentNodeType;

#define EXTENT_INDEX_MAGIC          0x1111
#define EXTENT_LEAF_MAGIC           0x2222
#define ENTRT_SIZE                  16   // 这里 extent tree 中 index 结点和 leaf 结点中 entry 的大小是一样的

#define EXTENT_INDEX_HEADER_SIZE    8         // 见 ExtentIndexNode 结构
#define MAX_ENTRIES_PER_NODE    ((BLOCK_SIZE - EXTENT_INDEX_HEADER_SIZE) / ENTRT_SIZE)
typedef struct {
    uint16_t       magic;            // EXTENT_INDEX_MAGIC
    uint16_t       type;             // INDEX
    uint32_t       valid_count;      // 当前有效 entry 数
	struct {
    	BlockID key;                 // 对应子树中最小的 start_lba
    	BlockID child_block;         // 指向子节点的 block 号
	} entries[MAX_ENTRIES_PER_NODE]; 
} ExtentIndexNode; // 4KB

#define EXTENT_LEAF_HEADER_SIZE     24        // 见 ExtentLeafNode 结构
#define MAX_EXTENTS_PER_LEAF        ((BLOCK_SIZE - EXTENT_LEAF_HEADER_SIZE)  / ENTRT_SIZE)
typedef struct {
    uint16_t        magic;                          // EXTENT_LEAF_MAGIC
    uint16_t        type;                           // LEAF
    uint32_t        valid_count;                    // 当前有效 entry 数
    BlockID         next_leaf, prev_leaf;           // 上、下叶子节点块号（双向链表）
    Extent          entries[MAX_EXTENTS_PER_LEAF];  // free extent
} ExtentLeafNode;  // 4KB

typedef union {
    ExtentIndexNode index;
    ExtentLeafNode  leaf;
    char            raw[BLOCK_SIZE];  // 用于按块读写
} ExtentTreeNode;



bool alloc_extent(uint64_t size, Extent *result);