#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#include "bplus_tree.hpp"
#include "bstar_tree.hpp"
#include "btree.hpp"
#include "csv_loader.hpp"
#include "experiments.hpp"
#include "iindex.hpp"
#include "invariant.hpp"
#include "record.hpp"
#include "timer.hpp"

namespace {

struct Args {
    std::unordered_map<std::string, std::string> kv;

    bool has(const std::string& k) const { return kv.find(k) != kv.end(); }
    std::string get(const std::string& k, const std::string& def = {}) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    int    geti(const std::string& k, int def) const { return has(k) ? std::stoi(kv.at(k)) : def; }
    double getd(const std::string& k, double def) const { return has(k) ? std::stod(kv.at(k)) : def; }
};

Args parse_args(int argc, char** argv, int start) {
    Args a;
    for (int i = start; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) != 0) continue;
        auto eq = s.find('=');
        if (eq == std::string::npos) {
            a.kv[s.substr(2)] = "1";
        } else {
            a.kv[s.substr(2, eq - 2)] = s.substr(eq + 1);
        }
    }
    return a;
}

void print_usage() {
    std::cerr <<
        "usage: ./run <subcommand> [--key=value ...]\n"
        "subcommands:\n"
        "  load    --csv=PATH                          sanity-check CSV\n"
        "  test    --tree=TYPE --d=N [--csv=...]       self-test\n"
        "  insert  --tree=TYPE --d=N --csv=...         [--out=PATH]\n"
        "  search  --tree=TYPE --d=N --csv=... --queries=Q --seed=S [--out=PATH]\n"
        "  range   --tree=TYPE --d=N --csv=... --lo=K --hi=K [--repeats=N]\n"
        "                    [--reduce=min|median|mean] [--out=PATH]\n"
        "  delete  --tree=TYPE --d=N --csv=... --ratio=R --seed=S [--out=PATH]\n"
        "  all     --csv=... [--out-dir=DIR] [--seed=S] [--queries=Q]\n"
        "                    [--lo=K] [--hi=K] [--ratios=A,B,...] [--ds=A,B,...]\n"
        "  range-sweep --csv=... [--d=N] [--lo=K] [--out=PATH]   (bonus)\n"
        "TYPE in {btree, bstar, bplus}\n"
        "Defaults: out-dir=results, seed=42, queries=10000,\n"
        "          lo=202000000, hi=202010000, ratios=0.02,0.10,0.20, ds=3,5,10\n";
}

int cmd_load(const Args& a) {
    std::string csv = a.get("csv", "student.csv");
    Timer t; t.start();
    auto recs = load_csv(csv);
    double ms = t.elapsed_ms();
    std::cout << "loaded " << recs.size() << " records from " << csv
              << " in " << ms << " ms\n";
    if (!recs.empty()) {
        const auto& f = recs.front();
        const auto& b = recs.back();
        std::cout << "first: id=" << f.id << " name=\"" << f.name
                  << "\" gender=" << (f.male ? "Male" : "Female")
                  << " gpa=" << f.gpa << " h=" << f.height << " w=" << f.weight << "\n";
        std::cout << "last:  id=" << b.id << " name=\"" << b.name
                  << "\" gender=" << (b.male ? "Male" : "Female")
                  << " gpa=" << b.gpa << " h=" << b.height << " w=" << b.weight << "\n";
    }
    return 0;
}

// Run the appropriate invariant check for whichever tree is loaded.
InvariantReport check_any(IIndex& tree, int d, const std::vector<Key>& expected) {
    if (auto* bt = dynamic_cast<BTree*>(&tree))
        return check_btree_like(bt->root_ptr(), d, expected);
    if (auto* bs = dynamic_cast<BStarTree*>(&tree))
        return check_btree_like(bs->root_ptr(), d, expected);
    if (auto* bp = dynamic_cast<BPlusTree*>(&tree))
        return check_bplus(*bp, d, expected);
    InvariantReport r;
    r.ok = true;
    return r;
}

