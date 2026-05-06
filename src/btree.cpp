#include "btree.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
inline int ceil_div2(int x) { return (x + 1) / 2; }
}

BTree::BTree(int d) : d_(d) {
    if (d < 3) throw std::invalid_argument("BTree order d must be >= 3");
    // CLRS-style minimum-degree mapping. We accept user-facing order `d`
    // (max children) and derive t = ceil(d/2) so that:
    //     max keys per node       = 2t - 1
    //     min keys per non-root   = t - 1
    //     max children per node   = 2t
    // This guarantees merge result (= 2(t-1)+1 = 2t-1) never exceeds max,
    // which keeps invariants consistent for *every* d >= 3 including odd
    // values (d=3, 5).  See plan.md §0 / research.md §3 for definition.
    int t      = ceil_div2(d);          // ceil(d/2)
    max_keys_  = 2 * t - 1;
    min_keys_  = t - 1;
    root_      = new BNode(/*leaf=*/true);
}

BTree::~BTree() {
    free_subtree(root_);
}

void BTree::free_subtree(BNode* x) {
    if (!x) return;
    if (!x->leaf) {
        for (int i = 0; i <= x->n; ++i) free_subtree(x->child[i]);
    }
    delete x;
}

int BTree::lower_bound_idx(const std::vector<Key>& keys, int n, Key k) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (keys[m] < k) lo = m + 1; else hi = m;
    }
    return lo;
}

// ---------------- search ----------------

std::optional<RID> BTree::search(Key k) const {
    const BNode* x = root_;
    while (x) {
        int i = lower_bound_idx(x->keys, x->n, k);
        if (i < x->n && x->keys[i] == k) return x->rids[i];
        if (x->leaf) return std::nullopt;
        x = x->child[i];
    }
    return std::nullopt;
}

// ---------------- insert (bottom-up split) ----------------
//
// 1. Descend to a leaf and insert (allowing temporary overflow up to
//    max_keys_ + 1 = d keys / d+1 children).
// 2. If the leaf overflows, split it and propagate one (key, rid) +
//    right-child pointer up to the parent. Repeat until root.
// 3. If root overflows, create a new root.
//
// Splitting a node with d keys (= max_keys_+1) into:
//     left  = first  L keys, where L = d / 2
//     right = last   R keys, where R = d - 1 - L = max_keys_ - L
//     median = keys[L] (promoted up)
//
// For d=3 (max_keys_=2): overflow has 3 keys; L=1, median=keys[1], right=1
// For d=5 (max_keys_=4): overflow has 5 keys; L=2, median=keys[2], right=2
// For d=10(max_keys_=9): overflow has 10 keys;L=5, median=keys[5], right=4
//
// All resulting nodes satisfy >= ceil(d/2)-1 keys (== min_keys_).

void BTree::split_child(BNode* parent, int idx) {
    BNode* y = parent->child[idx];
    // y is overflowing: y->n == max_keys_ + 1 keys (== d keys)
    int total = y->n;
    int L     = total / 2;            // left key count
    Key  midK = y->keys[L];
    RID  midR = y->rids[L];

    BNode* z = new BNode(y->leaf);
    z->parent = parent;
    z->n = total - L - 1;
    z->keys.assign(y->keys.begin() + L + 1, y->keys.end());
    z->rids.assign(y->rids.begin() + L + 1, y->rids.end());
    if (!y->leaf) {
        z->child.assign(y->child.begin() + L + 1, y->child.end());
        for (BNode* c : z->child) if (c) c->parent = z;
        y->child.resize(L + 1);
    }
    y->keys.resize(L);
    y->rids.resize(L);
    y->n = L;

    parent->keys.insert(parent->keys.begin() + idx, midK);
    parent->rids.insert(parent->rids.begin() + idx, midR);
    parent->child.insert(parent->child.begin() + idx + 1, z);
    parent->n += 1;

    ++splits_;
}

