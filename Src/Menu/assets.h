// Menu source watermark: @use_sberbank
#pragma once

#include <cstddef>

// Binary assets compiled into the executable. The iOS port only ships the fonts:
// the avatar and the inventory pack of the donor menu are not used here.
namespace nl::assets {

struct Blob {
    const unsigned char* data;
    std::size_t size;
};

extern const Blob noto_sans_medium;
extern const Blob noto_sans_semibold;
extern const Blob noto_sans_black;
extern const Blob fa_solid;

} // namespace nl::assets
