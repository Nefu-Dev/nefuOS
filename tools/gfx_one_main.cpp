#include <cstdio>
#include "gfxmath/gfx_all.h"

int main(int argc, char** argv) {
    using namespace nefu::gfx;
    int which = argc > 1 ? atoi(argv[1]) : 0;
    switch (which) {
        case 1: printf("vec3=%d\n", Vec3::self_test()); break;
        case 2: printf("mat4=%d\n", Mat4::self_test()); break;
        case 3: printf("quat=%d\n", Quat::self_test()); break;
        case 4: printf("ray=%d\n", Ray::self_test()); break;
        case 5: printf("plane=%d\n", Plane::self_test()); break;
        case 6: printf("aabb=%d\n", AABB::self_test()); break;
        case 7: printf("sphere=%d\n", Sphere::self_test()); break;
        case 8: printf("camera=%d\n", Camera::self_test()); break;
        case 9: printf("mesh=%d\n", Mesh::self_test()); break;
        case 10: printf("proj=%d\n", Projector::self_test()); break;
        case 11: printf("render=%d\n", Renderer::self_test()); break;
        default:
            for (int i = 1; i <= 11; i++) {
                printf("--- %d ---\n", i);
            }
    }
    return 0;
}
