#pragma once

#include <string>
#include <vector>

#include "record.hpp"

std::vector<Record> load_csv(const std::string& path);
