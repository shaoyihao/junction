#include "base.h"
#include "extent.h"
#include "blockCache.h"

// void print_extent_node(BlockID block_id)
// {
//     printf("\n--- Inspecting Extent Tree Node at block %lu ---\n", block_id);

//     ExtentTreeNode node;
//     readObj(&node, sizeof(node), block_id, 1);

//     uint16_t magic = *(uint16_t*)(&node); 
//     uint16_t type  = *((uint16_t*)(&node) + 1);

//     if (magic == EXTENT_LEAF_MAGIC && type == LEAF) 
// 	{
//         ExtentLeafNode* leaf = &node.leaf;
//         printf("[LEAF NODE]\n");
//         printf("Valid count : %u\n", leaf->valid_count);
//         printf("Prev leaf   : %lu\n", leaf->prev_leaf);
//         printf("Next leaf   : %lu\n", leaf->next_leaf);
//         for (uint32_t i = 0; i < leaf->valid_count; i++) 
//             printf("  Extent[%2u] = [start = %lu, count = %lu]\n", i, leaf->entries[i].physical_start, leaf->entries[i].block_count);
//     } 
// 	else if (magic == EXTENT_INDEX_MAGIC && type == INDEX) 
// 	{
//         ExtentIndexNode *index = &node.index;
//         printf("[INDEX NODE]\n");
//         printf("Valid count : %u\n", index->valid_count);
//         for (uint32_t i = 0; i < index->valid_count; i++) 
//             printf("  Entry[%2u] = { key = %lu, child = %lu }\n", i, index->entries[i].key, index->entries[i].child_block);
//     } 
// 	else 
// 	{
//         printf("Unknown node type! magic = 0x%X, type = %u\n", magic, type);
//     }

//     printf("--- END ---\n");
// }
// void print_extent_tree(BlockID block_id, int level)
// {
//     ExtentTreeNode node;
//     readObj(&node, sizeof(node), block_id, 1);

//     uint16_t magic = *(uint16_t*)(&node);
//     uint16_t type  = *((uint16_t*)(&node) + 1);

//     // 缩进输出
//     for (int i = 0; i < level; i++) printf("  ");

//     if (magic == EXTENT_LEAF_MAGIC && type == LEAF) 
// 	{
//         ExtentLeafNode *leaf = &node.leaf;
//         printf("Leaf Node @ block %lu | valid_count: %u\n", block_id, leaf->valid_count);
//         for (uint32_t i = 0; i < leaf->valid_count; i++) 
// 		{
//             for (int j = 0; j < level; j++) printf("  ");
//             printf("  - Extent[%u]: [start = %lu, count = %lu]\n", i, leaf->entries[i].physical_start, leaf->entries[i].block_count);
//         }
//     } 
// 	else if (magic == EXTENT_INDEX_MAGIC && type == INDEX) 
// 	{
//         ExtentIndexNode *index = &node.index;
//         printf("Index Node @ block %lu | valid_count: %u\n", block_id, index->valid_count);
//         for (uint32_t i = 0; i < index->valid_count; i++) 
// 		{
//             for (int j = 0; j < level; j++) printf("  ");
//             printf("  ↳ Entry[%u]: key = %lu, child = %lu\n", i, index->entries[i].key, index->entries[i].child_block);
//             print_extent_tree(index->entries[i].child_block, level + 1); // 递归打印 child
//         }
//     } 
// 	else 
// 	{
//         for (int i = 0; i < level; i++) printf("  ");
//         printf("Unknown node at block %lu (magic=0x%x, type=%u)\n", block_id, magic, type);
//     }
// }
// int check_tree_consistency(BlockID block_id, int level)
// {
//     ExtentTreeNode node;
//     readObj(&node, sizeof(node), block_id, 1);

//     uint16_t magic = *(uint16_t*)(&node);
//     uint16_t type  = *((uint16_t*)(&node) + 1);

//     char indent[64] = {0};
//     memset(indent, ' ', level * 2);
//     indent[level * 2] = '\0';

//     if (magic == EXTENT_LEAF_MAGIC && type == LEAF) 
// 	{
//         ExtentLeafNode *leaf = &node.leaf;

//         if (leaf->valid_count > MAX_EXTENTS_PER_LEAF) 
// 		{
//             printf("%s Leaf node at block %lu has too many entries: %u\n", indent, block_id, leaf->valid_count);
//             return 0;
//         }

//         // 检查 extent 是否升序 & 不重叠
//         for (uint32_t i = 1; i < leaf->valid_count; i++) 
// 		{
//             uint64_t prev_end = leaf->entries[i-1].physical_start + leaf->entries[i-1].block_count;
//             uint64_t curr_start = leaf->entries[i].physical_start;
//             if (curr_start < prev_end) 
// 			{
//                 printf("%s Leaf node extent overlap or out-of-order at block %lu (entry %u)\n", indent, block_id, i);
//                 return 0;
//             }
//         }

