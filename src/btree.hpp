#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "iindex.hpp"
#include "record.hpp"

// B-tree node. Internal and leaf use the same layout.
//   keys[0..n-1], rids[0..n-1] paired (record stored in-node, may be returned
//   from any level — search may terminate at internal nodes per spec).
//   child[0..n] valid only when leaf == false.
//
// Order d = max number of children (Knuth definition). See plan.md §0.
//   max keys per node     = d - 1
//   min keys (non-root)   = ceil(d/2) - 1
//   max children          = d
//   min children          = ceil(d/2)
struct BNode {
    bool                 leaf;
    int                  n = 0;
    std::vector<Key>     keys;
    std::vector<RID>     rids;
    std::vector<BNode*>  child;   // size n+1 when internal, empty when leaf
    BNode*               parent = nullptr;

    explicit BNode(bool is_leaf) : leaf(is_leaf) {}
};

class BTree : public IIndex {
public:
    explicit BTree(int d);
    ~BTree() override;

    BTree(const BTree&)            = delete;
    BTree& operator=(const BTree&) = delete;

    void               insert(Key k, RID r) override;
    std::optional<RID> search(Key k) const override;
    void               range(Key lo, Key hi,
                             const std::function<void(Key, RID)>& cb) const override;
    bool               remove(Key k) override;

    TreeStats          stats() const override;
    int                order() const override { return d_; }
    std::string        name() const override { return "btree"; }

    // expose root for invariant checking
    const BNode* root_ptr() const { return root_; }

private:
    int    d_;             // max children
    int    max_keys_;      // d - 1
    int    min_keys_;      // ceil(d/2) - 1  (root may have fewer)
    BNode* root_ = nullptr;

    // counters (mutable so const stats() can refresh node counts)
    mutable size_t splits_        = 0;
    mutable size_t redistributes_ = 0;
    mutable size_t merges_        = 0;

    // helpers
    static int  lower_bound_idx(const std::vector<Key>& keys, int n, Key k);
    void        split_child(BNode* parent, int idx);
    void        insert_nonfull(BNode* x, Key k, RID r);

    // delete helpers
    void        remove_from(BNode* x, Key k);
    void        remove_from_leaf(BNode* x, int idx);
    void        remove_from_internal(BNode* x, int idx);
    Key         get_pred_key(BNode* x, int idx) const;
    RID         get_pred_rid(BNode* x, int idx) const;
    Key         get_succ_key(BNode* x, int idx) const;
    RID         get_succ_rid(BNode* x, int idx) const;
    // Ensures x->child[idx] has at least min_keys+1 keys (so we can descend
    // and remove without underflow).  May redistribute or merge.  Returns
    // the updated child index — when a merge consumed the previous sibling
    // (left-merge), the target is now at idx-1.
    int         ensure_child_has_min(BNode* x, int idx);
    void        borrow_from_left(BNode* x, int idx);
    void        borrow_from_right(BNode* x, int idx);
    void        merge_children(BNode* x, int idx);

    // traversal / cleanup
    void        free_subtree(BNode* x);
    void        count_nodes(const BNode* x, size_t& internal, size_t& leaf,
                            size_t& keys, size_t& height, size_t depth) const;
    void        range_walk(const BNode* x, Key lo, Key hi,
                           const std::function<void(Key, RID)>& cb) const;
};