// Phase 1+2 self-test (tree-agnostic).
int cmd_test(const Args& a) {
    std::string kind = a.get("tree", "btree");
    int         d    = a.geti("d", 5);
    std::string csv  = a.get("csv", "student.csv");
    uint32_t    seed = static_cast<uint32_t>(a.geti("seed", 42));
    double      del  = a.getd("delete-ratio", 0.5);

    std::cout << "[test] tree=" << kind << " d=" << d << " seed=" << seed
              << " delete-ratio=" << del << "\n";

    // ---- (a) tiny manual sanity: d, 20 random keys ----
    {
        auto t1 = make_index(kind, d);
        std::vector<Key> small = {
            50, 20, 70, 10, 30, 60, 80, 5, 15, 25,
            35, 55, 65, 75, 85, 1, 12, 27, 33, 90};
        for (size_t i = 0; i < small.size(); ++i) t1->insert(small[i], static_cast<RID>(i));
        for (size_t i = 0; i < small.size(); ++i) {
            auto r = t1->search(small[i]);
            if (!r || *r != static_cast<RID>(i)) {
                std::cerr << "[test] FAIL small search: key " << small[i] << "\n";
                return 10;
            }
        }
        auto rep = check_any(*t1, d, small);
        if (!rep) {
            std::cerr << "[test] FAIL small invariant: " << rep.error << "\n";
            return 11;
        }
        std::cout << "[test] small (20 keys) ok\n";
    }

    // ---- (b) load full CSV ----
    auto recs = load_csv(csv);
    std::cout << "[test] loaded " << recs.size() << " records\n";
    auto tree = make_index(kind, d);

    Timer ti; ti.start();
    for (size_t rid = 0; rid < recs.size(); ++rid) tree->insert(recs[rid].id, static_cast<RID>(rid));
    double ins_ms = ti.elapsed_ms();
    auto s1 = tree->stats();
    std::cout << "[test] insert " << recs.size() << " in " << ins_ms << " ms"
              << " | height=" << s1.height
              << " nodes(I/L)=" << s1.internal_nodes << "/" << s1.leaf_nodes
              << " keys=" << s1.total_keys
              << " splits=" << s1.splits
              << " util=" << s1.utilization(d) << "\n";

    if (s1.total_keys != recs.size()) {
        std::cerr << "[test] FAIL total_keys " << s1.total_keys << " != "
                  << recs.size() << "\n";
        return 12;
    }

    // ---- (c) all keys searchable ----
    Timer ts; ts.start();
    size_t hit = 0;
    for (const auto& r : recs) if (tree->search(r.id)) ++hit;
    double sea_ms = ts.elapsed_ms();
    std::cout << "[test] search-all " << hit << "/" << recs.size()
              << " in " << sea_ms << " ms ("
              << (sea_ms * 1000.0 / static_cast<double>(recs.size())) << " us/op)\n";
    if (hit != recs.size()) { std::cerr << "[test] FAIL search-all\n"; return 13; }

    // invariant after full insert
    {
        std::vector<Key> all; all.reserve(recs.size());
        for (auto& r : recs) all.push_back(r.id);
        auto rep = check_any(*tree, d, all);
        if (!rep) {
            std::cerr << "[test] FAIL invariant after insert: " << rep.error << "\n";
            return 14;
        }
        std::cout << "[test] invariant after insert ok\n";

        // tree-agnostic range query sanity (10K window on the dataset)
        Key lo = 202'000'000, hi = 202'010'000;
        size_t cnt = 0;
        tree->range(lo, hi, [&](Key, RID){ ++cnt; });
        size_t expected_in_range = 0;
        for (auto& r : recs) if (r.id >= lo && r.id <= hi) ++expected_in_range;
        if (cnt != expected_in_range) {
            std::cerr << "[test] FAIL range count " << cnt << " != "
                      << expected_in_range << "\n";
            return 21;
        }
        std::cout << "[test] range [" << lo << "," << hi << "] hit "
                  << cnt << " ok\n";
    }

    // ---- (d) delete `del` fraction, then invariant + search remaining ----
    std::vector<Key> keys; keys.reserve(recs.size());
    for (auto& r : recs) keys.push_back(r.id);
    std::mt19937 rng(seed);
    std::shuffle(keys.begin(), keys.end(), rng);
    size_t to_del = static_cast<size_t>(static_cast<double>(keys.size()) * del);

    Timer td; td.start();
    size_t removed = 0;
    for (size_t i = 0; i < to_del; ++i) if (tree->remove(keys[i])) ++removed;
    double del_ms = td.elapsed_ms();
    auto s2 = tree->stats();
    std::cout << "[test] delete " << removed << "/" << to_del << " in "
              << del_ms << " ms"
              << " | height=" << s2.height
              << " nodes(I/L)=" << s2.internal_nodes << "/" << s2.leaf_nodes
              << " keys=" << s2.total_keys
              << " merges=" << s2.merges
              << " redistributes=" << s2.redistributes << "\n";
    if (removed != to_del) { std::cerr << "[test] FAIL deletion count\n"; return 15; }
    if (s2.total_keys + to_del != recs.size()) {
        std::cerr << "[test] FAIL total_keys after delete: " << s2.total_keys
                  << " + " << to_del << " != " << recs.size() << "\n";
        return 16;
    }

    // remaining should all be searchable; deleted should all miss
    size_t hit2 = 0, miss = 0;
    for (size_t i = 0; i < to_del; ++i) if (tree->search(keys[i])) ++hit2;
    for (size_t i = to_del; i < keys.size(); ++i) if (tree->search(keys[i])) ++miss;
    if (hit2 != 0) { std::cerr << "[test] FAIL deleted-still-found=" << hit2 << "\n"; return 17; }
    if (miss != keys.size() - to_del) {
        std::cerr << "[test] FAIL remaining-search " << miss << " != "
                  << (keys.size() - to_del) << "\n";
        return 18;
    }
    std::cout << "[test] post-delete searches ok\n";

    {
        std::vector<Key> remaining(keys.begin() + to_del, keys.end());
        auto rep = check_any(*tree, d, remaining);
        if (!rep) {
            std::cerr << "[test] FAIL invariant after delete: " << rep.error << "\n";
            return 19;
        }
        std::cout << "[test] invariant after delete ok\n";
    }

    std::cout << "[test] PASS\n";
    return 0;
}

