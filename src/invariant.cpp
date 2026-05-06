#include "invariant.hpp"

#include <algorithm>
#include <climits>
#include <sstream>

namespace {

inline int ceil_div2(int x) { return (x + 1) / 2; }

void inorder(const BNode* x, std::vector<Key>& out) {
    if (!x) return;
    if (x->leaf) {
        for (int i = 0; i < x->n; ++i) out.push_back(x->keys[i]);
        return;
    }
    for (int i = 0; i < x->n; ++i) {
        inorder(x->child[i], out);
        out.push_back(x->keys[i]);
    }
    inorder(x->child[x->n], out);
}

bool walk(const BNode* x, int d, int min_keys, int max_keys,
          bool is_root, int depth, int& leaf_depth,
          Key low, Key high, bool has_low, bool has_high,
          std::string& err) {
    if (!x) {
        err = "null node encountered";
        return false;
    }

    // (1) within-node monotonic
    for (int i = 1; i < x->n; ++i) {
        if (!(x->keys[i - 1] < x->keys[i])) {
            std::ostringstream os;
            os << "keys not strictly increasing at depth " << depth
               << ": keys[" << (i - 1) << "]=" << x->keys[i - 1]
               << " keys[" << i << "]=" << x->keys[i];
            err = os.str();
            return false;
        }
    }

    // (2) bounds from parent separators
    if (x->n > 0) {
        if (has_low && !(low < x->keys[0])) {
            std::ostringstream os;
            os << "key " << x->keys[0] << " <= lower bound " << low
               << " at depth " << depth;
            err = os.str();
            return false;
        }
        if (has_high && !(x->keys[x->n - 1] < high)) {
            std::ostringstream os;
            os << "key " << x->keys[x->n - 1] << " >= upper bound " << high
               << " at depth " << depth;
            err = os.str();
            return false;
        }
    }

    // (3) key count bounds
    if (x->n > max_keys) {
        std::ostringstream os;
        os << "node has " << x->n << " keys (> max " << max_keys
           << ") at depth " << depth;
        err = os.str();
        return false;
    }
    if (!is_root && x->n < min_keys) {
        std::ostringstream os;
        os << "non-root node has " << x->n << " keys (< min " << min_keys
           << ") at depth " << depth;
        err = os.str();
        return false;
    }

    if (x->leaf) {
        // (4) all leaves same depth
        if (leaf_depth == -1) {
            leaf_depth = depth;
        } else if (depth != leaf_depth) {
            std::ostringstream os;
            os << "leaf at depth " << depth << " differs from " << leaf_depth;
            err = os.str();
            return false;
        }
        return true;
    }

    // internal: must have n+1 children
    if (static_cast<int>(x->child.size()) != x->n + 1) {
        std::ostringstream os;
        os << "internal node has " << x->child.size() << " children, expected "
           << (x->n + 1) << " at depth " << depth;
        err = os.str();
        return false;
    }

    // recurse into children
    for (int i = 0; i <= x->n; ++i) {
        Key  child_low   = (i == 0)     ? low  : x->keys[i - 1];
        bool child_hl    = (i == 0)     ? has_low : true;
        Key  child_high  = (i == x->n)  ? high : x->keys[i];
        bool child_hh    = (i == x->n)  ? has_high : true;
        if (!walk(x->child[i], d, min_keys, max_keys,
                  /*is_root=*/false, depth + 1, leaf_depth,
                  child_low, child_high, child_hl, child_hh, err)) {
            return false;
        }
    }
    (void)d;
    return true;
}

}  // namespace

