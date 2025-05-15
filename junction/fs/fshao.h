#define MYPREFIX "FSHAO:"
#define MYPREFIX_LEN (sizeof(MYPREFIX) - 1)
#define MAX_PATH_LEN 4096

typedef struct {
	uint32_t       magic_number;                 // 0x0517
   
	uint32_t       block_size;                   // 每块（LBA）的大小（B）
    uint64_t       total_blocks_num;             // 总块数

	uint64_t       inode_size;                   // inode 大小
	uint64_t       inode_num;                    // inode 数目

	uint64_t       free_stk_len;                 // free block stack 的容量

	uint64_t       imap_block_num;               // inode bitmap 占用的块数
	uint64_t       imap_block_start;             // inode bitmap 起始块

	uint64_t       free_stk_block_num;           // free stack 占用的块数
	uint64_t       free_stk_block_start;         // free stack （用于分配）起始块
	uint64_t       free_stk_block2_start;        // free stack2（用于回收）起始块
	
	uint64_t       inode_table_block_num;        // inode table 占用的块数
    uint64_t       inode_table_block_start;      // inode table 起始块

	uint64_t       root_inode;                   // root 目录对应的 inode 号
   
	uint64_t       data_block_num;               // data占用的块数
    uint64_t       data_block_start;             // data起始块
} SuperBlock;



void readObj(void* obj, size_t siz, uint64_t lba_start, uint32_t lba_count);