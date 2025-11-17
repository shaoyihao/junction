#include "disk.h"
#include "extent.h"
#include "blockCache.h"
#include "group.h"
#include <vector>
#include <algorithm>

extern "C" {
#include "../runtime/defs.h"
}

void load_all_extents(DInode &di, std::vector<iExtent> &out)   // 将该 inode 拥有的 iextent 都读取出来（应该是已经“规范化”了，有序+无法再合并）
{
    // log_info("load_all_extents() START");

	out.clear();
	for (int i = 0; i < DIRECT_EXTENT_NUM; i++) 
	{
        const iExtent &e = di.direct_extents[i];
        if (e.block_count == 0) break;        // 遇到 iExtent{0,0,0}（结束标记）即退出
        out.push_back(e);
    }

    BlockEntry* block;
	read_block(di.indirect_extent_block, block);
	iExtent* exts = reinterpret_cast<iExtent*>(block->data);
	const size_t cap = BLOCK_SIZE / sizeof(iExtent);
	for (int i = 0; i < cap; i++)
	{
		if (exts[i].block_count == 0) break;   // 遇到 iExtent{0,0,0}（结束标记）即退出
		out.push_back(exts[i]);
	}

    // log_info("load_all_extents() OVER");
}
void normalize_extent(std::vector<iExtent> &exts) 
{
    if (exts.empty()) return;

    std::sort(exts.begin(), exts.end(), [](auto &a, auto &b){ return a.logical_start < b.logical_start; });
    exts.erase(std::remove_if(exts.begin(), exts.end(), [](const iExtent &e) { return e.block_count == 0; }), exts.end());

    std::vector<iExtent> v;
    v.reserve(exts.size());
    v.push_back(exts[0]);
    for (int i = 1; i < exts.size(); i++) 
	{
        auto &prev = v.back();
        const auto &cur = exts[i];
        
        if (prev.logical_start  + prev.block_count == cur.logical_start && prev.physical_start + prev.block_count == cur.physical_start)  // 逻辑连续且物理也紧邻
            prev.block_count += cur.block_count;
		else v.push_back(cur);
    }
    exts.swap(v);
}
void store_all_extents(DInode &di, std::vector<iExtent> &exts)
{
    // log_info("store_all_extents() START");

    normalize_extent(exts);
    
	size_t n = exts.size();
	size_t nd = MIN(n, DIRECT_EXTENT_NUM);
	for (int i = 0; i < nd; i++) di.direct_extents[i] = exts[i];
	for (int i = nd; i < DIRECT_EXTENT_NUM; i++) di.direct_extents[i] = iExtent{0, 0, 0};  // 若存在则填充为0
	
	int remain = (n > nd) ? (n - nd) : 0;
	if (remain == 0) return;

	const size_t cap = CEIL(BLOCK_SIZE, sizeof(iExtent));   // 此处无法整除，向上取整
    std::vector<iExtent> buf(cap);
    size_t to_copy = MIN(remain, cap);
	for (int i = 0; i < to_copy; i++) buf[i] = exts[nd + i];
	if (to_copy < cap) buf[to_copy] = iExtent{0, 0, 0};    // 结束标记
	write_block(di.indirect_extent_block, (const void*)buf.data());

    // log_info("store_all_extents() OVER");
}


