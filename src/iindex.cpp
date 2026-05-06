#include "iindex.hpp"

#include <stdexcept>
#include <string>

#include "bplus_tree.hpp"
#include "bstar_tree.hpp"
#include "btree.hpp"

std::unique_ptr<IIndex> make_index(std::string_view kind, int d) {
    if (kind == "btree") return std::make_unique<BTree>(d);
    if (kind == "bplus") return std::make_unique<BPlusTree>(d);
    if (kind == "bstar") return std::make_unique<BStarTree>(d);
    throw std::runtime_error("make_index: unknown tree '" + std::string(kind) + "'");
}