// Open in append mode and write a header if (and only if) the file is
// empty (fresh).  Returns the open ofstream.
std::ofstream open_with_header(const std::string& path,
                               void (*hdr)(std::ostream&)) {
    bool fresh = false;
    {
        struct stat st{};
        fresh = (path.empty() || ::stat(path.c_str(), &st) != 0 || st.st_size == 0);
    }
    std::ofstream f(path, std::ios::app);
    if (!f) throw std::runtime_error("cannot open output: " + path);
    if (fresh) hdr(f);
    return f;
}

void ensure_dir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) return;
    // recursive mkdir — handle nested paths like "results/sweep"
    auto pos = path.find('/');
    while (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (!parent.empty() && ::stat(parent.c_str(), &st) != 0) {
            ::mkdir(parent.c_str(), 0755);
        }
        pos = path.find('/', pos + 1);
    }
    ::mkdir(path.c_str(), 0755);
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

int cmd_insert(const Args& a) {
    auto recs = load_csv(a.get("csv", "student.csv"));
    std::string out_path = a.get("out", "");
    std::string kind = a.get("tree", "btree");
    int         d    = a.geti("d", 5);
    if (out_path.empty()) {
        exp_insert(recs, kind, d, std::cout);
    } else {
        auto f = open_with_header(out_path, maybe_write_header_insert);
        exp_insert(recs, kind, d, f);
    }
    return 0;
}

