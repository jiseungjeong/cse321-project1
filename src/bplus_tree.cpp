#include "bplus_tree.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
inline int ceil_div2(int x) { return (x + 1) / 2; }
}

BPlusTree::BPlusTree(int d) : d_(d) {
    if (d < 3) throw std::invalid_argument("BPlusTree order d must be >= 3");
    int t      = ceil_div2(d);
    max_keys_  = 2 * t - 1;
    min_keys_  = t - 1;

    auto* leaf  = new BPLeaf();
    root_         = leaf;
    root_is_leaf_ = true;
    first_leaf_   = leaf;
}

BPlusTree::~BPlusTree() {
    free_subtree(root_, root_is_leaf_);
}

void BPlusTree::free_subtree(void* node, bool is_leaf) {
    if (!node) return;
    if (is_leaf) {
        delete static_cast<BPLeaf*>(node);
        return;
    }
    auto* in = static_cast<BPInternal*>(node);
    for (int i = 0; i <= in->n; ++i) free_subtree(in->child[i], in->child_is_leaf);
    delete in;
}

int BPlusTree::lower_bound_idx(const std::vector<Key>& keys, int n, Key k) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) >> 1; if (keys[m] < k) lo = m + 1; else hi = m; }
    return lo;
}
int BPlusTree::upper_bound_idx(const std::vector<Key>& keys, int n, Key k) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) >> 1; if (keys[m] <= k) lo = m + 1; else hi = m; }
    return lo;
}

// ---------------- search ----------------

BPLeaf* BPlusTree::find_leaf(Key k) const {
    if (root_is_leaf_) return static_cast<BPLeaf*>(root_);
    auto* in = static_cast<BPInternal*>(root_);
    while (true) {
        int i = upper_bound_idx(in->keys, in->n, k);
        // upper_bound: first key > k → child[i] is the subtree containing k
        if (in->child_is_leaf) return static_cast<BPLeaf*>(in->child[i]);
        in = static_cast<BPInternal*>(in->child[i]);
    }
}

std::optional<RID> BPlusTree::search(Key k) const {
    BPLeaf* leaf = find_leaf(k);
    int i = lower_bound_idx(leaf->keys, leaf->n, k);
    if (i < leaf->n && leaf->keys[i] == k) return leaf->rids[i];
    return std::nullopt;
}

// ---------------- insert ----------------

void BPlusTree::split_leaf(BPLeaf* leaf, BPLeaf*& out_right, Key& out_sep) {
    int total = leaf->n;     // == max_keys_+1
    int L     = total / 2;

    auto* z = new BPLeaf();
    z->n = total - L;
    z->keys.assign(leaf->keys.begin() + L, leaf->keys.end());
    z->rids.assign(leaf->rids.begin() + L, leaf->rids.end());
    z->next   = leaf->next;
    z->parent = leaf->parent;

    leaf->keys.resize(L);
    leaf->rids.resize(L);
    leaf->n = L;
    leaf->next = z;

    out_right = z;
    out_sep   = z->keys[0];
    ++splits_;
}

void BPlusTree::split_internal(BPInternal* node, BPInternal*& out_right, Key& out_sep) {
    int total = node->n;     // == max_keys_+1
    int L     = total / 2;
    Key sep   = node->keys[L];

    auto* z = new BPInternal();
    z->child_is_leaf = node->child_is_leaf;
    z->n = total - L - 1;
    z->keys.assign(node->keys.begin() + L + 1, node->keys.end());
    z->child.assign(node->child.begin() + L + 1, node->child.end());
    z->parent = node->parent;
    if (z->child_is_leaf) {
        for (void* c : z->child) static_cast<BPLeaf*>(c)->parent = z;
    } else {
        for (void* c : z->child) static_cast<BPInternal*>(c)->parent = z;
    }

    node->keys.resize(L);
    node->child.resize(L + 1);
    node->n = L;

    out_right = z;
    out_sep   = sep;
    ++splits_;
}

