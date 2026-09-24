#include "gfx3d/gfx3d_all.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
namespace nefu {
void* kalloc(size_t sz) { return std::calloc(1, sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void  platform_dbg(const char*) {}
}
using namespace nefu::gfx3d;
int main() {
    {
        Mesh m; make_cube(m, 1.0);
        std::printf("cube v=%d f=%d mnx=%g mxx=%g\n", m.vertex_count(), m.face_count(),
                    m.bounds.mn.x, m.bounds.mx.x);
    }
    {
        const char* obj =
            "v -1 -1 -1\nv  1 -1 -1\nv  1  1 -1\nv -1  1 -1\nf 1 2 3\nf 1 3 4\n";
        Mesh m; bool ok = obj_parse(obj, m);
        std::printf("obj ok=%d v=%d f=%d\n", (int)ok, m.vertex_count(), m.face_count());
    }
    {
        Mesh m; make_icosahedron(m, 1.0);
        std::printf("icosa v=%d f=%d\n", m.vertex_count(), m.face_count());
    }
    return 0;
}
