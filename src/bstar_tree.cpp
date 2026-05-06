#include "bstar_tree.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
inline int ceil_div2(int x) { return (x + 1) / 2; }
}

BStarTree::BStarTree(int d) : d_(d) {
    if (d < 3) throw std::invalid_argument("BStarTree order d must be >= 3");
    int t      = ceil_div2(d);
    max_keys_  = 2 * t - 1;
    min_keys_  = t - 1;
    root_      = new BNode(/*leaf=*/true);
}

BStarTree::~BStarTree() { free_subtree(root_); }

void BStarTree::free_subtree(BNode* x) {
    if (!x) return;
    if (!x->leaf) {
        for (int i = 0; i <= x->n; ++i) free_subtree(x->child[i]);
    }
    delete x;
}

int BStarTree::lower_bound_idx(const std::vector<Key>& keys, int n, Key k) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) >> 1; if (keys[m] < k) lo = m + 1; else hi = m; }
    return lo;
}

// ---------------- search / range ----------------

std::optional<RID> BStarTree::search(Key k) const {
    const BNode* x = root_;
    while (x) {
        int i = lower_bound_idx(x->keys, x->n, k);
        if (i < x->n && x->keys[i] == k) return x->rids[i];
        if (x->leaf) return std::nullopt;
        x = x->child[i];
    }
    return std::nullopt;
}

void BStarTree::range_walk(const BNode* x, Key lo, Key hi,
                           const std::function<void(Key, RID)>& cb) const {
    if (!x) return;
    int i = lower_bound_idx(x->keys, x->n, lo);
    if (x->leaf) {
        for (; i < x->n && x->keys[i] <= hi; ++i) cb(x->keys[i], x->rids[i]);
        return;
    }
    for (; i < x->n; ++i) {
        range_walk(x->child[i], lo, hi, cb);
        if (x->keys[i] > hi) return;
        if (x->keys[i] >= lo) cb(x->keys[i], x->rids[i]);
    }
    range_walk(x->child[x->n], lo, hi, cb);
}

void BStarTree::range(Key lo, Key hi,
                      const std::function<void(Key, RID)>& cb) const {
    if (lo > hi) return;
    range_walk(root_, lo, hi, cb);
}

// ---------------- insert ----------------

bool BStarTree::insert_into(BNode* x, Key k, RID r) {
    int i = lower_bound_idx(x->keys, x->n, k);
    if (i < x->n && x->keys[i] == k) return false;     // duplicate

    if (x->leaf) {
        x->keys.insert(x->keys.begin() + i, k);
        x->rids.insert(x->rids.begin() + i, r);
        x->n += 1;
        return x->n > max_keys_;
    }

    bool child_overflow = insert_into(x->child[i], k, r);
    if (!child_overflow) return false;

    resolve_overflow(x, i);
    return x->n > max_keys_;
}

bool BStarTree::try_redistribute_to_left(BNode* parent, int idx) {
    if (idx == 0) return false;
    BNode* x    = parent->child[idx];
    BNode* left = parent->child[idx - 1];
    if (left->n >= max_keys_) return false;

    // Move parent->keys[idx-1] into left's tail, take x->keys[0] up to parent.
    // For internal nodes also transfer x->child[0] to become left's new last
    // child.
    Key sep = parent->keys[idx - 1];
    RID sep_rid = parent->rids[idx - 1];

    left->keys.push_back(sep);
    left->rids.push_back(sep_rid);
    if (!x->leaf) {
        BNode* moved = x->child.front();
        if (moved) moved->parent = left;
        left->child.push_back(moved);
        x->child.erase(x->child.begin());
    }
    left->n += 1;

    parent->keys[idx - 1] = x->keys.front();
    parent->rids[idx - 1] = x->rids.front();
    x->keys.erase(x->keys.begin());
    x->rids.erase(x->rids.begin());
    x->n -= 1;

    ++redistributes_;
    return true;
}