static void bitmap_set_range(unsigned long *bm, uint64_t start, uint64_t len, bool one) 
{
    if (one)
        for (uint64_t i = 0; i < len; i++) bitmap_set(bm, start + i);
    else
        for (uint64_t i = 0; i < len; i++) bitmap_clear(bm, start + i);
}
static uint64_t alloc_oneextent_from_group(int gid, uint64_t cnt, Extent &res)   // 从该组中尝试分配一个extent（长度至多为cnt）
{
    if (cnt == 0 || gid == -1) return 0;

    BlockID bm_start, datablock_start;
	get_group_by_gid(gid, &bm_start, &datablock_start);

    // 因为 BMAPNUM_PERGROUP 一般为 1，所以直接读取一个 block 即可  （不过可能需要将代码写得更通用些，考虑到 BMAPNUM_PERGROUP 可能大于 1 的情况）
    BlockEntry* block;
	read_block(bm_start, block);
	unsigned long* bm = reinterpret_cast<unsigned long*>(block->data);


	// // unsigned long bm[BMAPNUM_PERGROUP * BLOCK_SIZE / sizeof(unsigned long)];  // 读取该组的 bitmap
    // size_t bm_size_bytes = BMAPNUM_PERGROUP * BLOCK_SIZE;
    // // unsigned long* bm = (unsigned long*)smalloc(bm_size_bytes);
    // char* raw_buffer = new char[bm_size_bytes];
    // unsigned long* bm = reinterpret_cast<unsigned long*>(raw_buffer);
    // if (!bm) 
    // {
    //     log_info("[ERROR] new() failed in alloc_extents_from_group");
    //     return 0;
    // }
    // for (int j = 0; j < BMAPNUM_PERGROUP; ++j) read_block(bm_start + j, bm + j * BLOCK_SIZE / sizeof(unsigned long));
    
    if (bitmap_popcount(bm, DATABLOCKS_PERGROUP) == DATABLOCKS_PERGROUP) 
    {
        // // sfree(bm);
        // delete[] raw_buffer;
        return 0;
    }
    if (cnt > (uint64_t)DATABLOCKS_PERGROUP) cnt = DATABLOCKS_PERGROUP;

    uint64_t current_count = 0, startbit;
    for (int i = 0; i < DATABLOCKS_PERGROUP; i++)
    {
        bool is_free = !bitmap_test(bm, i);
        if (is_free) 
        {
            if (current_count == 0) startbit = i;  // 标记空闲区起始位
            current_count++;
        }
        else if (!is_free && current_count == 0) continue;
        
        if ((!is_free && current_count > 0) || current_count == cnt)
        {
            res = Extent{ .physical_start = datablock_start + startbit, .block_count = current_count };
            bitmap_set_range(bm, startbit, current_count, 1); // 直接修改 block cache 中的 block 内容
            // for (int j = 0; j < BMAPNUM_PERGROUP; ++j) write_block(bm_start + j, bm + j * BLOCK_SIZE / sizeof(unsigned long));   // 其实可以优化为“只回写受影响块”
            // sfree(bm);
            // delete[] raw_buffer;
            return current_count;
        }
    }

    if (current_count > 0)
    {
        res = Extent{ .physical_start = datablock_start + startbit, .block_count = current_count };
        bitmap_set_range(bm, startbit, current_count, 1); // 直接修改 block cache 中的 block 内容
        // for (int j = 0; j < BMAPNUM_PERGROUP; ++j) write_block(bm_start + j, bm + j * BLOCK_SIZE / sizeof(unsigned long));  // 其实可以优化为“只回写受影响块”
        // // sfree(bm);
        // delete[] raw_buffer;
        return current_count;
    }

    // 理论上不会执行到这里（因为若该 group 中已不存在空闲块，则会在 bitmap_popcount() 判断中即return）
    // sfree(bm);
    // delete[] raw_buffer;
    return 0; 
}

// 在同一 group 内，尽量用若干个 extent 累加到 cnt 块；返回实际分到的块数。
// static uint64_t alloc_extents_from_group(int gid, uint64_t cnt, std::vector<Extent> &res)    // 这种写法可能会有多次的 bitmap IO
// {
//     if (cnt == 0 || gid == -1) return 0;

//     uint64_t taken = 0;
//     while (taken < cnt)
//     {
//         const uint64_t need = cnt - taken;
//         Extent e;
//         uint64_t got = alloc_oneextent_from_group(gid, need, e);
//         if (got == 0) break;  // 该 group 已无可分配的空闲 extent
//         res.push_back(e);
//         taken += got;
//     }

//     return taken;
// }


