#pragma once

#include <cstdint>
#include <string>

using Key = uint32_t;
using RID = uint32_t;

struct Record {
    Key id;
    std::string name;
    bool male;
    float gpa;
    float height;
    float weight;
};
