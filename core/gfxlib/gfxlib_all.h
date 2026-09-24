// nefuOS graphics library — master header (STL-style single include)
//   #include "gfxlib/gfxlib_all.h"
// Exposes nefu::gfxlib: raster, geo, transform (Q16.16), noise/fractals
// and color.  Integer-only: runs on host and bare kernel alike.
#pragma once
#include "raster.h"
#include "geo.h"
#include "transform.h"
#include "noise.h"
#include "color.h"

// combined self test helper: returns total failures across all modules
namespace nefu { namespace gfxlib {
inline int gfxlib_all_self_test() {
    int f = 0;
    f += raster_self_test();
    f += geo_self_test();
    f += transform_self_test();
    f += noise_self_test();
    f += color_self_test();
    return f;
}
} }