// Recursively descend, then split on the way back up while x has overflowed.
void BTree::insert_nonfull(BNode* x, Key k, RID r) {
    int i = lower_bound_idx(x->keys, x->n, k);
    if (i < x->n && x->keys[i] == k) return;   // duplicate — silently reject

    if (x->leaf) {
        x->keys.insert(x->keys.begin() + i, k);
        x->rids.insert(x->rids.begin() + i, r);
        x->n += 1;
        return;
    }
    insert_nonfull(x->child[i], k, r);
    if (x->child[i]->n > max_keys_) {
        split_child(x, i);
    }
}

void BTree::insert(Key k, RID r) {
    insert_nonfull(root_, k, r);
    if (root_->n > max_keys_) {
        BNode* s = new BNode(/*leaf=*/false);
        s->child.push_back(root_);
        s->n = 0;
        root_->parent = s;
        BNode* old_root = root_;
        root_ = s;
        // split the old root as child 0 of the new root
        split_child(s, 0);
        (void)old_root;
    }
}

// ---------------- range (inorder over [lo, hi]) ----------------

void BTree::range_walk(const BNode* x, Key lo, Key hi,
                       const std::function<void(Key, RID)>& cb) const {
    if (!x) return;
    int i = lower_bound_idx(x->keys, x->n, lo);
    if (x->leaf) {
        for (; i < x->n && x->keys[i] <= hi; ++i) cb(x->keys[i], x->rids[i]);
        return;
    }
    // descend into child[i], then key[i], child[i+1], ... until key > hi
    for (; i < x->n; ++i) {
        range_walk(x->child[i], lo, hi, cb);
        if (x->keys[i] > hi) return;
        if (x->keys[i] >= lo) cb(x->keys[i], x->rids[i]);
    }
    range_walk(x->child[x->n], lo, hi, cb);
}

void BTree::range(Key lo, Key hi,
                  const std::function<void(Key, RID)>& cb) const {
    if (lo > hi) return;
    range_walk(root_, lo, hi, cb);
}

// ---------------- delete ----------------

Key BTree::get_pred_key(BNode* x, int idx) const {
    BNode* c = x->child[idx];
    while (!c->leaf) c = c->child[c->n];
    return c->keys[c->n - 1];
}
RID BTree::get_pred_rid(BNode* x, int idx) const {
    BNode* c = x->child[idx];
    while (!c->leaf) c = c->child[c->n];
    return c->rids[c->n - 1];
}
Key BTree::get_succ_key(BNode* x, int idx) const {
    BNode* c = x->child[idx + 1];
    while (!c->leaf) c = c->child[0];
    return c->keys[0];
}
RID BTree::get_succ_rid(BNode* x, int idx) const {
    BNode* c = x->child[idx + 1];
    while (!c->leaf) c = c->child[0];
    return c->rids[0];
}

void BTree::remove_from_leaf(BNode* x, int idx) {
    x->keys.erase(x->keys.begin() + idx);
    x->rids.erase(x->rids.begin() + idx);
    x->n -= 1;
}

void BTree::remove_from_internal(BNode* x, int idx) {
    Key k = x->keys[idx];
    BNode* left  = x->child[idx];
    BNode* right = x->child[idx + 1];

    if (left->n > min_keys_) {
        Key pk  = get_pred_key(x, idx);
        RID pr  = get_pred_rid(x, idx);
        x->keys[idx] = pk;
        x->rids[idx] = pr;
        remove_from(left, pk);
    } else if (right->n > min_keys_) {
        Key sk = get_succ_key(x, idx);
        RID sr = get_succ_rid(x, idx);
        x->keys[idx] = sk;
        x->rids[idx] = sr;
        remove_from(right, sk);
    } else {
        merge_children(x, idx);
        remove_from(left, k);
    }
}

void BTree::borrow_from_left(BNode* x, int idx) {
    BNode* child = x->child[idx];
    BNode* left  = x->child[idx - 1];

    // pull separator down into child front
    child->keys.insert(child->keys.begin(), x->keys[idx - 1]);
    child->rids.insert(child->rids.begin(), x->rids[idx - 1]);
    if (!child->leaf) {
        BNode* moved = left->child.back();
        if (moved) moved->parent = child;
        child->child.insert(child->child.begin(), moved);
        left->child.pop_back();
    }
    child->n += 1;

    // promote left's last key to parent
    x->keys[idx - 1] = left->keys.back();
    x->rids[idx - 1] = left->rids.back();
    left->keys.pop_back();
    left->rids.pop_back();
    left->n -= 1;

    ++redistributes_;
}

