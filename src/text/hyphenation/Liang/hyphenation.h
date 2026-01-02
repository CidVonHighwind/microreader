#ifndef LIANG_HYPHENATION_H
#define LIANG_HYPHENATION_H

#include <cstddef>

#include "liang_hyphenation_patterns.h"

// Hyphenate into an output integer buffer. Returns number of positions written.
int liang_hyphenate(const char* word, size_t leftmin, size_t rightmin, char boundary_char, size_t* out_positions,
                    int max_positions, const HyphenationPatterns& pats);

#endif  // LIANG_HYPHENATION_H