BPlusTree::InsertResult BPlusTree::insert_into_leaf(BPLeaf* leaf, Key k, RID r) {
    int i = lower_bound_idx(leaf->keys, leaf->n, k);
    if (i < leaf->n && leaf->keys[i] == k) return {};   // duplicate: silently reject
    leaf->keys.insert(leaf->keys.begin() + i, k);
    leaf->rids.insert(leaf->rids.begin() + i, r);
    leaf->n += 1;
    if (leaf->n <= max_keys_) return {};
    InsertResult out;
    out.split = true;
    BPLeaf* z = nullptr;
    split_leaf(leaf, z, out.sep);
    out.right = z;
    return out;
}

BPlusTree::InsertResult BPlusTree::insert_into_internal(BPInternal* node, Key k, RID r) {
    int i = upper_bound_idx(node->keys, node->n, k);
    InsertResult sub;
    if (node->child_is_leaf) {
        sub = insert_into_leaf(static_cast<BPLeaf*>(node->child[i]), k, r);
    } else {
        sub = insert_into_internal(static_cast<BPInternal*>(node->child[i]), k, r);
    }
    if (!sub.split) return {};

    // attach the new right sibling to *this* node
    node->keys.insert(node->keys.begin() + i, sub.sep);
    node->child.insert(node->child.begin() + i + 1, sub.right);
    if (node->child_is_leaf) {
        static_cast<BPLeaf*>(sub.right)->parent = node;
    } else {
        static_cast<BPInternal*>(sub.right)->parent = node;
    }
    node->n += 1;
    if (node->n <= max_keys_) return {};

    InsertResult out;
    out.split = true;
    BPInternal* z = nullptr;
    split_internal(node, z, out.sep);
    out.right = z;
    return out;
}

void BPlusTree::insert(Key k, RID r) {
    InsertResult sub;
    if (root_is_leaf_) sub = insert_into_leaf(static_cast<BPLeaf*>(root_), k, r);
    else               sub = insert_into_internal(static_cast<BPInternal*>(root_), k, r);
    if (!sub.split) return;

    auto* new_root = new BPInternal();
    new_root->child_is_leaf = root_is_leaf_;
    new_root->n = 1;
    new_root->keys.push_back(sub.sep);
    new_root->child.push_back(root_);
    new_root->child.push_back(sub.right);
    if (root_is_leaf_) {
        static_cast<BPLeaf*>(root_)->parent = new_root;
        static_cast<BPLeaf*>(sub.right)->parent = new_root;
    } else {
        static_cast<BPInternal*>(root_)->parent = new_root;
        static_cast<BPInternal*>(sub.right)->parent = new_root;
    }
    root_         = new_root;
    root_is_leaf_ = false;
}

// ---------------- range query (linked-leaf scan) ----------------

void BPlusTree::range(Key lo, Key hi,
                      const std::function<void(Key, RID)>& cb) const {
    if (lo > hi) return;
    BPLeaf* leaf = find_leaf(lo);
    int i = lower_bound_idx(leaf->keys, leaf->n, lo);
    while (leaf) {
        for (; i < leaf->n; ++i) {
            if (leaf->keys[i] > hi) return;
            cb(leaf->keys[i], leaf->rids[i]);
        }
        leaf = leaf->next;
        i = 0;
    }
}

// ---------------- delete ----------------

bool BPlusTree::remove_from_leaf(BPLeaf* leaf, Key k) {
    int i = lower_bound_idx(leaf->keys, leaf->n, k);
    if (i >= leaf->n || leaf->keys[i] != k) return false;
    leaf->keys.erase(leaf->keys.begin() + i);
    leaf->rids.erase(leaf->rids.begin() + i);
    leaf->n -= 1;
    return true;
}

void BPlusTree::borrow_left_leaf(BPInternal* parent, int idx) {
    auto* child = static_cast<BPLeaf*>(parent->child[idx]);
    auto* left  = static_cast<BPLeaf*>(parent->child[idx - 1]);
    Key   moved_key = left->keys.back();
    RID   moved_rid = left->rids.back();
    left->keys.pop_back();
    left->rids.pop_back();
    left->n -= 1;
    child->keys.insert(child->keys.begin(), moved_key);
    child->rids.insert(child->rids.begin(), moved_rid);
    child->n += 1;
    parent->keys[idx - 1] = child->keys[0];
    ++redistributes_;
}