// 在同一 group 内，尽量用若干个 extent 累加到 cnt 块；返回实际分到的块数。
static uint64_t alloc_extents_from_group(int gid, uint64_t cnt, std::vector<Extent> &res)  // 考虑使用 buddy system 进行优化
{
    if (cnt == 0 || gid == -1) return 0;

    BlockID bm_start, datablock_start;
	get_group_by_gid(gid, &bm_start, &datablock_start);

    // 因为 BMAPNUM_PERGROUP 一般为 1，所以直接读取一个 block 即可  （不过可能需要将代码写得更通用些，考虑到 BMAPNUM_PERGROUP 可能大于 1 的情况）
    BlockEntry* block;
	read_block(bm_start, block);
	unsigned long* bm = reinterpret_cast<unsigned long*>(block->data);


	// // unsigned long bm[BMAPNUM_PERGROUP * BLOCK_SIZE / sizeof(unsigned long)];  // 读取该组的 bitmap
    // size_t bm_size_bytes = BMAPNUM_PERGROUP * BLOCK_SIZE;
    // char* raw_buffer = new char[bm_size_bytes];
    // unsigned long* bm = reinterpret_cast<unsigned long*>(raw_buffer);
    // // unsigned long* bm = (unsigned long*)smalloc(bm_size_bytes);
    // if (!bm) 
    // {
    //     log_info("[ERROR] new() failed in alloc_extents_from_group");
    //     return 0;
    // }
    // for (int j = 0; j < BMAPNUM_PERGROUP; j++) read_block(bm_start + j, bm + j * BLOCK_SIZE / sizeof(unsigned long));
    
    if (bitmap_popcount(bm, DATABLOCKS_PERGROUP) == DATABLOCKS_PERGROUP) 
    {
        // // sfree(bm);
        // delete[] raw_buffer;
        return 0;
    }

    if (cnt > (uint64_t)DATABLOCKS_PERGROUP) cnt = DATABLOCKS_PERGROUP;

    uint64_t taken = 0, current_count = 0, startbit;
    for (int i = 0; i < DATABLOCKS_PERGROUP; i++)
    {
        bool is_free = !bitmap_test(bm, i);
        if (is_free) 
        {
            if (current_count == 0) startbit = i;  // 标记空闲区起始位
            current_count++;
        }
        else if (!is_free && current_count == 0) continue;

        if ((taken + current_count == cnt) || (!is_free && current_count > 0)) 
        {
            res.push_back(Extent{ .physical_start = datablock_start + startbit, .block_count = current_count });
            bitmap_set_range(bm, startbit, current_count, 1);
            taken += current_count;
            current_count = 0;
            break;
        }
    }

    if (current_count > 0)
    {
        res.push_back(Extent{ .physical_start = datablock_start + startbit, .block_count = current_count });
        bitmap_set_range(bm, startbit, current_count, 1);
        taken += current_count;
    }

    // for (int j = 0; j < BMAPNUM_PERGROUP; j++) write_block(bm_start + j, bm + j * BLOCK_SIZE / sizeof(unsigned long));  // 其实可以优化为“只回写受影响块”（不过如果bitmap只有1块就无所谓了）
    // // sfree(bm);
    // delete[] raw_buffer;
    return taken;
}

bool alloc_extents(uint64_t lba_count, std::vector<Extent> &res)    
{
    if (lba_count == 0) return true;
    res.clear();

    struct kthread *k = myk();
	unsigned int coreid = k->curr_cpu;

    int loop = 0;
    uint64_t need = lba_count;
    while (need > 0 && loop < sb.group_num)
    {
        uint64_t got = alloc_extents_from_group(core_to_group[coreid], need, res);
        if (got >= need) { need = 0; break; }
        need -= got;
        set_newgroup(coreid);
        loop++;  // 避免死循环
    }

    return (need == 0);
}

