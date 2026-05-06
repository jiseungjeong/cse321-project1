#include "experiments.hpp"

#include <algorithm>
#include <random>

#include "iindex.hpp"
#include "timer.hpp"

namespace {

// leaf utilization: only counts leaf nodes' keys / leaf capacity.
// For BTree / BStarTree internal nodes also hold (key, RID) pairs, but the
// leaf-only metric is comparable across all three trees.  Reported alongside
// the "all-nodes" utilization that comes from TreeStats::utilization().
double leaf_util(const TreeStats& s, int d) {
    if (s.leaf_nodes == 0) return 0.0;
    // For BPlusTree: total_keys IS leaf-key count (internal separators
    //   are not counted; see bplus_tree.cpp).
    // For BTree / BStarTree: total_keys includes internal keys too, so
    //   leaf-only key count is not directly available from TreeStats.
    //   We approximate by total_keys * leaf_nodes / total_nodes — only used
    //   as a rough cross-tree comparison; the canonical metric remains the
    //   raw TreeStats::utilization(d).
    size_t total_nodes = s.total_nodes();
    if (total_nodes == 0) return 0.0;
    double leaf_keys_est = static_cast<double>(s.total_keys)
                           * static_cast<double>(s.leaf_nodes)
                           / static_cast<double>(total_nodes);
    return leaf_keys_est / (static_cast<double>(s.leaf_nodes)
                            * static_cast<double>(d - 1));
}

void build_full(IIndex& tree, const std::vector<Record>& recs) {
    for (size_t rid = 0; rid < recs.size(); ++rid) {
        tree.insert(recs[rid].id, static_cast<RID>(rid));
    }
}

}  // namespace

// ---------------- header helpers ----------------

void maybe_write_header_insert(std::ostream& out) {
    out << "kind,d,records,insert_us,height,internal_nodes,leaf_nodes,"
           "total_keys,splits,redistributes,utilization,leaf_utilization\n";
}
void maybe_write_header_search(std::ostream& out) {
    out << "kind,d,queries,seed,hits,total_us,avg_us\n";
}
void maybe_write_header_range(std::ostream& out) {
    out << "kind,d,lo,hi,hits,male_hits,avg_gpa_male,avg_height_male,total_us\n";
}
void maybe_write_header_delete(std::ostream& out) {
    out << "kind,d,ratio,deleted,delete_us,height_after,nodes_after,"
           "keys_after,merges,redistributes\n";
}

// ---------------- Experiment 1: insert ----------------

void exp_insert(const std::vector<Record>& recs,
                std::string_view kind, int d, std::ostream& out) {
    auto tree = make_index(kind, d);

    Timer t; t.start();
    build_full(*tree, recs);
    double us = t.elapsed_us();

    auto s = tree->stats();
    out << kind << "," << d << "," << recs.size() << ","
        << us << "," << s.height << ","
        << s.internal_nodes << "," << s.leaf_nodes << ","
        << s.total_keys << "," << s.splits << "," << s.redistributes << ","
        << s.utilization(d) << "," << leaf_util(s, d) << "\n";
}

// ---------------- Experiment 2: point search ----------------

void exp_search(const std::vector<Record>& recs,
                std::string_view kind, int d, int queries,
                uint32_t seed, std::ostream& out) {
    auto tree = make_index(kind, d);
    build_full(*tree, recs);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> u(0, recs.size() - 1);
    std::vector<Key> qs;
    qs.reserve(queries);
    for (int i = 0; i < queries; ++i) qs.push_back(recs[u(rng)].id);

    // warm-up: 1000 lookups (or all if queries < 1000)
    int warm = std::min(1000, queries);
    for (int i = 0; i < warm; ++i) (void)tree->search(qs[i % queries]);

    Timer t; t.start();
    size_t hits = 0;
    for (Key k : qs) if (tree->search(k)) ++hits;
    double us = t.elapsed_us();

    out << kind << "," << d << "," << queries << "," << seed << ","
        << hits << "," << us << ","
        << (queries > 0 ? us / queries : 0.0) << "\n";
}

// ---------------- Experiment 3: range query ----------------

void exp_range(const std::vector<Record>& recs,
               std::string_view kind, int d,
               Key lo, Key hi, std::ostream& out,
               int repeats, std::string_view reduce) {
    auto tree = make_index(kind, d);
    build_full(*tree, recs);

    auto run_once = [&](size_t& hits, size_t& male_hits,
                        double& sum_gpa, double& sum_h) -> double {
        hits = male_hits = 0;
        sum_gpa = sum_h = 0.0;
        Timer t; t.start();
        tree->range(lo, hi, [&](Key, RID r) {
            const Record& rec = recs[r];
            ++hits;
            if (rec.male) {
                ++male_hits;
                sum_gpa += rec.gpa;
                sum_h   += rec.height;
            }
        });
        return t.elapsed_us();
    };

    if (repeats < 1) repeats = 1;

    // untimed warm-up so the first measurement is not penalised by a
    // cold cache (only meaningful when we actually average / minimise)
    size_t hits = 0, male_hits = 0;
    double sum_gpa = 0.0, sum_h = 0.0;
    if (repeats > 1) (void)run_once(hits, male_hits, sum_gpa, sum_h);

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        samples.push_back(run_once(hits, male_hits, sum_gpa, sum_h));
    }

    double us;
    if (reduce == "median") {
        std::sort(samples.begin(), samples.end());
        size_t n = samples.size();
        us = (n % 2)
            ? samples[n / 2]
            : 0.5 * (samples[n / 2 - 1] + samples[n / 2]);
    } else if (reduce == "mean") {
        double s = 0.0;
        for (double v : samples) s += v;
        us = s / static_cast<double>(samples.size());
    } else {
        // default: min — most robust against scheduler / cache jitter
        us = *std::min_element(samples.begin(), samples.end());
    }

    double avg_gpa = male_hits ? sum_gpa / static_cast<double>(male_hits) : 0.0;
    double avg_h   = male_hits ? sum_h   / static_cast<double>(male_hits) : 0.0;

    out << kind << "," << d << "," << lo << "," << hi << ","
        << hits << "," << male_hits << ","
        << avg_gpa << "," << avg_h << "," << us << "\n";
}

// ---------------- Experiment 4: delete ----------------

void exp_delete(const std::vector<Record>& recs,
                std::string_view kind, int d,
                double ratio, uint32_t seed, std::ostream& out) {
    auto tree = make_index(kind, d);
    build_full(*tree, recs);

    std::vector<Key> ks;
    ks.reserve(recs.size());
    for (const auto& r : recs) ks.push_back(r.id);

    std::mt19937 rng(seed);
    std::shuffle(ks.begin(), ks.end(), rng);

    size_t to_del = static_cast<size_t>(static_cast<double>(ks.size()) * ratio);

    Timer t; t.start();
    size_t deleted = 0;
    for (size_t i = 0; i < to_del; ++i) {
        if (tree->remove(ks[i])) ++deleted;
    }
    double us = t.elapsed_us();

    auto s = tree->stats();
    out << kind << "," << d << "," << ratio << "," << deleted << ","
        << us << "," << s.height << "," << s.total_nodes() << ","
        << s.total_keys << "," << s.merges << "," << s.redistributes << "\n";
}
