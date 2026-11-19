// Menu source watermark: @use_sberbank
// The donor menu has a search overlay that re-collects every row. The iOS port
// does not ship that overlay, so collection is always off; the API is kept so
// the panel and popup widgets compile unchanged.
#include "Menu/search.h"

#include <cctype>
#include <cstring>

namespace nl::search {
namespace {

std::vector<Entry> g_entries;
std::vector<Path> g_paths;

} // namespace

bool collecting() { return false; }

void begin() {}
void end() {}
void set_tab(int, int) {}
void push(const char*) {}
void pop() {}
void add(Entry) {}

const std::vector<Entry>& entries() { return g_entries; }
const std::vector<Path>& paths() { return g_paths; }

bool matches(const char* text, const char* query)
{
    if (!text || !query)
        return false;
    for (const char* t = text; *t; ++t) {
        const char* a = t;
        const char* b = query;
        while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

void fold_query(const char* query, char* out, size_t size)
{
    if (!out || size == 0)
        return;
    const char* source = query ? query : "";
    std::strncpy(out, source, size - 1);
    out[size - 1] = '\0';
}

} // namespace nl::search
