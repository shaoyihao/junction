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
MInode* get_inode_by_pathname(const char *pathname)   // 获取该 pathname 对应的 inode pointer
{
    log_info("try to lookup path: %s", pathname);

    auto& dentrycache = DentryCacheManager::instance();

    char* path_copy = new char[MAX_PATH_LEN];
    strncpy(path_copy, pathname, MAX_PATH_LEN);
    path_copy[MAX_PATH_LEN - 1] = '\0';

    std::vector<std::string> parts;    // 对 pathname 进行拆分
    char* saveptr;
    char* token = strtok_r(path_copy, "/", &saveptr);
    while (token) 
    { 
        parts.push_back(token); 
        token = strtok_r(NULL, "/", &saveptr);
    }
    // sfree(path_copy);
    delete[] path_copy;

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
    // 至少会命中到 “/”，此时 base_inode=ROOT_INO，prefix_hit_index=0

    MInode* current_inode = get_inode(base_inode);
    for (int i = prefix_hit_index; i < parts.size(); ++i)    // 在 dentries 中搜索 parts[i]
    {
        SpinGuard g(&current_inode->lock);
        
        if (current_inode->disk_inode.type != DIRECTORY) 
        {
            release_inode(current_inode);
            return nullptr;   // 查找失败
        }

        // Dirent* entries = (Dirent*)smalloc(current_inode->disk_inode.file_size);   // 一次性读取整个文件内容，是否会有问题？考虑进行优化
        char* raw_buffer = new char[current_inode->disk_inode.file_size];
        Dirent* entries = reinterpret_cast<Dirent*>(raw_buffer);
        read_full_file(current_inode, entries);
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
        // sfree(entries);
        delete[] raw_buffer;
        
        release_inode(current_inode);
        if (found == false) return nullptr;
        
        current_inode = get_inode(target_inum);
        std::string full_path = join_path(parts, i + 1);
        dentrycache.put(full_path.c_str(), target_inum);
    }

    return current_inode;
}