void BTree::borrow_from_right(BNode* x, int idx) {
    BNode* child = x->child[idx];
    BNode* right = x->child[idx + 1];

    child->keys.push_back(x->keys[idx]);
    child->rids.push_back(x->rids[idx]);
    if (!child->leaf) {
        BNode* moved = right->child.front();
        if (moved) moved->parent = child;
        child->child.push_back(moved);
        right->child.erase(right->child.begin());
    }
    child->n += 1;

    x->keys[idx] = right->keys.front();
    x->rids[idx] = right->rids.front();
    right->keys.erase(right->keys.begin());
    right->rids.erase(right->rids.begin());
    right->n -= 1;

    ++redistributes_;
}

void BTree::merge_children(BNode* x, int idx) {
    BNode* left  = x->child[idx];
    BNode* right = x->child[idx + 1];

    // append separator + right's content into left
    left->keys.push_back(x->keys[idx]);
    left->rids.push_back(x->rids[idx]);
    left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
    left->rids.insert(left->rids.end(), right->rids.begin(), right->rids.end());
    if (!left->leaf) {
        for (BNode* c : right->child) if (c) c->parent = left;
        left->child.insert(left->child.end(), right->child.begin(), right->child.end());
    }
    left->n += 1 + right->n;

    // remove separator and right ptr from x
    x->keys.erase(x->keys.begin() + idx);
    x->rids.erase(x->rids.begin() + idx);
    x->child.erase(x->child.begin() + idx + 1);
    x->n -= 1;

    delete right;
    ++merges_;
}

int BTree::ensure_child_has_min(BNode* x, int idx) {
    BNode* child = x->child[idx];
    if (child->n > min_keys_) return idx;

    BNode* left  = idx > 0       ? x->child[idx - 1] : nullptr;
    BNode* right = idx < x->n    ? x->child[idx + 1] : nullptr;

    if (left && left->n > min_keys_) {
        borrow_from_left(x, idx);
        return idx;
    }
    if (right && right->n > min_keys_) {
        borrow_from_right(x, idx);
        return idx;
    }
    if (left) {
        merge_children(x, idx - 1);  // child[idx-1] absorbs sep + child[idx]
        return idx - 1;
    }
    merge_children(x, idx);          // child[idx] absorbs sep + child[idx+1]
    return idx;
}

void BTree::remove_from(BNode* x, Key k) {
    int i = lower_bound_idx(x->keys, x->n, k);
    if (i < x->n && x->keys[i] == k) {
        if (x->leaf) {
            remove_from_leaf(x, i);
        } else {
            remove_from_internal(x, i);
        }
        return;
    }
    if (x->leaf) return;  // not found

    int new_idx = ensure_child_has_min(x, i);
    remove_from(x->child[new_idx], k);
}

bool BTree::remove(Key k) {
    if (!root_) return false;
    if (!search(k).has_value()) return false;

    remove_from(root_, k);

    if (root_->n == 0) {
        BNode* old = root_;
        if (old->leaf) {
            // tree is now empty: keep an empty leaf as root
        } else {
            root_ = old->child[0];
            if (root_) root_->parent = nullptr;
            delete old;
        }
    }
    return true;
}

// ---------------- stats ----------------

void BTree::count_nodes(const BNode* x, size_t& internal, size_t& leaf,
                        size_t& keys, size_t& height, size_t depth) const {
    if (!x) return;
    keys += static_cast<size_t>(x->n);
    if (x->leaf) {
        ++leaf;
        if (depth > height) height = depth;
        return;
    }
    ++internal;
    for (int i = 0; i <= x->n; ++i) {
        count_nodes(x->child[i], internal, leaf, keys, height, depth + 1);
    }
}

TreeStats BTree::stats() const {
    TreeStats s;
    size_t height = 0;
    count_nodes(root_, s.internal_nodes, s.leaf_nodes, s.total_keys, height, 1);
    s.height        = height;
    s.splits        = splits_;
    s.redistributes = redistributes_;
    s.merges        = merges_;
    return s;
}
