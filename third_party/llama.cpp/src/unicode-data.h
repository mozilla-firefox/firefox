#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct range_nfd {
    uint32_t first;
    uint32_t last;
    uint32_t nfd;
};

static const uint32_t MAX_CODEPOINTS = 0x110000;

extern const std::initializer_list<std::pair<uint32_t, uint16_t>> unicode_ranges_flags;
// Firefox (no static constructors): the std::unordered_set has a non-trivial
// constructor/destructor, so expose it via a lazily-initialized accessor instead
// of a load-time global. The initializer_list globals are constant-initialized
// and need no such treatment.
const std::unordered_set<uint32_t> & get_unicode_set_whitespace();
extern const std::initializer_list<std::pair<uint32_t, uint32_t>> unicode_map_lowercase;
extern const std::initializer_list<std::pair<uint32_t, uint32_t>> unicode_map_uppercase;
extern const std::initializer_list<range_nfd> unicode_ranges_nfd;

// WordPiece (BERT) normalization: full canonical decomposition, then drop Mn.
struct range_ccc {
    uint32_t first;
    uint32_t last;
    uint8_t  ccc;   // canonical combining class
};

struct nfd_full {
    uint32_t cpt;
    uint32_t parts[4];  // fully expanded, canonically ordered; unused entries are 0
};

extern const std::initializer_list<range_ccc> unicode_ranges_combining_class;
extern const std::initializer_list<nfd_full> unicode_map_nfd_full;
extern const std::initializer_list<std::pair<uint32_t, uint32_t>> unicode_ranges_nonspacing_mark;