void BPlusTree::borrow_right_leaf(BPInternal* parent, int idx) {
    auto* child = static_cast<BPLeaf*>(parent->child[idx]);
    auto* right = static_cast<BPLeaf*>(parent->child[idx + 1]);
    Key   moved_key = right->keys.front();
    RID   moved_rid = right->rids.front();
    right->keys.erase(right->keys.begin());
    right->rids.erase(right->rids.begin());
    right->n -= 1;
    child->keys.push_back(moved_key);
    child->rids.push_back(moved_rid);
    child->n += 1;
    parent->keys[idx] = right->keys[0];
    ++redistributes_;
}

void BPlusTree::merge_leaf(BPInternal* parent, int idx) {
    auto* left  = static_cast<BPLeaf*>(parent->child[idx]);
    auto* right = static_cast<BPLeaf*>(parent->child[idx + 1]);
    left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
    left->rids.insert(left->rids.end(), right->rids.begin(), right->rids.end());
    left->n += right->n;
    left->next = right->next;

    parent->keys.erase(parent->keys.begin() + idx);
    parent->child.erase(parent->child.begin() + idx + 1);
    parent->n -= 1;

    delete right;
    ++merges_;
}

void BPlusTree::borrow_left_internal(BPInternal* parent, int idx) {
    auto* child = static_cast<BPInternal*>(parent->child[idx]);
    auto* left  = static_cast<BPInternal*>(parent->child[idx - 1]);

    // pull parent separator down as new first key of child; child gains
    // left's last child as its new first child
    child->keys.insert(child->keys.begin(), parent->keys[idx - 1]);
    void* moved_child = left->child.back();
    if (child->child_is_leaf) static_cast<BPLeaf*>(moved_child)->parent = child;
    else                      static_cast<BPInternal*>(moved_child)->parent = child;
    child->child.insert(child->child.begin(), moved_child);
    child->n += 1;

    parent->keys[idx - 1] = left->keys.back();
    left->keys.pop_back();
    left->child.pop_back();
    left->n -= 1;
    ++redistributes_;
}

void BPlusTree::borrow_right_internal(BPInternal* parent, int idx) {
    auto* child = static_cast<BPInternal*>(parent->child[idx]);
    auto* right = static_cast<BPInternal*>(parent->child[idx + 1]);

    child->keys.push_back(parent->keys[idx]);
    void* moved_child = right->child.front();
    if (child->child_is_leaf) static_cast<BPLeaf*>(moved_child)->parent = child;
    else                      static_cast<BPInternal*>(moved_child)->parent = child;
    child->child.push_back(moved_child);
    child->n += 1;

    parent->keys[idx] = right->keys.front();
    right->keys.erase(right->keys.begin());
    right->child.erase(right->child.begin());
    right->n -= 1;
    ++redistributes_;
}

void BPlusTree::merge_internal(BPInternal* parent, int idx) {
    auto* left  = static_cast<BPInternal*>(parent->child[idx]);
    auto* right = static_cast<BPInternal*>(parent->child[idx + 1]);

    // pull separator + right's content into left
    left->keys.push_back(parent->keys[idx]);
    left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
    if (left->child_is_leaf) {
        for (void* c : right->child) static_cast<BPLeaf*>(c)->parent = left;
    } else {
        for (void* c : right->child) static_cast<BPInternal*>(c)->parent = left;
    }
    left->child.insert(left->child.end(), right->child.begin(), right->child.end());
    left->n = static_cast<int>(left->keys.size());

    parent->keys.erase(parent->keys.begin() + idx);
    parent->child.erase(parent->child.begin() + idx + 1);
    parent->n -= 1;

    delete right;
    ++merges_;
}

void BPlusTree::fix_underflow_leaf_child(BPInternal* parent, int idx) {
    auto* child = static_cast<BPLeaf*>(parent->child[idx]);
    if (child->n >= min_keys_) return;
    BPLeaf* left  = idx > 0           ? static_cast<BPLeaf*>(parent->child[idx - 1]) : nullptr;
    BPLeaf* right = idx < parent->n   ? static_cast<BPLeaf*>(parent->child[idx + 1]) : nullptr;
    if (left  && left->n  > min_keys_) { borrow_left_leaf(parent, idx);  return; }
    if (right && right->n > min_keys_) { borrow_right_leaf(parent, idx); return; }
    if (left)  merge_leaf(parent, idx - 1);
    else       merge_leaf(parent, idx);
}