int cmd_search(const Args& a) {
    auto recs = load_csv(a.get("csv", "student.csv"));
    std::string out_path = a.get("out", "");
    std::string kind = a.get("tree", "btree");
    int         d    = a.geti("d", 5);
    int         q    = a.geti("queries", 10000);
    uint32_t    seed = static_cast<uint32_t>(a.geti("seed", 42));
    if (out_path.empty()) {
        exp_search(recs, kind, d, q, seed, std::cout);
    } else {
        auto f = open_with_header(out_path, maybe_write_header_search);
        exp_search(recs, kind, d, q, seed, f);
    }
    return 0;
}

int cmd_range(const Args& a) {
    auto recs = load_csv(a.get("csv", "student.csv"));
    std::string out_path = a.get("out", "");
    std::string kind = a.get("tree", "btree");
    int         d    = a.geti("d", 5);
    Key         lo   = static_cast<Key>(a.geti("lo", 202000000));
    Key         hi   = static_cast<Key>(a.geti("hi", 202010000));
    int         reps = a.geti("repeats", 1);
    std::string red  = a.get("reduce", "min");
    if (out_path.empty()) {
        exp_range(recs, kind, d, lo, hi, std::cout, reps, red);
    } else {
        auto f = open_with_header(out_path, maybe_write_header_range);
        exp_range(recs, kind, d, lo, hi, f, reps, red);
    }
    return 0;
}

int cmd_delete(const Args& a) {
    auto recs = load_csv(a.get("csv", "student.csv"));
    std::string out_path = a.get("out", "");
    std::string kind = a.get("tree", "btree");
    int         d    = a.geti("d", 5);
    double      r    = a.getd("ratio", 0.10);
    uint32_t    seed = static_cast<uint32_t>(a.geti("seed", 42));
    if (out_path.empty()) {
        exp_delete(recs, kind, d, r, seed, std::cout);
    } else {
        auto f = open_with_header(out_path, maybe_write_header_delete);
        exp_delete(recs, kind, d, r, seed, f);
    }
    return 0;
}

// Bonus experiment: range-window sweep. Vary window width to expose the
// crossover where B+ tree's leaf-link advantage starts to dominate. We
// pick window upper bounds that yield ~target hit counts, then run all
// three trees at d=5 (default) for each window.
int cmd_range_sweep(const Args& a) {
    auto recs = load_csv(a.get("csv", "student.csv"));
    std::string out_path = a.get("out", "results/range_sweep.csv");
    int         d        = a.geti("d", 5);
    Key         lo       = static_cast<Key>(a.geti("lo", 202000000));

    // window upper bounds covering ~100, 1k, 10k, 50k, 100k hits
    std::vector<Key> his = {
        static_cast<Key>(lo + 1000),
        static_cast<Key>(lo + 10000),
        static_cast<Key>(lo + 100000),
        static_cast<Key>(lo + 500000),
        static_cast<Key>(lo + 1000000),
    };

    int         reps = a.geti("repeats", 5);
    std::string red  = a.get("reduce", "min");
    auto f = open_with_header(out_path, maybe_write_header_range);
    for (Key hi : his) {
        for (auto& kind : std::vector<std::string>{"btree", "bstar", "bplus"}) {
            exp_range(recs, kind, d, lo, hi, f, reps, red);
        }
    }
    std::cout << "[range-sweep] wrote " << out_path
              << " (d=" << d << ", lo=" << lo
              << ", " << his.size() << " window sizes x 3 trees)\n";
    return 0;
}