//         printf("%s Leaf @ block %lu passed.\n", indent, block_id);
//         return 1;

//     } 
// 	else if (magic == EXTENT_INDEX_MAGIC && type == INDEX) 
// 	{
//         ExtentIndexNode *index = &node.index;

//         if (index->valid_count > MAX_ENTRIES_PER_NODE) 
// 		{
//             printf("%s Index node at block %lu has too many entries: %u\n", indent, block_id, index->valid_count);
//             return 0;
//         }

//         // key 是否升序
//         for (uint32_t i = 1; i < index->valid_count; i++) 
//             if (index->entries[i].key < index->entries[i-1].key) 
// 			{
//                 printf("%s Index keys not in order at block %lu (entry %u)\n", indent, block_id, i);
//                 return 0;
//             }

//         printf("%s Index @ block %lu passed.\n", indent, block_id);

//         // 递归检查每个 child
//         for (uint32_t i = 0; i < index->valid_count; i++) 
//             if (!check_tree_consistency(index->entries[i].child_block, level + 1)) return 0;

//         return 1;

//     } 
// 	else 
// 	{
//         printf("%s Unknown node at block %lu: magic=0x%x, type=%u\n", indent, block_id, magic, type);
//         return 0;
//     }
// }

// static inline bool extent_is_adjacent(const Extent *a, const Extent *b)
// {
//     return (a->physical_start + a->block_count) == b->physical_start || (b->physical_start + b->block_count) == a->physical_start;
// }
static inline bool extent_overlaps_or_adjacent(const Extent *a, const Extent *b)
{
    BlockID a_start = a->physical_start, a_end = a->physical_start + a->block_count;
    BlockID b_start = b->physical_start, b_end = b->physical_start + b->block_count;
    return !(a_end < b_start || b_end < a_start);   // overlap or touch
}
static inline void extent_merge_into(Extent *a, const Extent *b)   // 将 a、b 合并至 a 中（无需考虑 a、b 顺序）
{
    BlockID start = MIN(a->physical_start, b->physical_start);
    BlockID end   = MAX(a->physical_start + a->block_count, b->physical_start + b->block_count);
    a->physical_start = start;
    a->block_count = end - start;
}

