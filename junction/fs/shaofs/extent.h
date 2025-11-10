#pragma once

#include "base.h"
#include "disk.h"
#include <vector>

bool alloc_extents(uint64_t lba_count, std::vector<Extent> &res);
void load_all_extents(DInode &di, std::vector<iExtent> &out);
void store_all_extents(DInode &di, std::vector<iExtent> &exts);
void ensure_coverage(std::vector<iExtent> &exts, uint64_t end);
void normalize_extent(std::vector<iExtent> &exts);
void read_extentS(const std::vector<iExtent> &exts, uint64_t off, void* buf, uint64_t len);
void write_extentS(const std::vector<iExtent> &exts, uint64_t off, const char *buf, uint64_t len);

void free_oneextent(iExtent &e, uint64_t startblk);