bool BStarTree::try_redistribute_to_right(BNode* parent, int idx) {
    if (idx == parent->n) return false;
    BNode* x     = parent->child[idx];
    BNode* right = parent->child[idx + 1];
    if (right->n >= max_keys_) return false;

    Key sep     = parent->keys[idx];
    RID sep_rid = parent->rids[idx];

    right->keys.insert(right->keys.begin(), sep);
    right->rids.insert(right->rids.begin(), sep_rid);
    if (!x->leaf) {
        BNode* moved = x->child.back();
        if (moved) moved->parent = right;
        right->child.insert(right->child.begin(), moved);
        x->child.pop_back();
    }
    right->n += 1;

    parent->keys[idx] = x->keys.back();
    parent->rids[idx] = x->rids.back();
    x->keys.pop_back();
    x->rids.pop_back();
    x->n -= 1;

    ++redistributes_;
    return true;
}

// Ordinary CLRS-style split: child y = parent->child[idx] is overflowing
// (n == max_keys_+1).  Split into y (left half) and z (right half), median
// pushed up.
void BStarTree::one_to_two_split(BNode* parent, int idx) {
    BNode* y = parent->child[idx];
    int total = y->n;        // == max_keys_ + 1
    int L     = total / 2;
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

// 2-to-3 split. parent->child[idx] (= x) is overflowing; sib_idx is either
// idx-1 (left sibling) or idx+1 (right sibling) and is full
// (n == max_keys_).  We pool x + separator + sibling and redistribute
// across THREE children, promoting two new separators.
void BStarTree::two_to_three_split(BNode* parent, int idx, int sib_idx) {
    bool sib_is_left = (sib_idx < idx);
    int  L_idx = sib_is_left ? sib_idx : idx;
    int  R_idx = sib_is_left ? idx     : sib_idx;
    BNode* L = parent->child[L_idx];
    BNode* R = parent->child[R_idx];
    Key    sep      = parent->keys[L_idx];
    RID    sep_rid  = parent->rids[L_idx];

    // Pool keys/rids: L.keys, sep, R.keys
    std::vector<Key> ks; ks.reserve(L->n + 1 + R->n);
    std::vector<RID> rs; rs.reserve(L->n + 1 + R->n);
    ks.insert(ks.end(), L->keys.begin(), L->keys.end());
    rs.insert(rs.end(), L->rids.begin(), L->rids.end());
    ks.push_back(sep); rs.push_back(sep_rid);
    ks.insert(ks.end(), R->keys.begin(), R->keys.end());
    rs.insert(rs.end(), R->rids.begin(), R->rids.end());

    // Pool children if internal
    std::vector<BNode*> cs;
    if (!L->leaf) {
        cs.reserve(L->child.size() + R->child.size());
        cs.insert(cs.end(), L->child.begin(), L->child.end());
        cs.insert(cs.end(), R->child.begin(), R->child.end());
    }

    int total = static_cast<int>(ks.size());
    // Distribute ~evenly into three children: sizes a, b, c with two
    // separators between them, satisfying a + b + c + 2 == total.
    // Use as-balanced-as-possible split with min size = (total - 2) / 3.
    int rem = total - 2;            // payload that ends in nodes
    int a   = rem / 3;
    int b   = (rem - a) / 2;
    int c   = rem - a - b;
    // Each must be >= min_keys_ AND <= max_keys_; with rem = 2*max in a
    // typical 2-to-3 case (both source nodes were full), rem = 2*max = 2*(2t-1),
    // a = (4t-2)/3.  For t >= 2 this satisfies (t-1) <= a, b, c <= (2t-1).

    // Sep1 is at index a (in pooled ks); Sep2 at a + 1 + b
    Key sep1 = ks[a];
    RID sep1r = rs[a];
    Key sep2 = ks[a + 1 + b];
    RID sep2r = rs[a + 1 + b];

    // Build node L (already exists; reuse) with first `a` entries
    L->keys.assign(ks.begin(),         ks.begin() + a);
    L->rids.assign(rs.begin(),         rs.begin() + a);
    L->n = a;

    // Build middle node M (new)
    BNode* M = new BNode(L->leaf);
    M->parent = parent;
    M->keys.assign(ks.begin() + a + 1, ks.begin() + a + 1 + b);
    M->rids.assign(rs.begin() + a + 1, rs.begin() + a + 1 + b);
    M->n = b;

    // R reused with last c entries
    R->keys.assign(ks.begin() + a + 1 + b + 1, ks.end());
    R->rids.assign(rs.begin() + a + 1 + b + 1, rs.end());
    R->n = c;

    // distribute children
    if (!L->leaf) {
        // L gets first a+1 children, M gets next b+1, R gets remaining c+1
        std::vector<BNode*> Lc(cs.begin(),                 cs.begin() + a + 1);
        std::vector<BNode*> Mc(cs.begin() + a + 1,         cs.begin() + a + 1 + b + 1);
        std::vector<BNode*> Rc(cs.begin() + a + 1 + b + 1, cs.end());
        L->child = std::move(Lc);
        M->child = std::move(Mc);
        R->child = std::move(Rc);
        for (BNode* c0 : L->child) if (c0) c0->parent = L;
        for (BNode* c0 : M->child) if (c0) c0->parent = M;
        for (BNode* c0 : R->child) if (c0) c0->parent = R;
    }

    // Replace parent's separator at L_idx with sep1, insert sep2 and middle
    // child.
    parent->keys[L_idx]   = sep1;
    parent->rids[L_idx]   = sep1r;
    parent->keys.insert(parent->keys.begin() + L_idx + 1, sep2);
    parent->rids.insert(parent->rids.begin() + L_idx + 1, sep2r);
    parent->child.insert(parent->child.begin() + L_idx + 1, M);
    parent->n += 1;

    ++two_to_three_;
}

void BStarTree::resolve_overflow(BNode* parent, int idx) {
    BNode* x = parent->child[idx];
    if (x->n <= max_keys_) return;

    // 1) try redistribute to a non-full sibling
    if (try_redistribute_to_left(parent, idx))  return;
    if (try_redistribute_to_right(parent, idx)) return;

    // 2) both siblings full (or x is at the boundary): 2-to-3 split if a
    //    sibling exists, else ordinary 1-to-2 split (root-overflow is
    //    handled at the caller via insert()).
    if (idx + 1 <= parent->n) {
        two_to_three_split(parent, idx, idx + 1);
        return;
    }
    if (idx > 0) {
        two_to_three_split(parent, idx, idx - 1);
        return;
    }
    one_to_two_split(parent, idx);
}

void BStarTree::insert(Key k, RID r) {
    bool overflow = insert_into(root_, k, r);
    if (!overflow) return;

    // Root overflow: 1-to-2 split (no siblings to redistribute with).
    BNode* s = new BNode(/*leaf=*/false);
    s->child.push_back(root_);
    s->n = 0;
    root_->parent = s;
    BNode* old = root_;
    root_ = s;
    one_to_two_split(s, 0);
    (void)old;
}

// ---------------- delete (same policy as BTree) ----------------

Key BStarTree::get_pred_key(BNode* x, int idx) const {
    BNode* c = x->child[idx];
    while (!c->leaf) c = c->child[c->n];
    return c->keys[c->n - 1];
}
RID BStarTree::get_pred_rid(BNode* x, int idx) const {
    BNode* c = x->child[idx];
    while (!c->leaf) c = c->child[c->n];
    return c->rids[c->n - 1];
}
Key BStarTree::get_succ_key(BNode* x, int idx) const {
    BNode* c = x->child[idx + 1];
    while (!c->leaf) c = c->child[0];
    return c->keys[0];
}
RID BStarTree::get_succ_rid(BNode* x, int idx) const {
    BNode* c = x->child[idx + 1];
    while (!c->leaf) c = c->child[0];
    return c->rids[0];
}

void BStarTree::remove_from_leaf(BNode* x, int idx) {
    x->keys.erase(x->keys.begin() + idx);
    x->rids.erase(x->rids.begin() + idx);
    x->n -= 1;
}

void BStarTree::remove_from_internal(BNode* x, int idx) {
    Key k = x->keys[idx];
    BNode* left  = x->child[idx];
    BNode* right = x->child[idx + 1];

    if (left->n > min_keys_) {
        Key pk = get_pred_key(x, idx);
        RID pr = get_pred_rid(x, idx);
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

void BStarTree::borrow_from_left(BNode* x, int idx) {
    BNode* child = x->child[idx];
    BNode* left  = x->child[idx - 1];
    child->keys.insert(child->keys.begin(), x->keys[idx - 1]);
    child->rids.insert(child->rids.begin(), x->rids[idx - 1]);
    if (!child->leaf) {
        BNode* moved = left->child.back();
        if (moved) moved->parent = child;
        child->child.insert(child->child.begin(), moved);
        left->child.pop_back();
    }
    child->n += 1;
    x->keys[idx - 1] = left->keys.back();
    x->rids[idx - 1] = left->rids.back();
    left->keys.pop_back();
    left->rids.pop_back();
    left->n -= 1;
    ++del_redists_;
}

void BStarTree::borrow_from_right(BNode* x, int idx) {
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
    ++del_redists_;
}

void BStarTree::merge_children(BNode* x, int idx) {
    BNode* left  = x->child[idx];
    BNode* right = x->child[idx + 1];
    left->keys.push_back(x->keys[idx]);
    left->rids.push_back(x->rids[idx]);
    left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
    left->rids.insert(left->rids.end(), right->rids.begin(), right->rids.end());
    if (!left->leaf) {
        for (BNode* c : right->child) if (c) c->parent = left;
        left->child.insert(left->child.end(), right->child.begin(), right->child.end());
    }
    left->n += 1 + right->n;
    x->keys.erase(x->keys.begin() + idx);
    x->rids.erase(x->rids.begin() + idx);
    x->child.erase(x->child.begin() + idx + 1);
    x->n -= 1;
    delete right;
    ++merges_;
}

int BStarTree::ensure_child_has_min(BNode* x, int idx) {
    BNode* child = x->child[idx];
    if (child->n > min_keys_) return idx;
    BNode* left  = idx > 0    ? x->child[idx - 1] : nullptr;
    BNode* right = idx < x->n ? x->child[idx + 1] : nullptr;
    if (left  && left->n  > min_keys_) { borrow_from_left(x, idx);  return idx; }
    if (right && right->n > min_keys_) { borrow_from_right(x, idx); return idx; }
    if (left)  { merge_children(x, idx - 1); return idx - 1; }
    merge_children(x, idx);
    return idx;
}

void BStarTree::remove_from(BNode* x, Key k) {
    int i = lower_bound_idx(x->keys, x->n, k);
    if (i < x->n && x->keys[i] == k) {
        if (x->leaf) remove_from_leaf(x, i);
        else         remove_from_internal(x, i);
        return;
    }
    if (x->leaf) return;
    int new_idx = ensure_child_has_min(x, i);
    remove_from(x->child[new_idx], k);
}

bool BStarTree::remove(Key k) {
    if (!root_) return false;
    if (!search(k).has_value()) return false;
    remove_from(root_, k);
    if (root_->n == 0 && !root_->leaf) {
        BNode* old = root_;
        root_ = old->child[0];
        if (root_) root_->parent = nullptr;
        delete old;
    }
    return true;
}

// ---------------- stats ----------------

void BStarTree::count_nodes(const BNode* x, size_t& internal, size_t& leaf,
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

TreeStats BStarTree::stats() const {
    TreeStats s;
    size_t height = 0;
    count_nodes(root_, s.internal_nodes, s.leaf_nodes, s.total_keys, height, 1);
    s.height = height;
    // For B*-tree we expose:
    //   splits  = 1-to-2 splits  +  2-to-3 splits  (a 2-to-3 grows total
    //                                                node count by 1, just
    //                                                like a 1-to-2 split)
    //   redistributes = insertion redistributes  +  deletion redistributes
    //   merges  = deletion merges
    s.splits        = splits_ + two_to_three_;
    s.redistributes = redistributes_ + del_redists_;
    s.merges        = merges_;
    return s;
}
