#pragma once

#include <string>
#include <vector>

#include "bplus_tree.hpp"
#include "btree.hpp"
#include "record.hpp"

struct InvariantReport {
    bool        ok = true;
    std::string error;

    operator bool() const { return ok; }
};

// Check structural invariants for a BTree (also used by B*-tree later — they
// share BNode layout).
//
//  1. keys monotonically increasing within each node
//  2. child[i]'s keys all in (parent.keys[i-1], parent.keys[i])
//  3. node key count: root in [0, max]; non-root in [min, max]
//     where t = ceil(d/2), min = t-1, max = 2t-1
//  4. all leaves at same depth
//  5. (B+ specific — verified separately, see check_bplus)
//  6. inorder traversal yields exactly `expected_keys` (multiset equality
//     check; pass empty vector to skip)
InvariantReport check_btree_like(const BNode* root, int d,
                                 const std::vector<Key>& expected_keys = {});

// Check invariants for a B+ tree.  Adds two B+-specific checks on top of
// the generic ones:
//   - separator keys[i] equals the smallest key in subtree child[i+1] for
//     internal nodes (canonical B+ separator placement)
//   - linked list of leaves visits every leaf exactly once and yields
//     keys in sorted order; multiset matches `expected_keys` if given
InvariantReport check_bplus(const BPlusTree& tree, int d,
                            const std::vector<Key>& expected_keys = {});