static void insertion_sort_extents(Extent *arr, int n)
{
    for (int i = 1; i < n; i++) 
    {
        Extent key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j].physical_start > key.physical_start) 
        {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static int merge_sorted_extents(const Extent *sortedarr, int n, Extent *out)
{
    if (n == 0) return 0;

    int m = 0;
    out[m++] = sortedarr[0];
    for (int i = 1; i < n; i++) 
    {
        Extent const *cur = &sortedarr[i];
        Extent *last = &out[m - 1];
        if (extent_overlaps_or_adjacent(last, cur)) extent_merge_into(last, cur); 
        else out[m++] = *cur;
    }
    return m;
}

typedef enum {
    FREE_OK,            // inserted and merged successfully, leaf written back
    FREE_NEED_SPLIT,    // insertion would overflow leaf even after merge; caller should split
    FREE_ERROR          // error (e.g., corrupted tree or I/O fail)
} FreeResult;

static FreeResult insert_extent_into_leaf(ExtentLeafNode *leaf, const Extent *e)   // Try to insert `e` into the provided leaf (in-memory)
{
    if (!leaf) return FREE_ERROR;
    if (leaf->valid_count > MAX_EXTENTS_PER_LEAF) return FREE_ERROR;    // corrupted

    Extent tmp[MAX_EXTENTS_PER_LEAF + 1];
    uint32_t n = leaf->valid_count;
    memcpy(tmp, leaf->entries, n * sizeof(Extent));
    tmp[n++] = *e;

    insertion_sort_extents(tmp, n);

    Extent merged[MAX_EXTENTS_PER_LEAF + 1];
    uint32_t m = merge_sorted_extents(tmp, n, merged);

    if (m > MAX_EXTENTS_PER_LEAF) return FREE_NEED_SPLIT;   // can't fit even after merging

    memset(leaf->entries, 0, sizeof(leaf->entries));   // 清空 entries
    memcpy(leaf->entries, merged, m * sizeof(Extent)); // copy `merged` back to `leaf`
    leaf->valid_count = m;

    return FREE_OK;
}

static BlockID find_leaf_for_extent(const Extent *e)   // 查找该 extent 所属的叶结点
{
    ExtentTreeNode node;
    BlockID cur = sb.extent_root_block;
    for (int depth_guard = 0; depth_guard < 10; depth_guard++) 
    {
        read_block(cur, &node);
        uint16_t type = *((uint16_t*)(&node) + 1);
        if (type == LEAF) return cur;

        ExtentIndexNode *idx = &node.index;
        int pick = 0;
        for (int i = 0; i < idx->valid_count; i++)    // choose child: largest index with key <= e.start   （可以用二分来优化）
        {
            if (idx->entries[i].key <= e->physical_start) pick = i;
            else break;
        }
        cur = idx->entries[pick].child_block;
    }
}

bool free_extent(Extent e)
{
    if (e.block_count == 0) return FREE_OK;

    ExtentTreeNode node;
    BlockID leaf_block = find_leaf_for_extent(&e);
    read_block(leaf_block, &node);   // 该结点的类型应当是 LEAF

    if (insert_extent_into_leaf(&node.leaf, &e) == FREE_OK)
    {
        write_block(leaf_block, &node);
        return true;
    }

    return false;
}


// BlockID split_leaf_and_insert(BlockID leaf, Extent e)   // 分裂叶子结点，并插入新 extent
// {
//     ExtentTreeNode old_node;
//     read_block(leaf, &old_node);
//     ExtentLeafNode* old_leaf = &old_node.leaf;

//     Extent tmp[MAX_EXTENTS_PER_LEAF + 1];
//     memcpy(tmp, old_leaf->entries, old_leaf->valid_count * sizeof(Extent));
//     tmp[old_leaf->valid_count] = e;
//     uint32_t total = old_leaf->valid_count + 1;

//     insertion_sort_extents(tmp, total);

//     uint32_t left_count = total / 2;
//     uint32_t right_count = total - left_count;

//     // 创建新叶子节点，分配新块号
//     BlockID new_leaf_block = alloc_block(); // 你需要实现这个，申请一个空闲块号
//     ExtentTreeNode new_node = {0};
//     ExtentLeafNode *new_leaf = &new_node.leaf;

//     new_leaf->magic = EXTENT_LEAF_MAGIC;
//     new_leaf->type = LEAF;
//     new_leaf->valid_count = right_count;
//     new_leaf->prev_leaf = leaf;
//     new_leaf->next_leaf = old_leaf->next_leaf;

//     // 新叶子写入右半部分extent
//     memcpy(new_leaf->entries, &tmp[left_count], right_count * sizeof(Extent));

//     // 老叶子写入左半部分extent
//     memset(old_leaf->entries, 0, sizeof(old_leaf->entries));
//     memcpy(old_leaf->entries, tmp, left_count * sizeof(Extent));
//     old_leaf->valid_count = left_count;

//     // 更新双向链表指针
//     if (old_leaf->next_leaf != 0) {
//         // 读取 old next leaf 更新它的 prev_leaf
//         ExtentTreeNode next_node;
//         read_block(old_leaf->next_leaf, &next_node);
//         next_node.leaf.prev_leaf = new_leaf_block;
//         write_block(old_leaf->next_leaf, &next_node);
//     }
//     old_leaf->next_leaf = new_leaf_block;

//     // 写回两个叶子节点
//     write_block(leaf, &old_node);
//     write_block(new_leaf_block, &new_node);

//     // 新叶子最小 key 用于索引提升
//     BlockID new_key = new_leaf->entries[0].physical_start;

//     // 提升到父节点（递归插入）
//     insert_index(sb.extent_root_block, new_key, new_leaf_block);

//     return new_leaf_block;
// }



bool alloc_from_leaf(ExtentLeafNode *leaf, uint64_t size, Extent *result)  // 从该叶结点中能否分配出一个 extent
{
    for (uint32_t i = 0; i < leaf->valid_count; i++) 
	{
        Extent *e = &leaf->entries[i];
        if (e->block_count >= size)   // 可以分配
		{
            *result = (Extent){ .physical_start = e->physical_start, .block_count = size };

            if (e->block_count == size)  // 完全匹配，删除这个 extent
			{
                for (uint32_t j = i + 1; j < leaf->valid_count; j++)
                    leaf->entries[j - 1] = leaf->entries[j];
                leaf->valid_count--;
            } 
			else  // 分裂，保留右半部分
			{
                e->physical_start += size;
                e->block_count    -= size;
            }
            return true;
        }
    }
    return false;
}
bool alloc_from_node(BlockID block_id, uint64_t size, Extent *result)  // 判断从该结点中能否分配出一个 extent
{
    ExtentTreeNode node;
    // readObj(&node, sizeof(node), block_id, 1);
    read_block(block_id, &node);

    uint16_t type = *((uint16_t*)(&node) + 1);
    if (type == LEAF) 
	{
		if (alloc_from_leaf(&node.leaf, size, result))    // 若分配成功则 node 会发生变化，需要重新写入
		{
			// writeObj(&node, sizeof(node), block_id, 1);
            write_block(block_id, &node);
			return true;
		}
	}
	else if (type == INDEX)
	{
		ExtentIndexNode* idx = &node.index;
		for (uint32_t i = 0; i < idx->valid_count; i++)   // 遍历所有子树
			if (alloc_from_node(idx->entries[i].child_block, size, result)) return true;
	}

	return false;
}
bool alloc_extent(uint64_t size, Extent *result)
{
    return alloc_from_node(sb.extent_root_block, size, result);
}
