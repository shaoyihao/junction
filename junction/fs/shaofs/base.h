// fs的一些重要参数、常用类型等

#pragma once

#include "utili.h"

#define SHAOFS                  517
#define MYPREFIX                "FSHAO:"
#define MYPREFIX_LEN            (sizeof(MYPREFIX) - 1)   // 不包括末尾的 \0，值为 6

#define BLOCK_SIZE              4096
#define INODENUM                (BLOCK_SIZE * 8)    // 所能创建的文件数目（目前设置的是 32768）
#define BMAPNUM_PERGROUP        1                   // 每个 group 中 bitmap 占多少个块
#define ROOT_INO                0                   // 根结点对应的 inode

#define DIRECT_EXTENT_NUM       6                   // 每个 inode 中的 direct extent 数目
#define DEFAULT_EXTENT_LENGTH   10                  // extent 的默认长度

#define MAX_PATH_LEN            4096
#define NAMESIZ                 255                  // 单个文件名 token 的长度 （和 ext4 一致）


typedef uint64_t BlockID;

typedef enum {
    UNKNOWN = 0,
    REGULAR,    // 普通文件
    DIRECTORY,  // 目录
    SYMLINK,    // 符号链接
} file_type_t;

