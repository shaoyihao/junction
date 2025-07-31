// fs的一些重要参数、常用类型等

#pragma once

#include "utili.h"

#define SHAOFS                  517
#define DIRECT_EXTENT_NUM       6
#define MYPREFIX                "FSHAO:"
#define MYPREFIX_LEN            (sizeof(MYPREFIX) - 1)
#define MAX_PATH_LEN            4096
#define ROOT_INO                0
#define INODENUM                1024
#define BLOCK_SIZE              4096
#define DIRSIZ                  40        // 单个文件名 token 的长度
#define DEFAULT_EXTENT_LENGTH   10        // extent 的默认长度


typedef uint64_t BlockID;

typedef enum {
    UNKNOWN = 0,
    REGULAR,    // 普通文件
    DIRECTORY,  // 目录
    SYMLINK,    // 符号链接
} file_type_t;