InvariantReport check_btree_like(const BNode* root, int d,
                                 const std::vector<Key>& expected_keys) {
    InvariantReport r;
    if (!root) {
        r.ok = false;
        r.error = "root is null";
        return r;
    }

    // Use the same CLRS-style mapping as BTree::BTree: t=ceil(d/2),
    // max_keys = 2t-1, min_keys = t-1. See btree.cpp for rationale.
    int t        = ceil_div2(d);
    int max_keys = 2 * t - 1;
    int min_keys = t - 1;
    int leaf_depth = -1;
    std::string err;

    if (!walk(root, d, min_keys, max_keys, /*is_root=*/true, /*depth=*/1,
              leaf_depth, 0, 0, false, false, err)) {
        r.ok = false;
        r.error = err;
        return r;
    }

    if (!expected_keys.empty()) {
        std::vector<Key> got;
        got.reserve(expected_keys.size());
        inorder(root, got);

        std::vector<Key> want = expected_keys;
        std::sort(want.begin(), want.end());

        // inorder of a B-tree is already sorted; verify
        for (size_t i = 1; i < got.size(); ++i) {
            if (!(got[i - 1] < got[i])) {
                std::ostringstream os;
                os << "inorder not sorted at i=" << i
                   << ": " << got[i - 1] << " then " << got[i];
                r.ok = false;
                r.error = os.str();
                return r;
            }
        }
        if (got.size() != want.size()) {
            std::ostringstream os;
            os << "key multiset size mismatch: tree has " << got.size()
               << ", expected " << want.size();
            r.ok = false;
            r.error = os.str();
            return r;
        }
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] != want[i]) {
                std::ostringstream os;
                os << "key mismatch at i=" << i << ": tree=" << got[i]
                   << " expected=" << want[i];
                r.ok = false;
                r.error = os.str();
                return r;
            }
        }
    }

    return r;
}

// ---------------- B+ tree invariant ----------------

namespace {

Key bplus_min_key(const void* node, bool is_leaf) {
    while (!is_leaf) {
        const auto* in = static_cast<const BPInternal*>(node);
        node    = in->child[0];
        is_leaf = in->child_is_leaf;
    }
    return static_cast<const BPLeaf*>(node)->keys[0];
}

bool bplus_walk(const void* node, bool is_leaf, int d, int min_keys, int max_keys,
                bool is_root, int depth, int& leaf_depth,
                std::vector<const BPLeaf*>& leaves_seen,
                std::string& err) {
    if (!node) { err = "null node"; return false; }

    if (is_leaf) {
        const auto* l = static_cast<const BPLeaf*>(node);
        leaves_seen.push_back(l);
        for (int i = 1; i < l->n; ++i) {
            if (!(l->keys[i - 1] < l->keys[i])) {
                std::ostringstream os;
                os << "leaf keys not strictly increasing at depth " << depth;
                err = os.str(); return false;
            }
        }
        if (l->n > max_keys) {
            std::ostringstream os;
            os << "leaf has " << l->n << " keys (> max " << max_keys << ")";
            err = os.str(); return false;
        }
        if (!is_root && l->n < min_keys) {
            std::ostringstream os;
            os << "non-root leaf has " << l->n << " keys (< min " << min_keys << ")";
            err = os.str(); return false;
        }
        if (leaf_depth == -1) leaf_depth = depth;
        else if (leaf_depth != depth) {
            std::ostringstream os;
            os << "leaf at depth " << depth << " differs from " << leaf_depth;
            err = os.str(); return false;
        }
        return true;
    }

    const auto* in = static_cast<const BPInternal*>(node);
    for (int i = 1; i < in->n; ++i) {
        if (!(in->keys[i - 1] < in->keys[i])) {
            std::ostringstream os;
            os << "internal keys not strictly increasing at depth " << depth;
            err = os.str(); return false;
        }
    }
    if (in->n > max_keys) {
        std::ostringstream os;
        os << "internal has " << in->n << " keys (> max " << max_keys << ")";
        err = os.str(); return false;
    }
    if (!is_root && in->n < min_keys) {
        std::ostringstream os;
        os << "non-root internal has " << in->n << " keys (< min " << min_keys << ")";
        err = os.str(); return false;
    }
    if (static_cast<int>(in->child.size()) != in->n + 1) {
        std::ostringstream os;
        os << "internal has " << in->child.size() << " children, expected " << (in->n + 1);
        err = os.str(); return false;
    }

    // separator: keys[i] separates child[i] (all <= sep) from child[i+1]
    // (all >= sep).  We do NOT require separator to equal min(child[i+1]);
    // it only needs to satisfy max(child[i]) <= sep <= min(child[i+1]),
    // which is sufficient for search correctness in a B+ tree.
    for (int i = 0; i < in->n; ++i) {
        Key sep = in->keys[i];
        Key right_min = bplus_min_key(in->child[i + 1], in->child_is_leaf);
        if (right_min < sep) {
            std::ostringstream os;
            os << "separator " << sep << " > min of child[" << (i + 1) << "]="
               << right_min << " at depth " << depth;
            err = os.str(); return false;
        }
        // also: max(child[i]) must be < sep (strictly, since keys are unique)
        // we cheaply check by descending to the rightmost leaf of child[i]
        const void* lc = in->child[i];
        bool lc_is_leaf = in->child_is_leaf;
        while (!lc_is_leaf) {
            const auto* lin = static_cast<const BPInternal*>(lc);
            lc = lin->child[lin->n];
            lc_is_leaf = lin->child_is_leaf;
        }
        const auto* lleaf = static_cast<const BPLeaf*>(lc);
        if (lleaf->n > 0 && !(lleaf->keys[lleaf->n - 1] < sep)) {
            std::ostringstream os;
            os << "left subtree max " << lleaf->keys[lleaf->n - 1]
               << " >= separator " << sep << " at depth " << depth;
            err = os.str(); return false;
        }
    }

    for (int i = 0; i <= in->n; ++i) {
        if (!bplus_walk(in->child[i], in->child_is_leaf, d, min_keys, max_keys,
                        false, depth + 1, leaf_depth, leaves_seen, err)) return false;
    }
    (void)d;
    return true;
}

inline int ceil_div2_local(int x) { return (x + 1) / 2; }

}  // namespace

