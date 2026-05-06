#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "btree.hpp"      // reuse BNode
#include "iindex.hpp"
#include "record.hpp"

// B*-tree.  Same node layout as BTree (BNode), same delete policy as BTree.
// Different *insert overflow* policy:
//
//   1. Redistribute-first: when a node overflows (n == max_keys_+1), try to
//      shift one (key, rid) into a non-full sibling instead of splitting.
//   2. Two-to-three split: if both immediate siblings are full, take this
//      node + one sibling (total 2*(max_keys_+1) - 1 keys including the
//      separator) and rebalance into THREE nodes, promoting two new
//      separators to the parent.
//   3. Root has no siblings → ordinary 1-to-2 split.
//
// Same CLRS-style mapping:  t = ceil(d/2), max keys = 2t-1, min = t-1.
class BStarTree : public IIndex {
public:
    explicit BStarTree(int d);
    ~BStarTree() override;

    BStarTree(const BStarTree&)            = delete;
    BStarTree& operator=(const BStarTree&) = delete;

    void               insert(Key k, RID r) override;
    std::optional<RID> search(Key k) const override;
    void               range(Key lo, Key hi,
                             const std::function<void(Key, RID)>& cb) const override;
    bool               remove(Key k) override;

    TreeStats          stats() const override;
    int                order() const override { return d_; }
    std::string        name() const override { return "bstar"; }

    const BNode*       root_ptr() const { return root_; }

private:
    int    d_;
    int    max_keys_;
    int    min_keys_;
    BNode* root_ = nullptr;

    mutable size_t splits_         = 0;   // 1-to-2 splits
    mutable size_t two_to_three_   = 0;   // 2-to-3 splits (each counted as one)
    mutable size_t redistributes_  = 0;   // insertion-time shifts to siblings
    mutable size_t merges_         = 0;
    mutable size_t del_redists_    = 0;   // deletion-time redistributes (kept separate for analysis)

    static int lower_bound_idx(const std::vector<Key>& keys, int n, Key k);

    // ---- insert ----
    // Inserts (k, r) into x's subtree.  Returns true if the caller (parent)
    // must handle x's overflow afterwards (i.e. x ended up with > max_keys_).
    bool   insert_into(BNode* x, Key k, RID r);

    // Resolve overflow of x by redistributing or splitting.  Caller passes
    // x's parent and x's index in parent->child[].  After this returns x
    // has <= max_keys_ keys; parent may now be the overflowing one.
    void   resolve_overflow(BNode* parent, int idx);

    void   one_to_two_split(BNode* parent, int idx);   // ordinary CLRS split
    void   two_to_three_split(BNode* parent, int idx, int sib_idx);
    bool   try_redistribute_to_left(BNode* parent, int idx);
    bool   try_redistribute_to_right(BNode* parent, int idx);

    // ---- delete (B-tree style) ----
    void   remove_from(BNode* x, Key k);
    void   remove_from_leaf(BNode* x, int idx);
    void   remove_from_internal(BNode* x, int idx);
    Key    get_pred_key(BNode* x, int idx) const;
    RID    get_pred_rid(BNode* x, int idx) const;
    Key    get_succ_key(BNode* x, int idx) const;
    RID    get_succ_rid(BNode* x, int idx) const;
    int    ensure_child_has_min(BNode* x, int idx);
    void   borrow_from_left(BNode* x, int idx);
    void   borrow_from_right(BNode* x, int idx);
    void   merge_children(BNode* x, int idx);

    // ---- traversal / cleanup ----
    void   free_subtree(BNode* x);
    void   count_nodes(const BNode* x, size_t& internal, size_t& leaf,
                       size_t& keys, size_t& height, size_t depth) const;
    void   range_walk(const BNode* x, Key lo, Key hi,
                      const std::function<void(Key, RID)>& cb) const;
};
