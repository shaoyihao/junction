#include "base.h"
#include "extent.h"

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
    readObj(&node, sizeof(node), block_id, 1);

    uint16_t type = *((uint16_t*)(&node) + 1);

    if (type == LEAF) 
	{
		if (alloc_from_leaf(&node.leaf, size, result)) 
		{
			writeObj(&node, sizeof(node), block_id, 1);
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