InvariantReport check_bplus(const BPlusTree& tree, int d,
                            const std::vector<Key>& expected_keys) {
    InvariantReport r;
    int t        = ceil_div2_local(d);
    int max_keys = 2 * t - 1;
    int min_keys = t - 1;

    int leaf_depth = -1;
    std::vector<const BPLeaf*> leaves_seen;
    std::string err;

    bool root_is_leaf = tree.root_is_leaf_flag();
    const void* root_ptr = root_is_leaf
        ? static_cast<const void*>(tree.root_leaf())
        : static_cast<const void*>(tree.root_internal());
    if (!root_ptr) {
        r.ok = false; r.error = "root is null"; return r;
    }

    if (!bplus_walk(root_ptr, root_is_leaf, d, min_keys, max_keys,
                    /*is_root=*/true, 1, leaf_depth, leaves_seen, err)) {
        r.ok = false; r.error = err; return r;
    }

    // Walk linked list of leaves; collect keys; ensure every leaf was seen.
    // Use sorted vectors instead of std::set so the implementation does not
    // depend on any standard tree container (project rules forbid using
    // std::map/set/etc. for index data structures; we steer clear of them
    // entirely just to be safe).
    std::vector<Key> linked_keys;
    std::vector<const BPLeaf*> linked_leaves;
    const BPLeaf* cur = tree.first_leaf();
    while (cur) {
        linked_leaves.push_back(cur);
        for (int i = 0; i < cur->n; ++i) linked_keys.push_back(cur->keys[i]);
        cur = cur->next;
    }
    for (size_t i = 1; i < linked_keys.size(); ++i) {
        if (!(linked_keys[i - 1] < linked_keys[i])) {
            std::ostringstream os;
            os << "leaf-link sequence not strictly increasing at i=" << i
               << ": " << linked_keys[i - 1] << " then " << linked_keys[i];
            r.ok = false; r.error = os.str(); return r;
        }
    }
    {
        std::vector<const BPLeaf*> a = linked_leaves;
        std::vector<const BPLeaf*> b = leaves_seen;
        std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
        if (std::adjacent_find(a.begin(), a.end()) != a.end()) {
            r.ok = false; r.error = "leaf-link forms a cycle"; return r;
        }
        if (a != b) {
            std::ostringstream os;
            os << "leaf-link visits " << a.size() << " leaves but tree has "
               << b.size();
            r.ok = false; r.error = os.str(); return r;
        }
    }

    if (!expected_keys.empty()) {
        std::vector<Key> want = expected_keys;
        std::sort(want.begin(), want.end());
        if (linked_keys.size() != want.size()) {
            std::ostringstream os;
            os << "B+ key multiset size mismatch: tree has " << linked_keys.size()
               << ", expected " << want.size();
            r.ok = false; r.error = os.str(); return r;
        }
        for (size_t i = 0; i < linked_keys.size(); ++i) {
            if (linked_keys[i] != want[i]) {
                std::ostringstream os;
                os << "B+ key mismatch at i=" << i << ": tree=" << linked_keys[i]
                   << " expected=" << want[i];
                r.ok = false; r.error = os.str(); return r;
            }
        }
    }

    return r;
}