void BPlusTree::fix_underflow_internal_child(BPInternal* parent, int idx) {
    auto* child = static_cast<BPInternal*>(parent->child[idx]);
    if (child->n >= min_keys_) return;
    BPInternal* left  = idx > 0           ? static_cast<BPInternal*>(parent->child[idx - 1]) : nullptr;
    BPInternal* right = idx < parent->n   ? static_cast<BPInternal*>(parent->child[idx + 1]) : nullptr;
    if (left  && left->n  > min_keys_) { borrow_left_internal(parent, idx);  return; }
    if (right && right->n > min_keys_) { borrow_right_internal(parent, idx); return; }
    if (left)  merge_internal(parent, idx - 1);
    else       merge_internal(parent, idx);
}

bool BPlusTree::remove_descend(BPInternal* node, Key k) {
    int i = upper_bound_idx(node->keys, node->n, k);
    bool removed;
    if (node->child_is_leaf) {
        auto* leaf = static_cast<BPLeaf*>(node->child[i]);
        removed = remove_from_leaf(leaf, k);
        if (!removed) return false;
        // After removal, fix underflow on this leaf child; also separator
        // keys[i-1] still points correctly because leaf->keys[0] only changes
        // if we removed the very first key.  Refresh that separator here
        // (cheap and avoids stale separators).
        if (i > 0 && leaf->n > 0) node->keys[i - 1] = leaf->keys[0];
        fix_underflow_leaf_child(node, i);
    } else {
        auto* sub = static_cast<BPInternal*>(node->child[i]);
        removed = remove_descend(sub, k);
        if (!removed) return false;
        fix_underflow_internal_child(node, i);
    }
    return true;
}

bool BPlusTree::remove(Key k) {
    if (!root_) return false;
    if (root_is_leaf_) {
        auto* leaf = static_cast<BPLeaf*>(root_);
        bool ok = remove_from_leaf(leaf, k);
        // root leaf may legitimately be empty; do not fix underflow
        return ok;
    }
    auto* in = static_cast<BPInternal*>(root_);
    bool ok = remove_descend(in, k);
    if (!ok) return false;

    // root collapse
    if (in->n == 0) {
        if (in->child_is_leaf) {
            auto* only = static_cast<BPLeaf*>(in->child[0]);
            only->parent = nullptr;
            root_ = only;
            root_is_leaf_ = true;
        } else {
            auto* only = static_cast<BPInternal*>(in->child[0]);
            only->parent = nullptr;
            root_ = only;
        }
        delete in;
    }
    return true;
}

// ---------------- stats ----------------

void BPlusTree::count_nodes(const void* node, bool is_leaf, size_t depth,
                            size_t& internal, size_t& leaf, size_t& keys,
                            size_t& height) const {
    if (!node) return;
    if (is_leaf) {
        const auto* l = static_cast<const BPLeaf*>(node);
        ++leaf;
        keys += static_cast<size_t>(l->n);
        if (depth > height) height = depth;
        return;
    }
    const auto* in = static_cast<const BPInternal*>(node);
    ++internal;
    // NB: internal separator keys are NOT counted here. total_keys for a
    // B+ tree means "number of distinct (key, RID) pairs in leaves", which
    // equals the number of data records and matches BTree::stats().
    for (int i = 0; i <= in->n; ++i) {
        count_nodes(in->child[i], in->child_is_leaf, depth + 1,
                    internal, leaf, keys, height);
    }
}

TreeStats BPlusTree::stats() const {
    TreeStats s;
    size_t height = 0;
    count_nodes(root_, root_is_leaf_, 1, s.internal_nodes, s.leaf_nodes,
                s.total_keys, height);
    s.height        = height;
    s.splits        = splits_;
    s.redistributes = redistributes_;
    s.merges        = merges_;
    return s;
}
