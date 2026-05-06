#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "record.hpp"

struct TreeStats {
    size_t height         = 0;
    size_t internal_nodes = 0;
    size_t leaf_nodes     = 0;
    size_t total_keys     = 0;
    size_t splits         = 0;
    size_t redistributes  = 0;
    size_t merges         = 0;

    size_t total_nodes() const { return internal_nodes + leaf_nodes; }

    // utilization = total_keys / (total_nodes * (d - 1))
    double utilization(int d) const {
        size_t cap = total_nodes() * static_cast<size_t>(d - 1);
        return cap == 0 ? 0.0 : static_cast<double>(total_keys) / static_cast<double>(cap);
    }
};

class IIndex {
public:
    virtual ~IIndex() = default;

    virtual void                insert(Key k, RID r)              = 0;
    virtual std::optional<RID>  search(Key k) const               = 0;
    virtual void                range(Key lo, Key hi,
                                      const std::function<void(Key, RID)>& cb) const = 0;
    virtual bool                remove(Key k)                     = 0;

    virtual TreeStats           stats() const                     = 0;
    virtual int                 order() const                     = 0;
    virtual std::string         name() const                      = 0;
};

std::unique_ptr<IIndex> make_index(std::string_view kind, int d);
