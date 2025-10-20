#include "dentry.h"
#include "inode.h"
#include "inodeCache.h"
#include "file.h"
#include "dentryCache.h"
#include <vector>
#include <cstring>
#include <string>


void lookup(const char *pathname, IEntry& res)
{
    // log_info("[lookup(%s)] START", pathname);
    
    auto& dentrycache = DentryCacheManager::instance();

    // char* path_copy = (char*)smalloc(MAX_PATH_LEN);
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
    delete[] path_copy;
    // log_info("[lookup(%s)] split the pathname SUCCESS", pathname);


    int prefix_hit_index;  // 表示命中到哪一层（parts.size() 表示完整路径，0 表示 "/"）
    int base_inode;
    for (int i = parts.size(); i >= 0; --i)    // 从完整路径开始递减搜索
    {
        std::string probe_path = join_path(parts, i);
        if (dentrycache.get(probe_path.c_str(), base_inode))  // cache hit
        {
            // log_info("dentry cache hits <\"%s\", %d>!", probe_path.c_str(), base_inode);
            prefix_hit_index = i;
            break;
        }
    }
    // 至少会命中到 “/”，此时 base_inode=0，prefix_hit_index=0
    // log_info("[lookup(%s)] find the base inode [%d]", pathname, base_inode);

    // 基于 base_inode 搜索完整路径对应的 Inode
    MInode* current_inode = get_inode(base_inode), *last_inode = nullptr;
    if (!current_inode) 
    {
        log_info("[lookup] ERROR: get_inode(%d) returned null!", base_inode);
        res.code = -1; // 设置错误码
        return;        // 立即返回
    }

    for (int i = prefix_hit_index; i < parts.size(); i++)    // 在 dentries 中搜索 parts[i]
    {
        // log_info("[lookup(%s)] looking for part '%s'", pathname, parts[i].c_str());

        if (current_inode->disk_inode.type != DIRECTORY) 
        {
            log_info("not a DIRECTORY");
            release_inode(last_inode);
            release_inode(current_inode);
            res.code = -1;
            res.parent_ino = nullptr;
            res.ino = nullptr;
            return;
        }

        bool found = false;
        int target_inum;

        {
            // log_info("try to get inode[%d]'s lock", current_inode->inum);
            // SpinGuard g(&current_inode->lock);
            // log_info("get inode[%d]'s lock", current_inode->inum);
            // uint64_t before_readfullfile = rdtsc();

            char* raw_buffer = new char[current_inode->disk_inode.file_size];
            Dirent* entries = reinterpret_cast<Dirent*>(raw_buffer);

            // log_info("gonna smalloc");
            // Dirent* entries = (Dirent*)smalloc(current_inode->disk_inode.file_size);  // 一次性读取整个文件内容，是否会有问题？考虑进行优化
            // log_info("ret from smalloc");
            if (!entries)
            {
                log_info("[lookup(%s)->new(%lu)] ERROR: fail to new", pathname, current_inode->disk_inode.file_size);

            }
            read_full_file(current_inode, entries);
            // log_info("read full content of inode %d", current_inode->inum);
            // uint64_t after_readfullfile = rdtsc();
            // log_info("[readfullfile] duration: %lu us", (after_readfullfile - before_readfullfile) / cycles_per_us);

            int entry_count = current_inode->disk_inode.file_size / sizeof(Dirent);
            for (int j = 0; j < entry_count; ++j) 
                if (strcmp(entries[j].name, parts[i].c_str()) == 0) 
                {
                    target_inum = entries[j].inum;
                    found = true;
                    log_info("found the dentry");
                    break;
                }
            // sfree(entries);
            delete[] raw_buffer;
        }

        if (found == false)   // 该目录下不存在 parts[i]
        {
            if (i == parts.size() - 1)  // 仅是最后一个token不匹配（可能是新建文件）
            {
                release_inode(last_inode);
                res.code = 1;
                res.parent_ino = current_inode;
                res.ino = nullptr;
                strncpy(res.last_name, parts[i].c_str(), MAX_PATH_LEN);
            }
            else                        // 不合法路径
            {
                release_inode(last_inode);
                release_inode(current_inode);
                res.code = -1;
                res.parent_ino = nullptr;
                res.ino = nullptr;
            }
            return;
        }

        // 该目录下存在 parts[i]，向下一级
        release_inode(last_inode);
        last_inode = current_inode;
        // uint64_t before_get_inode = rdtsc();
        current_inode = get_inode(target_inum);
        if (!current_inode) 
        {
            log_info("[lookup] ERROR: get_inode(%d) returned null!", target_inum);
            res.code = -1; 
            return;        
        }
        // uint64_t after_get_inode = rdtsc();
        // log_info("[get_inode] duration: %lu us", (after_get_inode - before_get_inode) / cycles_per_us);

        std::string full_path = join_path(parts, i + 1);  // 构造完整路径
        dentrycache.put(full_path.c_str(), target_inum);  // 存入 cache 中
    }
    // uint64_t after_examine = rdtsc();
    // log_info("[examine] duration: %lu us", (after_examine - before_examine) / cycles_per_us);

    // 完全匹配
    res.code = 0;
    res.parent_ino = last_inode;
    res.ino = current_inode;
    strncpy(res.last_name, parts.back().c_str(), MAX_PATH_LEN);
}

void delete_dentry(MInode*& dir_inode, char* name)
{
    SpinGuard g(&dir_inode->lock);

    // log_info("delete_dentry(%d, %s) START", dir_inode->inum, name);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        log_info("[ERROR] Cannot delete special entries '.' or '..'");
        return;
    }

    if (dir_inode->disk_inode.type != DIRECTORY) 
    {
        log_info("[ERROR] delete_dentry(%d, %s): inode[%d] is not a dir", dir_inode->inum, name, dir_inode->inum);
        return;
    }

    uint64_t filesize = dir_inode->disk_inode.file_size;

    // Dirent* entries = (Dirent*)smalloc(filesize);
    char* raw_buffer = new char[filesize];
    Dirent* entries = reinterpret_cast<Dirent*>(raw_buffer);
    read_full_file(dir_inode, entries);
    int entry_count = filesize / sizeof(Dirent);
    bool found = false;
    int idx;
    for (idx = 0; idx < entry_count; idx++) 
        if (strcmp(entries[idx].name, name) == 0) 
        {
            found = true;
            break;
        }
    
    if (found == false)
    {
        log_info("dir [%d] doesn't include name \"%s\"", dir_inode->inum, name);
    }
    else
    {
        memmove(entries + idx, entries + idx + 1, (entry_count - idx - 1) * sizeof(Dirent));
        entry_count--;
        uint64_t newsize = entry_count * sizeof(Dirent);
        write_file(dir_inode, 0, (char*)entries, newsize);  // 从文件偏移 0 处开始写数据（可能会覆盖）
        dir_inode->dirty = true;
        dir_inode->disk_inode.file_size = newsize;
    }

    // sfree(entries);
    delete[] raw_buffer;

    // log_info("delete_dentry(%d, %s) OVER", dir_inode->inum, name);
}