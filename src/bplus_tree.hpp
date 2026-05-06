#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "iindex.hpp"
#include "record.hpp"

// B+ tree.
// Internal nodes hold separator keys + child pointers only (no RIDs).
// Leaf nodes hold (key, RID) pairs and are connected via a singly-linked list
// for efficient range scans.
//
// Same CLRS-style mapping as BTree: t = ceil(d/2), max keys = 2t-1,
// min keys (non-root) = t-1.  See plan.md §0 / btree.cpp.
//
// Internal split: middle key MOVES up (separator) — internal nodes never
//   keep duplicates of leaf keys (a separator key is itself a guidepost,
//   not a payload key, so when it equals a leaf key only the leaf carries
//   the RID).
// Leaf split: right leaf's first key is COPIED up as separator.

struct BPInternal;
struct BPLeaf;

struct BPInternal {
    int                       n = 0;
    std::vector<Key>          keys;     // n entries
    std::vector<void*>        child;    // n+1 entries (BPInternal* or BPLeaf*)
    bool                      child_is_leaf = false;
    BPInternal*               parent = nullptr;
};

struct BPLeaf {
    int                       n = 0;
    std::vector<Key>          keys;
    std::vector<RID>          rids;
    BPLeaf*                   next   = nullptr;
    BPInternal*               parent = nullptr;
};

class BPlusTree : public IIndex {
public:
    explicit BPlusTree(int d);
    ~BPlusTree() override;

    BPlusTree(const BPlusTree&)            = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    void               insert(Key k, RID r) override;
    std::optional<RID> search(Key k) const override;
    void               range(Key lo, Key hi,
                             const std::function<void(Key, RID)>& cb) const override;
    bool               remove(Key k) override;

    TreeStats          stats() const override;
    int                order() const override { return d_; }
    std::string        name() const override { return "bplus"; }

    // structural accessors for invariant checking
    bool               root_is_leaf_flag() const { return root_is_leaf_; }
    const BPInternal*  root_internal()  const { return root_is_leaf_ ? nullptr : reinterpret_cast<const BPInternal*>(root_); }
    const BPLeaf*      root_leaf()      const { return root_is_leaf_ ? reinterpret_cast<const BPLeaf*>(root_) : nullptr; }
    const BPLeaf*      first_leaf()     const { return first_leaf_; }

private:
    int      d_;
    int      max_keys_;     // 2t - 1
    int      min_keys_;     // t - 1
    void*    root_         = nullptr;
    bool     root_is_leaf_ = true;
    BPLeaf*  first_leaf_   = nullptr;

    mutable size_t splits_        = 0;
    mutable size_t redistributes_ = 0;
    mutable size_t merges_        = 0;

    static int lower_bound_idx(const std::vector<Key>& keys, int n, Key k);
    static int upper_bound_idx(const std::vector<Key>& keys, int n, Key k);

    BPLeaf*       find_leaf(Key k) const;

    // returns (separator_key, new_right_node) when split happened
    struct InsertResult {
        bool   split = false;
        Key    sep   = 0;
        void*  right = nullptr;  // BPLeaf* or BPInternal*
    };
    InsertResult insert_into_leaf(BPLeaf* leaf, Key k, RID r);
    InsertResult insert_into_internal(BPInternal* node, Key k, RID r);

    void          split_leaf(BPLeaf* leaf, BPLeaf*& out_right, Key& out_sep);
    void          split_internal(BPInternal* node, BPInternal*& out_right, Key& out_sep);

    // delete helpers
    bool          remove_from_leaf(BPLeaf* leaf, Key k);
    bool          remove_descend(BPInternal* node, Key k);
    void          fix_underflow_internal_child(BPInternal* parent, int idx);
    void          fix_underflow_leaf_child(BPInternal* parent, int idx);

    // borrow/merge for internal children
    void          borrow_left_internal(BPInternal* parent, int idx);
    void          borrow_right_internal(BPInternal* parent, int idx);
    void          merge_internal(BPInternal* parent, int idx);   // merges child[idx] and child[idx+1]

    // borrow/merge for leaf children
    void          borrow_left_leaf(BPInternal* parent, int idx);
    void          borrow_right_leaf(BPInternal* parent, int idx);
    void          merge_leaf(BPInternal* parent, int idx);

    // cleanup / stats
    void          free_subtree(void* node, bool is_leaf);
    void          count_nodes(const void* node, bool is_leaf, size_t depth,
                              size_t& internal, size_t& leaf, size_t& keys,
                              size_t& height) const;
};
