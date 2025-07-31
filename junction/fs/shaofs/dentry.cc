#include "dentry.h"
#include "inode.h"
#include "inodeCache.h"
#include "file.h"
#include "dentryCache.h"
#include <vector>
#include <cstring>
#include <string>


static std::string join_path(const std::vector<std::string>& parts, int count) 
{
    if (count == 0) return "/";

    std::string path;
    for (int i = 0; i < count; ++i) path = path + "/" + parts[i];
    return path;
}

IEntry lookup(const char *pathname)
{
    log_info("try to lookup path: %s", pathname);
    
    auto& dentrycache = DentryCacheManager::instance();

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
    int base_inode;
    for (int i = parts.size(); i >= 0; --i)    // 从完整路径开始递减搜索
    {
        std::string probe_path = join_path(parts, i);
        if (dentrycache.get(probe_path.c_str(), base_inode))  // cache hit
        {
            prefix_hit_index = i;
            break;
        }
    }

    if (prefix_hit_index == -1)   // 根目录都没命中，手动读出来
    {   
        base_inode = ROOT_INO;
        dentrycache.put("/", base_inode);  // 存入 cache 中
        prefix_hit_index = 0;
    }

    // 基于 base_inode 搜索完整路径对应的 Inode
    IEntry res;
    std::shared_ptr<MInode> current_inode = get_inode(base_inode);
    for (int i = prefix_hit_index; i < parts.size(); ++i)    // 在 dentries 中搜索 parts[i]
    {
        if (current_inode->disk_inode.type != DIRECTORY) 
        {
            res.code = -1;
            return res;
        }

        Dirent* entries = (Dirent*)read_file_content(current_inode);
        int entry_count = current_inode->disk_inode.file_size / sizeof(Dirent);
        bool found = false;
        int target_inum;
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
        current_inode = get_inode(target_inum);
        std::string full_path = join_path(parts, i + 1);  // 构造完整路径
        dentrycache.put(full_path.c_str(), target_inum);      // 存入 cache 中
    }

    // 完全匹配
    res.code = 0;
    res.ino = current_inode;
    return res;
}