void free_oneextent(iExtent &e, uint64_t startblk)  // free_oneextent(e, 0) 即释放整个 extent
{
    // log_info("free_oneextent() START");

    if (startblk >= e.block_count) return; // 视为成功

    BlockID  start_to_free = e.physical_start + startblk;  // 所要释放的起始块
    uint64_t count_to_free = e.block_count - startblk;     // 所要释放的块数

    int gid;
    BlockID bm_start, datablock_start;
    get_group_by_blkid(start_to_free, &gid, &bm_start, &datablock_start);
    // log_info("group id: %d, bm_start: %llu, datablock_start: %llu", gid, bm_start, datablock_start);
    

    BlockEntry* block;
	read_block(bm_start, block);
	unsigned long* bm = reinterpret_cast<unsigned long*>(block->data);

    // // DEFINE_BITMAP(bm, BMAPNUM_PERGROUP * BLOCK_SIZE * 8);
    // size_t bm_size_bytes = BMAPNUM_PERGROUP * BLOCK_SIZE;
    // // unsigned long* bm = (unsigned long*)smalloc(bm_size_bytes);
    // char* raw_buffer = new char[bm_size_bytes];
    // unsigned long* bm = reinterpret_cast<unsigned long*>(raw_buffer);
    // if (!bm) 
    // {
    //     log_info("[ERROR] new() failed in free_oneextent");
    //     return;
    // }
    // for (int j = 0; j < BMAPNUM_PERGROUP; j++) read_block(bm_start + j, (char*)bm + j * BLOCK_SIZE);

    uint64_t bit_offset = start_to_free - datablock_start;
    bitmap_set_range(bm, bit_offset, count_to_free, 0);
    // for (int j = 0; j < BMAPNUM_PERGROUP; j++) write_block(bm_start + j, (char*)bm + j * BLOCK_SIZE);  // 其实可以优化为“只回写受影响块”（不过如果bitmap只有1块就无所谓了）

    e.block_count = startblk;

    // sfree(bm);
    // delete[] raw_buffer;
    // log_info("free_oneextent() OVER");
}


void read_extentS(const std::vector<iExtent> &exts, uint64_t off, void* buf, uint64_t len)
{
    // log_info("read_extentS() START");

    if (len == 0) return;

    uint64_t taken = 0;
    for (const auto &e : exts)
    {
        uint64_t e_L = e.logical_start * BLOCK_SIZE, e_R = (e.logical_start + e.block_count) * BLOCK_SIZE;  // 该 extent 覆盖的字节区间：[e_L, e_R)
        uint64_t s = MAX(off, e_L), t = MIN(off + len, e_R);
        if (s >= t) continue;   // 该 extent 不与 [off, off+len) 重叠

        uint64_t off0  = s - e_L;
        uint64_t size0 = t - s;
        read_extent(&e, off0, (char*)buf + taken, size0);

        taken += size0;
        if (taken == len) break;
    }

    // log_info("read_extentS() OVER");
}

void write_extentS(const std::vector<iExtent> &exts, uint64_t off, const char* buf, uint64_t len)
{
    // log_info("write_extentS() START");

    if (len == 0) return;

    uint64_t remaining = len;
    for (const auto &e : exts)
    {
        uint64_t e_L = e.logical_start * BLOCK_SIZE, e_R = (e.logical_start + e.block_count) * BLOCK_SIZE;  // 该 extent 覆盖的字节区间：[e_L, e_R)
        uint64_t s = MAX(off, e_L), t = MIN(off + len, e_R);
        if (s >= t) continue;   // 该 extent 不与 [off, off+len) 重叠

        uint64_t off0  = s - e_L;
        uint64_t size0 = t - s;
        if (buf == NULL) write_extent(&e, off0, NULL, size0);  // buf 为 NULL 时填充 0
        else write_extent(&e, off0, buf + s - off, size0); 

        remaining -= size0;
        if (remaining == 0) break;
    }

    // log_info("write_extentS() OVER");
}

void ensure_coverage(std::vector<iExtent> &exts, uint64_t end)   // 确保 [0, end) 逻辑块区间被 extents 覆盖；若有缺口则分配并填充  （exts中的各extent在逻辑上应当是连续的）
{
    // log_info("ensure_coverage() START");

    uint64_t start = 0;
    if (!exts.empty()) 
    {
        iExtent& last = exts[exts.size() - 1];
        start = last.logical_start + last.block_count;
    }
    if (start >= end) return;

    // 存在缺口 [start, end)
    uint64_t need = end - start;
    std::vector<Extent> new_extents;
    if (!alloc_extents(need, new_extents))
    {
        log_info("ERROR: fail to alloc extents in ensure_coverage()");
        return;
    }

    // 将 new_extents 加入到 iExtent 中
    uint64_t L = start;
    for (const auto &e : new_extents) 
    {
        iExtent ni;
        ni.logical_start  = L;
        ni.physical_start = e.physical_start; 
		ni.block_count    = e.block_count;
        exts.push_back(ni);
        L += ni.block_count;
    }

    normalize_extent(exts);

    // log_info("ensure_coverage() OVER");
}