int cmd_all(const Args& a) {
    std::string csv     = a.get("csv", "student.csv");
    std::string out_dir = a.get("out-dir", "results");
    uint32_t    seed    = static_cast<uint32_t>(a.geti("seed", 42));
    int         queries = a.geti("queries", 10000);
    Key         lo      = static_cast<Key>(a.geti("lo", 202000000));
    Key         hi      = static_cast<Key>(a.geti("hi", 202010000));
    int         reps    = a.geti("repeats", 5);   // default 5 for stable curves
    std::string red     = a.get("reduce", "min"); // min: robust to jitter

    std::vector<int> ds;
    for (auto& s : split_csv(a.get("ds", "3,5,10"))) ds.push_back(std::stoi(s));
    std::vector<double> ratios;
    for (auto& s : split_csv(a.get("ratios", "0.02,0.10,0.20")))
        ratios.push_back(std::stod(s));
    std::vector<std::string> kinds = {"btree", "bstar", "bplus"};

    ensure_dir(out_dir);
    std::string p_ins = out_dir + "/insert.csv";
    std::string p_sea = out_dir + "/search.csv";
    std::string p_rng = out_dir + "/range.csv";
    std::string p_del = out_dir + "/delete.csv";
    // Wipe stale files so each `all` run produces a clean dataset.
    std::ofstream(p_ins, std::ios::trunc);
    std::ofstream(p_sea, std::ios::trunc);
    std::ofstream(p_rng, std::ios::trunc);
    std::ofstream(p_del, std::ios::trunc);

    auto recs = load_csv(csv);
    std::cout << "[all] loaded " << recs.size() << " records from " << csv
              << "; out-dir=" << out_dir << "\n";

    for (auto& kind : kinds) {
        for (int d : ds) {
            std::cout << "[all] " << kind << " d=" << d << " ..." << std::flush;
            Timer t; t.start();
            {
                auto f = open_with_header(p_ins, maybe_write_header_insert);
                exp_insert(recs, kind, d, f);
            }
            {
                auto f = open_with_header(p_sea, maybe_write_header_search);
                exp_search(recs, kind, d, queries, seed, f);
            }
            {
                auto f = open_with_header(p_rng, maybe_write_header_range);
                exp_range(recs, kind, d, lo, hi, f, reps, red);
            }
            for (double r : ratios) {
                auto f = open_with_header(p_del, maybe_write_header_delete);
                exp_delete(recs, kind, d, r, seed, f);
            }
            std::cout << " done in " << t.elapsed_ms() << " ms\n";
        }
    }
    std::cout << "[all] outputs:\n"
              << "  " << p_ins << "\n  " << p_sea << "\n  "
              << p_rng << "\n  " << p_del << "\n";

    // Cross-tree parity sanity for range queries: brute-force the same
    // window once and compare against every row in range.csv.
    size_t bf_hits = 0, bf_male = 0;
    double bf_gpa = 0, bf_h = 0;
    for (const auto& r : recs) {
        if (r.id >= lo && r.id <= hi) {
            ++bf_hits;
            if (r.male) { ++bf_male; bf_gpa += r.gpa; bf_h += r.height; }
        }
    }
    std::cout << "[all] brute-force range parity: hits=" << bf_hits
              << " male_hits=" << bf_male
              << " avg_gpa=" << (bf_male ? bf_gpa / bf_male : 0.0)
              << " avg_height=" << (bf_male ? bf_h / bf_male : 0.0) << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string sub = argv[1];
    Args        a   = parse_args(argc, argv, 2);

    try {
        if (sub == "load")   return cmd_load(a);
        if (sub == "test")   return cmd_test(a);
        if (sub == "insert") return cmd_insert(a);
        if (sub == "search") return cmd_search(a);
        if (sub == "range")  return cmd_range(a);
        if (sub == "delete") return cmd_delete(a);
        if (sub == "all")    return cmd_all(a);
        if (sub == "range-sweep") return cmd_range_sweep(a);
        if (sub == "-h" || sub == "--help" || sub == "help") {
            print_usage();
            return 0;
        }
        std::cerr << "unknown subcommand: " << sub << "\n";
        print_usage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 3;
    }
}
