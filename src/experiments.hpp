#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "record.hpp"

// Each experiment appends one CSV row to `out`.  All rows include the
// (kind, d) prefix and a header is written by `write_header_*` at file
// open time (in main.cpp / run_all.sh).
//
// Seeds are explicit so the same workload is reproducible across trees.

void exp_insert(const std::vector<Record>& recs,
                std::string_view kind, int d, std::ostream& out);

void exp_search(const std::vector<Record>& recs,
                std::string_view kind, int d, int queries,
                uint32_t seed, std::ostream& out);

// Range query experiment.  When `repeats > 1` the query is run multiple
// times back-to-back and the reported `total_us` is reduced via
// `reduce` ∈ {"min", "median", "mean"}.  An untimed warm-up pass runs
// before measurement so that the first sample is not artificially slow
// from a cold cache.  The default reduce mode is "min", which is the
// most robust against scheduler / cache jitter (those only push samples
// upward).
void exp_range(const std::vector<Record>& recs,
               std::string_view kind, int d,
               Key lo, Key hi, std::ostream& out,
               int repeats = 1, std::string_view reduce = "min");

void exp_delete(const std::vector<Record>& recs,
                std::string_view kind, int d,
                double ratio, uint32_t seed, std::ostream& out);

// Header writers (idempotent per file): emit only when file is empty.
void maybe_write_header_insert(std::ostream& out);
void maybe_write_header_search(std::ostream& out);
void maybe_write_header_range(std::ostream& out);
void maybe_write_header_delete(std::ostream& out);
