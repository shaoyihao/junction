#include "dentryCache.h"
#include "inodeCache.h"
#include "dentry.h"
#include "file.h"
#include <string.h>
#include <vector>

void init_dentryCache(size_t capacity)
{
    log_info("init dir entry cache ...");
    auto& dentrycache = DentryCacheManager::instance(capacity);   // 好像没有必要设置 evict 函数
    dentrycache.put("/", ROOT_INO);      // 手动将 <“/”, ROOT_INO> 放入 cache 中
}

// count == 0: "/"
// count == k: "/parts[0]/.../parts[k-1]"
std::string join_path(const std::vector<std::string>& parts, int count) 
{
    if (count == 0) return "/";

    std::string path;
    for (int i = 0; i < count; ++i) path = path + "/" + parts[i];
    return path;
}
std::shared_ptr<MInode> get_inode(const char *pathname)   // 获取该 pathname 对应的 inode pointer
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

    int prefix_hit_index;  // 表示命中到哪一层（parts.size() 表示完整路径，0 表示 "/"）
    int base_inode;
    for (int i = parts.size(); i >= 0; --i)    // 从完整路径开始递减搜索
    {
        std::string probe_path = join_path(parts, i);
        if (dentrycache.get(probe_path.c_str(), base_inode))  // cache hit
        {
            log_info("dentry cache hits <\"%s\", %d>!", probe_path.c_str(), base_inode);
            prefix_hit_index = i;
            break;
        }
    }
    // 至少会命中到 “/”，此时 base_inode=0，prefix_hit_index=0

    std::shared_ptr<MInode> current_inode = get_inode(base_inode);
    for (int i = prefix_hit_index; i < parts.size(); ++i)    // 在 dentries 中搜索 parts[i]
    {
        if (current_inode->disk_inode.type != DIRECTORY) 
        {
            release_inode(current_inode);
            return nullptr;   // 查找失败
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
        free(entries);
        
        if (found == false) 
        {
            release_inode(current_inode);
            return nullptr;
        }
        
        release_inode(current_inode);
        current_inode = get_inode(target_inum);
        std::string full_path = join_path(parts, i + 1);
        dentrycache.put(full_path.c_str(), target_inum);
    }

    return current_inode;
}