#include <cstdio>
#include <cmath>
#include "gfxmath/mesh.h"

using namespace nefu::gfx;
static int F = 0;
#define CK(cond, name) do { if (!(cond)) { F++; printf("FAIL: %s\n", name); } } while (0)

int main() {
    // 1
    {
        Mesh m;
        int a = m.add_vertex(Vec3(0, 0, 0));
        int b = m.add_vertex(Vec3(1, 0, 0));
        int c = m.add_vertex(Vec3(0, 1, 0));
        m.add_triangle(a, b, c);
        CK(m.vertex_count() == 3, "mesh1-v");
        CK(m.triangle_count() == 1, "mesh1-t");
        int ta, tb, tc;
        m.get_triangle(0, ta, tb, tc);
        CK(ta == a && tb == b && tc == c, "mesh1-g");
    }
    // 2
    {
        Mesh m;
        m.add_triangle(m.add_vertex(Vec3(0, 0, 0)),
                       m.add_vertex(Vec3(1, 0, 0)),
                       m.add_vertex(Vec3(0, 1, 0)));
        m.compute_normals();
        CK(std::abs(m.vertex(0).normal.z - 1) < 1e-9, "mesh2-n");
    }
    // 3
    {
        Mesh c = Mesh::cube(2);
        CK(c.vertex_count() == 8, "mesh3-v");
        CK(c.triangle_count() == 12, "mesh3-t");
        Vec3 mn, mx;
        c.bounding_box(mn, mx);
        CK(mn.x == -1 && mx.x == 1 && mn.y == -1 && mx.y == 1, "mesh3-bb");
    }
    // 4
    {
        Mesh s = Mesh::sphere(1, 8, 12);
        printf("sphere v=%d t=%d\n", s.vertex_count(), s.triangle_count());
        CK(s.vertex_count() >= 40, "mesh4-v");
        CK(s.triangle_count() >= 80, "mesh4-t");
        Vec3 mn, mx;
        s.bounding_box(mn, mx);
        CK(mx.x <= 1.001 && mn.x >= -1.001, "mesh4-bb");
    }
    // 5
    {
        Mesh p = Mesh::plane(2, 2, 4, 4);
        printf("plane v=%d t=%d\n", p.vertex_count(), p.triangle_count());
        CK(p.vertex_count() == 25, "mesh5-v");
        CK(p.triangle_count() == 32, "mesh5-t");
    }
    // 6
    {
        Mesh m;
        m.add_triangle(m.add_vertex(Vec3(0, 0, 0)), m.add_vertex(Vec3(1, 0, 0)), m.add_vertex(Vec3(0, 1, 0)));
        m.compute_normals();
        Mat4 T = Mat4::translation(10, 0, 0);
        m.transform(T, T);
        CK(std::abs(m.vertex(0).pos.x - 10) < 1e-9, "mesh6-t");
    }
    printf("mesh total=%d\n", F);
    return 0;
}
