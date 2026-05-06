#include "csv_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

void split6(const std::string& line, std::string out[6]) {
    size_t start = 0;
    int    field = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            if (field >= 6) throw std::runtime_error("too many fields: " + line);
            out[field++] = line.substr(start, i - start);
            start        = i + 1;
        }
    }
    if (field != 6) throw std::runtime_error("expected 6 fields, got " + std::to_string(field) + ": " + line);
}

}

std::vector<Record> load_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);

    std::vector<Record> out;
    out.reserve(100'000);

    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("empty file: " + path);
    // header skipped

    std::string f[6];
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        split6(line, f);

        Record r;
        r.id     = static_cast<Key>(std::stoul(f[0]));
        r.name   = std::move(f[1]);
        r.male   = (f[2] == "Male");
        r.gpa    = std::stof(f[3]);
        r.height = std::stof(f[4]);
        r.weight = std::stof(f[5]);
        out.push_back(std::move(r));
    }
    return out;
}
