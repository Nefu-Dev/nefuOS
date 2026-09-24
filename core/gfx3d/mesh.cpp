// ============================================================================
// nefuOS 3D 图形库 —— mesh 实现
// ============================================================================
#include "mesh.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace nefu {
namespace gfx3d {

static inline double f_abs(double x) { return x < 0 ? -x : x; }

// ============================================================================
// 法线计算
// ============================================================================
void Mesh::compute_smooth_normals() {
    // 先清零，再逐面累加面积法线
    for (int i = 0; i < verts.size(); i++) verts[i].normal = Vec3(0, 0, 0);
    for (int i = 0; i < faces.size(); i++) {
        const Face& f = faces[i];
        const Vec3& a = verts[f[0]].pos;
        const Vec3& b = verts[f[1]].pos;
        const Vec3& c = verts[f[2]].pos;
        Vec3 n = (b - a).cross(c - a);    // 叉乘长度 = 2*面积，天然面积加权
        verts[f[0]].normal += n;
        verts[f[1]].normal += n;
        verts[f[2]].normal += n;
    }
    for (int i = 0; i < verts.size(); i++)
        verts[i].normal = verts[i].normal.normalized();
}

void Mesh::compute_flat_normals() {
    // 平面着色：每个面三个顶点使用同一个法线
    for (int i = 0; i < faces.size(); i++) {
        const Face& f = faces[i];
        const Vec3& a = verts[f[0]].pos;
        const Vec3& b = verts[f[1]].pos;
        const Vec3& c = verts[f[2]].pos;
        Vec3 n = (b - a).cross(c - a).normalized();
        verts[f[0]].normal = n;
        verts[f[1]].normal = n;
        verts[f[2]].normal = n;
    }
}

// ============================================================================
// 网格变换
// ============================================================================
void Mesh::translate(const Vec3& t) {
    for (int i = 0; i < verts.size(); i++) verts[i].pos += t;
    bounds = AABB();
    for (int i = 0; i < verts.size(); i++) bounds.expand(verts[i].pos);
}

void Mesh::rotate(const Mat4& rot) {
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = rot.m[i][j];
    for (int i = 0; i < verts.size(); i++) {
        verts[i].pos = r * verts[i].pos;
        verts[i].normal = (r * verts[i].normal).normalized();
    }
}

void Mesh::scale(const Vec3& s) {
    for (int i = 0; i < verts.size(); i++) {
        verts[i].pos.x *= s.x;
        verts[i].pos.y *= s.y;
        verts[i].pos.z *= s.z;
    }
    bounds = AABB();
    for (int i = 0; i < verts.size(); i++) bounds.expand(verts[i].pos);
}

void Mesh::transform(const Mat4& m) {
    // 法线用逆转置矩阵（这里用旋转部分近似，对均匀缩放足够）
    Mat3 rot;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) rot.m[i][j] = m.m[i][j];
    Mat3 nrm = inverse(transpose(rot));
    for (int i = 0; i < verts.size(); i++) {
        Vec4 p = m * Vec4(verts[i].pos, 1.0);
        verts[i].pos = p.xyz();
        verts[i].normal = (nrm * verts[i].normal).normalized();
    }
    bounds = AABB();
    for (int i = 0; i < verts.size(); i++) bounds.expand(verts[i].pos);
}

void Mesh::merge(const Mesh& other) {
    int base = verts.size();
    for (int i = 0; i < other.verts.size(); i++) verts.push(other.verts[i]);
    for (int i = 0; i < other.faces.size(); i++) {
        Face f;
        f[0] = other.faces[i][0] + base;
        f[1] = other.faces[i][1] + base;
        f[2] = other.faces[i][2] + base;
        faces.push(f);
        int mi = (i < other.face_mat.size()) ? other.face_mat[i] : -1;
        face_mat.push(mi);
    }
    bounds.expand(other.bounds);
}

// ============================================================================
// 小文本工具：跳过空白、读取一个 token、解析浮点数
// ============================================================================
static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

// 从 p 解析一个 double，返回解析后的指针
static const char* parse_f64(const char* p, double& out) {
    // 简单浮点解析（支持负号、小数点、科学计数法后缀 e/E 也容忍）
    char* end = 0;
    out = std::strtod(p, &end);
    if (end == p) return 0;
    return end;
}

// 读取一个 token 到 buf（连续非空白字符），返回指针
static const char* read_token(const char* p, char* buf, int bufsiz) {
    p = skip_ws(p);
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (i < bufsiz - 1) buf[i++] = *p;
        p++;
    }
    buf[i] = 0;
    return p;
}

// 跳到下一行开头
static const char* next_line(const char* p) {
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    return p;
}

// ============================================================================
// OBJ 解析
// ============================================================================
bool obj_parse(const char* data, Mesh& out) {
    if (!data) return false;

    // OBJ 允许面索引引用 vt/vn，这里分别缓存
    List<Vec3> raw_v;
    List<Vec2> raw_vt;
    List<Vec3> raw_vn;

    const char* p = data;
    while (*p) {
        const char* line_start = p;
        p = skip_ws(p);
        if (*p == '#') { p = next_line(p); continue; }
        if (*p == 'v' && (*(p + 1) == ' ' || *(p + 1) == '\t' || *(p + 1) == 0)) {
            // v x y z [w]
            double x, y, z;
            p++;
            p = skip_ws(p); p = parse_f64(p, x);
            p = skip_ws(p); p = parse_f64(p, y);
            p = skip_ws(p); p = parse_f64(p, z);
            raw_v.push(Vec3(x, y, z));
        } else if (*p == 'v' && *(p + 1) == 't') {
            // vt u v [w]
            double u, vv;
            p += 2;
            p = skip_ws(p); p = parse_f64(p, u);
            p = skip_ws(p); p = parse_f64(p, vv);
            raw_vt.push(Vec2(u, vv));
        } else if (*p == 'v' && *(p + 1) == 'n') {
            // vn x y z
            double nx, ny, nz;
            p += 2;
            p = skip_ws(p); p = parse_f64(p, nx);
            p = skip_ws(p); p = parse_f64(p, ny);
            p = skip_ws(p); p = parse_f64(p, nz);
            raw_vn.push(Vec3(nx, ny, nz));
        } else if (*p == 'f') {
            // f v/vt/vn v/vt/vn ... （支持多边形，三角扇化）
            p++;
            int idx[64];          // 顶点索引（1-based）
            int n = 0;
            while (1) {
                // 按空白分隔 token，但绝不跨行
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '\n' || *p == '\r' || *p == 0) break;
                char buf[32];
                // 读 v 部分（到 / 或空白为止）
                int i = 0;
                while (*p && *p != '/' && *p != ' ' && *p != '\t' &&
                       *p != '\r' && *p != '\n' && *p != 0) {
                    if (i < 31) buf[i++] = *p;
                    p++;
                }
                buf[i] = 0;
                int vi = std::atoi(buf);
                // 跳过 /vt/vn 剩余部分
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
                if (vi < 0) vi = raw_v.size() + vi + 1;   // 负索引 = 相对末尾
                idx[n++] = vi;
                if (n >= 64) break;
            }
            // 三角扇化 (0,1,2),(0,2,3)...
            for (int k = 1; k + 1 < n; k++) {
                Face f(idx[0] - 1, idx[k] - 1, idx[k + 1] - 1);
                out.faces.push(f);
                out.face_mat.push(-1);
            }
            continue;   // 已消费整行
        } else {
            // 其他行（mtllib/usemtl/o/g 等）忽略
        }
        p = next_line(line_start);
    }

    // 把 raw_v 拷贝进 Mesh.verts，补法线和 uv
    for (int i = 0; i < raw_v.size(); i++) {
        Vertex v;
        v.pos = raw_v[i];
        if (i < raw_vn.size()) v.normal = raw_vn[i];
        out.verts.push(v);
    }
    // 若没有任何法线，计算平滑法线
    if (raw_vn.size() == 0 && out.faces.size() > 0) out.compute_smooth_normals();

    // 重新计算包围盒
    out.bounds = AABB();
    for (int i = 0; i < out.verts.size(); i++) out.bounds.expand(out.verts[i].pos);
    return out.verts.size() > 0;
}

// ============================================================================
// MTL 解析
// ============================================================================
bool mtl_parse(const char* data, List<Material>& out) {
    if (!data) return false;
    Material cur;
    bool has = false;
    const char* p = data;
    while (*p) {
        p = skip_ws(p);
        char tok[32];
        p = read_token(p, tok, sizeof(tok));
        if (std::strcmp(tok, "newmtl") == 0) {
            if (has) out.push(cur);
            cur = Material();
            p = skip_ws(p);
            int i = 0;
            while (*p && *p != '\r' && *p != '\n') {
                if (i < 63) cur.name.data()[i++] = *p;
                p++;
            }
            has = true;
        } else if (std::strcmp(tok, "Kd") == 0) {
            double r, g, b;
            p = skip_ws(p); p = parse_f64(p, r);
            p = skip_ws(p); p = parse_f64(p, g);
            p = skip_ws(p); p = parse_f64(p, b);
            cur.diffuse = Vec3(r, g, b);
        } else if (std::strcmp(tok, "Ks") == 0) {
            double r, g, b;
            p = skip_ws(p); p = parse_f64(p, r);
            p = skip_ws(p); p = parse_f64(p, g);
            p = skip_ws(p); p = parse_f64(p, b);
            cur.specular = Vec3(r, g, b);
        } else if (std::strcmp(tok, "Ka") == 0) {
            double r, g, b;
            p = skip_ws(p); p = parse_f64(p, r);
            p = skip_ws(p); p = parse_f64(p, g);
            p = skip_ws(p); p = parse_f64(p, b);
            cur.ambient = Vec3(r, g, b);
        } else if (std::strcmp(tok, "Ns") == 0) {
            double s; p = skip_ws(p); p = parse_f64(p, s);
            cur.shininess = s;
        } else if (std::strcmp(tok, "d") == 0 || std::strcmp(tok, "Tr") == 0) {
            double a; p = skip_ws(p); p = parse_f64(p, a);
            cur.alpha = (tok[0] == 'd') ? a : (1.0 - a);
        }
        p = next_line(p);
    }
    if (has) out.push(cur);
    return out.size() > 0;
}

// ============================================================================
// PLY 解析（ASCII + binary_little_endian）
// ============================================================================
static float le_to_float(const uint8_t* p) {
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

bool ply_parse(const char* data, int size, Mesh& out) {
    if (!data || size < 16) return false;
    // 头必须以 "ply" 开头
    if (data[0] != 'p' || data[1] != 'l' || data[2] != 'y') return false;

    bool binary = false;
    int vert_count = 0, face_count = 0;
    int x_idx = 0, y_idx = 1, z_idx = 2;   // vertex 属性顺序
    int vert_props = 0;
    bool has_nx = false, has_ny = false, has_nz = false;

    // 解析头
    const char* p = data;
    const char* end = data + size;
    while (p < end) {
        const char* line_start = p;
        // 找行尾
        const char* eol = p;
        while (eol < end && *eol != '\n') eol++;
        // 临时 NUL 终止这一行
        char saved = *eol;
        const_cast<char*>(eol)[0] = 0;

        char tok[32];
        const char* q = skip_ws(p);
        q = read_token(q, tok, sizeof(tok));
        if (std::strcmp(tok, "format") == 0) {
            char fmt[32]; q = skip_ws(q); read_token(q, fmt, sizeof(fmt));
            binary = (std::strstr(fmt, "binary") != 0);
        } else if (std::strcmp(tok, "element") == 0) {
            char el[32]; int cnt = 0;
            q = skip_ws(q); q = read_token(q, el, sizeof(el));
            q = skip_ws(q); cnt = std::atoi(q);
            if (std::strcmp(el, "vertex") == 0) { vert_count = cnt; vert_props = 0; }
            else if (std::strcmp(el, "face") == 0) { face_count = cnt; }
        } else if (std::strcmp(tok, "property") == 0) {
            char type[32], name[32];
            q = skip_ws(q); q = read_token(q, type, sizeof(type));
            if (std::strcmp(type, "list") == 0) {
                // face 列表属性，跳过
            } else {
                q = skip_ws(q); q = read_token(q, name, sizeof(name));
                if (std::strcmp(name, "x") == 0) x_idx = vert_props;
                if (std::strcmp(name, "y") == 0) y_idx = vert_props;
                if (std::strcmp(name, "z") == 0) z_idx = vert_props;
                if (std::strcmp(name, "nx") == 0) has_nx = true;
                vert_props++;
            }
        } else if (std::strcmp(tok, "end_header") == 0) {
            p = eol + 1;
            break;
        }
        const_cast<char*>(eol)[0] = saved;
        p = eol + 1;
    }

    if (vert_count <= 0) return false;

    if (!binary) {
        // ASCII：逐行读 vertex
        for (int i = 0; i < vert_count && p < end; i++) {
            Vertex v;
            double x = 0, y = 0, z = 0;
            for (int k = 0; k < vert_props; k++) {
                double val;
                p = skip_ws(p);
                const char* np = parse_f64(p, val);
                if (!np) break;
                p = np;
                if (k == x_idx) x = val;
                else if (k == y_idx) y = val;
                else if (k == z_idx) z = val;
            }
            v.pos = Vec3(x, y, z);
            out.verts.push(v);
        }
        // face 行
        for (int i = 0; i < face_count && p < end; i++) {
            int n = 0;
            p = skip_ws(p);
            n = std::atoi(p);
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            int idx[16];
            for (int k = 0; k < n && k < 16; k++) {
                p = skip_ws(p);
                idx[k] = std::atoi(p);
                while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            }
            for (int k = 1; k + 1 < n; k++) {
                out.faces.push(Face(idx[0], idx[k], idx[k + 1]));
                out.face_mat.push(-1);
            }
        }
    } else {
        // binary little-endian：vertex = 3 float32 (x,y,z)，按我们写的导出约定
        const uint8_t* bp = (const uint8_t*)p;
        const uint8_t* bend = (const uint8_t*)end;
        int floats_per_vert = vert_props;
        for (int i = 0; i < vert_count && bp + floats_per_vert * 4 <= bend; i++) {
            float x = le_to_float(bp + x_idx * 4);
            float y = le_to_float(bp + y_idx * 4);
            float z = le_to_float(bp + z_idx * 4);
            bp += floats_per_vert * 4;
            Vertex v; v.pos = Vec3(x, y, z);
            out.verts.push(v);
        }
        // face: uchar count + count*int32 indices
        for (int i = 0; i < face_count && bp < bend; i++) {
            uint8_t n = *bp++;
            int idx[16];
            for (int k = 0; k < n && k < 16 && bp + 4 <= bend; k++) {
                uint32_t u = (uint32_t)bp[0] | ((uint32_t)bp[1] << 8) |
                             ((uint32_t)bp[2] << 16) | ((uint32_t)bp[3] << 24);
                bp += 4;
                idx[k] = (int)u;
            }
            // 跳过 uint16 对齐填充（PLY 不强制，这里假设紧跟）
            for (int k = 1; k + 1 < n; k++) {
                out.faces.push(Face(idx[0], idx[k], idx[k + 1]));
                out.face_mat.push(-1);
            }
        }
    }

    if (out.faces.size() > 0) out.compute_smooth_normals();
    out.bounds = AABB();
    for (int i = 0; i < out.verts.size(); i++) out.bounds.expand(out.verts[i].pos);
    (void)has_nx;
    return out.verts.size() > 0;
}

// ============================================================================
// STL 解析
// ============================================================================
bool stl_parse(const char* data, int size, Mesh& out) {
    if (!data || size < 84) return false;
    // ASCII STL 以 "solid" 开头（但 binary STL 也可能以 solid 开头，
    // 用文件大小判断：80 字节头 + uint32 三角形数 + 50*n 字节）
    bool ascii = true;
    if (std::strncmp(data, "solid", 5) == 0) {
        // 在头 500 字节里找 "facet normal" 才算 ASCII
        int scan = size < 500 ? size : 500;
        bool found_facet = false;
        for (int i = 0; i < scan - 12; i++) {
            if (std::strncmp(data + i, "facet normal", 12) == 0) { found_facet = true; break; }
        }
        ascii = found_facet;
    } else {
        ascii = false;
    }

    if (!ascii) {
        // binary: 80 字节头，4 字节 triangle count
        uint32_t n = (uint32_t)((uint8_t)data[80]) |
                     ((uint32_t)(uint8_t)data[81] << 8) |
                     ((uint32_t)(uint8_t)data[82] << 16) |
                     ((uint32_t)(uint8_t)data[83] << 24);
        const uint8_t* bp = (const uint8_t*)data + 84;
        for (uint32_t t = 0; t < n; t++) {
            // 每个三角形：12 floats（normal + 3 verts）+ uint16 attr
            const uint8_t* tri = bp + t * 50;
            if (tri + 50 > (const uint8_t*)data + size) break;
            float nx = le_to_float(tri + 0);
            float ny = le_to_float(tri + 4);
            float nz = le_to_float(tri + 8);
            Vec3 n(nx, ny, nz);
            int base = out.verts.size();
            for (int k = 0; k < 3; k++) {
                float x = le_to_float(tri + 12 + k * 12 + 0);
                float y = le_to_float(tri + 12 + k * 12 + 4);
                float z = le_to_float(tri + 12 + k * 12 + 8);
                Vertex v(Vec3(x, y, z), n);
                out.verts.push(v);
            }
            out.faces.push(Face(base, base + 1, base + 2));
            out.face_mat.push(-1);
        }
    } else {
        // ASCII：facet normal / outer loop / vertex / endloop / endfacet
        const char* p = data;
        while (*p) {
            p = skip_ws(p);
            if (std::strncmp(p, "facet normal", 12) == 0) {
                double nx, ny, nz;
                p += 12;
                p = skip_ws(p); p = parse_f64(p, nx);
                p = skip_ws(p); p = parse_f64(p, ny);
                p = skip_ws(p); p = parse_f64(p, nz);
                Vec3 n(nx, ny, nz);
                int base = out.verts.size();
                for (int k = 0; k < 3; k++) {
                    p = next_line(p);
                    p = skip_ws(p);
                    // 找 "vertex"
                    while (*p && std::strncmp(p, "vertex", 6) != 0) p = next_line(p);
                    p += 6;
                    double x, y, z;
                    p = skip_ws(p); p = parse_f64(p, x);
                    p = skip_ws(p); p = parse_f64(p, y);
                    p = skip_ws(p); p = parse_f64(p, z);
                    Vertex v(Vec3(x, y, z), n);
                    out.verts.push(v);
                }
                out.faces.push(Face(base, base + 1, base + 2));
                out.face_mat.push(-1);
            }
            p = next_line(p);
        }
    }

    out.bounds = AABB();
    for (int i = 0; i < out.verts.size(); i++) out.bounds.expand(out.verts[i].pos);
    return out.faces.size() > 0;
}

bool mesh_load(const char* path_hint, const char* data, int size, Mesh& out) {
    if (!path_hint || !data) return false;
    const char* dot = std::strrchr(path_hint, '.');
    if (!dot) return false;
    if (std::strstr(dot, ".obj") != 0) return obj_parse(data, out);
    if (std::strstr(dot, ".ply") != 0) return ply_parse(data, size, out);
    if (std::strstr(dot, ".stl") != 0) return stl_parse(data, size, out);
    if (std::strstr(dot, ".mtl") != 0) {
        List<Material> m;
        bool ok = mtl_parse(data, m);
        for (int i = 0; i < m.size(); i++) out.materials.push(m[i]);
        return ok;
    }
    return false;
}

// ============================================================================
// 图元生成器
// ============================================================================
void make_cube(Mesh& out, double size) {
    double h = size * 0.5;
    // 8 个顶点
    for (int x = 0; x < 2; x++)
        for (int y = 0; y < 2; y++)
            for (int z = 0; z < 2; z++) {
                Vertex v(Vec3(x ? h : -h, y ? h : -h, z ? h : -h));
                v.uv = Vec2(x, y);
                out.verts.push(v);
            }
    // 12 个三角面（6 个面 * 2 三角形）
    // 顶点编号约定：idx = (x<<2)|(y<<1)|z
    static const int faces[12][3] = {
        {0,1,3},{0,3,2},   // 底面 z=-
        {4,6,7},{4,7,5},   // 顶面 z=+
        {0,4,5},{0,5,1},   // 前面 y=-
        {2,3,7},{2,7,6},   // 后面 y=+
        {0,2,6},{0,6,4},   // 左面 x=-
        {1,5,7},{1,7,3},   // 右面 x=+
    };
    for (int i = 0; i < 12; i++) {
        out.faces.push(Face(faces[i][0], faces[i][1], faces[i][2]));
        out.face_mat.push(-1);
    }
    out.compute_smooth_normals();
    out.bounds = AABB(Vec3(-h, -h, -h), Vec3(h, h, h));
}

void make_plane(Mesh& out, double w, double h, int seg) {
    // XZ 平面（y=0），seg*seg 网格
    for (int z = 0; z <= seg; z++)
        for (int x = 0; x <= seg; x++) {
            double fx = (double)x / seg - 0.5;
            double fz = (double)z / seg - 0.5;
            Vertex v(Vec3(fx * w, 0, fz * h), Vec3(0, 1, 0));
            v.uv = Vec2((double)x / seg, (double)z / seg);
            out.verts.push(v);
        }
    for (int z = 0; z < seg; z++)
        for (int x = 0; x < seg; x++) {
            int a = z * (seg + 1) + x;
            int b = a + 1;
            int c = a + (seg + 1);
            int d = c + 1;
            out.faces.push(Face(a, c, b));
            out.faces.push(Face(b, c, d));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
    out.bounds = AABB(Vec3(-w/2, 0, -h/2), Vec3(w/2, 0, h/2));
}

void make_sphere_uv(Mesh& out, double r, int rings, int segs) {
    // UV 球：极点在 ±Y
    for (int ri = 0; ri <= rings; ri++) {
        double theta = PI_D * (double)ri / rings;       // 0..PI
        double st = std::sin(theta), ct = std::cos(theta);
        for (int si = 0; si <= segs; si++) {
            double phi = TAU_D * (double)si / segs;     // 0..2PI
            double sp = std::sin(phi), cp = std::cos(phi);
            Vec3 p(r * st * cp, r * ct, r * st * sp);
            Vertex v(p, p.normalized());
            v.uv = Vec2((double)si / segs, (double)ri / rings);
            out.verts.push(v);
        }
    }
    for (int ri = 0; ri < rings; ri++)
        for (int si = 0; si < segs; si++) {
            int a = ri * (segs + 1) + si;
            int b = a + 1;
            int c = a + (segs + 1);
            int d = c + 1;
            out.faces.push(Face(a, c, b));
            out.faces.push(Face(b, c, d));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
    out.bounds = AABB(Vec3(-r, -r, -r), Vec3(r, r, r));
}

void make_sphere_latlong(Mesh& out, double r, int subdiv) {
    // 由二十面体细分得到（比 UV 球均匀，无极点收敛）
    make_icosahedron(out, r);
    // 简单递归细分：把每条边中点归一化到球面
    for (int s = 0; s < subdiv; s++) {
        Mesh tmp;
        for (int f = 0; f < out.faces.size(); f++) {
            const Face& F = out.faces[f];
            Vec3 a = out.verts[F[0]].pos;
            Vec3 b = out.verts[F[1]].pos;
            Vec3 c = out.verts[F[2]].pos;
            Vec3 ab = ((a + b) * 0.5).normalized() * r;
            Vec3 bc = ((b + c) * 0.5).normalized() * r;
            Vec3 ca = ((c + a) * 0.5).normalized() * r;
            int i0 = tmp.verts.size(); tmp.verts.push(Vertex(a, a.normalized()));
            int i1 = tmp.verts.size(); tmp.verts.push(Vertex(ab, ab.normalized()));
            int i2 = tmp.verts.size(); tmp.verts.push(Vertex(b, b.normalized()));
            int i3 = tmp.verts.size(); tmp.verts.push(Vertex(bc, bc.normalized()));
            int i4 = tmp.verts.size(); tmp.verts.push(Vertex(c, c.normalized()));
            int i5 = tmp.verts.size(); tmp.verts.push(Vertex(ca, ca.normalized()));
            tmp.faces.push(Face(i0, i1, i5));
            tmp.faces.push(Face(i1, i3, i5));
            tmp.faces.push(Face(i1, i2, i3));
            tmp.faces.push(Face(i5, i3, i4));
        }
        out.verts = tmp.verts;
        out.faces = tmp.faces;
    }
    out.compute_smooth_normals();
    out.bounds = AABB(Vec3(-r, -r, -r), Vec3(r, r, r));
}

void make_cylinder(Mesh& out, double r, double h, int segs) {
    double half = h * 0.5;
    // 侧面 + 上下盖
    int side_base = out.verts.size();
    for (int i = 0; i <= segs; i++) {
        double a = TAU_D * (double)i / segs;
        double x = r * std::cos(a), z = r * std::sin(a);
        Vertex vb(Vec3(x, -half, z), Vec3(x / r, 0, z / r));
        vb.uv = Vec2((double)i / segs, 0);
        out.verts.push(vb);
        Vertex vt(Vec3(x, half, z), Vec3(x / r, 0, z / r));
        vt.uv = Vec2((double)i / segs, 1);
        out.verts.push(vt);
    }
    for (int i = 0; i < segs; i++) {
        int a = side_base + i * 2;
        out.faces.push(Face(a, a + 2, a + 1));
        out.faces.push(Face(a + 1, a + 2, a + 3));
        out.face_mat.push(-1); out.face_mat.push(-1);
    }
    // 顶盖（法线 +Y）与底盖（-Y）
    int top_center = out.verts.size();
    out.verts.push(Vertex(Vec3(0, half, 0), Vec3(0, 1, 0)));
    int ring_top = side_base + 1;
    for (int i = 0; i < segs; i++) {
        int a = ring_top + i * 2;
        int b = ring_top + (i + 1) * 2;
        out.faces.push(Face(top_center, b, a));
        out.face_mat.push(-1);
    }
    int bot_center = out.verts.size();
    out.verts.push(Vertex(Vec3(0, -half, 0), Vec3(0, -1, 0)));
    int ring_bot = side_base;
    for (int i = 0; i < segs; i++) {
        int a = ring_bot + i * 2;
        int b = ring_bot + (i + 1) * 2;
        out.faces.push(Face(bot_center, a, b));
        out.face_mat.push(-1);
    }
    out.bounds = AABB(Vec3(-r, -half, -r), Vec3(r, half, r));
}

void make_cone(Mesh& out, double r, double h, int segs) {
    double half = h * 0.5;
    int apex = out.verts.size();
    out.verts.push(Vertex(Vec3(0, half, 0), Vec3(0, 1, 0)));
    int base = out.verts.size();
    for (int i = 0; i <= segs; i++) {
        double a = TAU_D * (double)i / segs;
        double x = r * std::cos(a), z = r * std::sin(a);
        out.verts.push(Vertex(Vec3(x, -half, z), Vec3(x, 0, z).normalized()));
    }
    for (int i = 0; i < segs; i++) {
        out.faces.push(Face(apex, base + i + 1, base + i));
        out.face_mat.push(-1);
    }
    int ctr = out.verts.size();
    out.verts.push(Vertex(Vec3(0, -half, 0), Vec3(0, -1, 0)));
    for (int i = 0; i < segs; i++) {
        out.faces.push(Face(ctr, base + i, base + i + 1));
        out.face_mat.push(-1);
    }
    out.bounds = AABB(Vec3(-r, -half, -r), Vec3(r, half, r));
}

void make_torus(Mesh& out, double R, double r, int major, int minor) {
    for (int i = 0; i <= major; i++) {
        double u = TAU_D * (double)i / major;
        double cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= minor; j++) {
            double v = TAU_D * (double)j / minor;
            double cv = std::cos(v), sv = std::sin(v);
            Vec3 p((R + r * cv) * cu, r * sv, (R + r * cv) * su);
            Vec3 n(cv * cu, sv, cv * su);
            Vertex vert(p, n.normalized());
            vert.uv = Vec2((double)i / major, (double)j / minor);
            out.verts.push(vert);
        }
    }
    for (int i = 0; i < major; i++)
        for (int j = 0; j < minor; j++) {
            int a = i * (minor + 1) + j;
            int b = a + 1;
            int c = a + (minor + 1);
            int d = c + 1;
            out.faces.push(Face(a, c, b));
            out.faces.push(Face(b, c, d));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
    out.bounds = AABB(Vec3(-R - r, -r, -R - r), Vec3(R + r, r, R + r));
}

void make_tetrahedron(Mesh& out, double s) {
    double a = s * 0.5;
    out.verts.push(Vertex(Vec3(a, a, a), Vec3(0, 0, 0)));
    out.verts.push(Vertex(Vec3(a, -a, -a), Vec3(0, 0, 0)));
    out.verts.push(Vertex(Vec3(-a, a, -a), Vec3(0, 0, 0)));
    out.verts.push(Vertex(Vec3(-a, -a, a), Vec3(0, 0, 0)));
    static const int f[4][3] = {
        {0,1,2},{0,3,1},{0,2,3},{1,3,2}
    };
    for (int i = 0; i < 4; i++) {
        out.faces.push(Face(f[i][0], f[i][1], f[i][2]));
        out.face_mat.push(-1);
    }
    out.compute_smooth_normals();
}

void make_octahedron(Mesh& out, double s) {
    out.verts.push(Vertex(Vec3( s, 0, 0)));   // 0 +X
    out.verts.push(Vertex(Vec3(-s, 0, 0)));   // 1 -X
    out.verts.push(Vertex(Vec3(0,  s, 0)));   // 2 +Y
    out.verts.push(Vertex(Vec3(0, -s, 0)));   // 3 -Y
    out.verts.push(Vertex(Vec3(0, 0,  s)));   // 4 +Z
    out.verts.push(Vertex(Vec3(0, 0, -s)));   // 5 -Z
    static const int f[8][3] = {
        {0,2,4},{2,1,4},{1,3,4},{3,0,4},
        {2,0,5},{1,2,5},{3,1,5},{0,3,5}
    };
    for (int i = 0; i < 8; i++) {
        out.faces.push(Face(f[i][0], f[i][1], f[i][2]));
        out.face_mat.push(-1);
    }
    out.compute_smooth_normals();
}

void make_icosahedron(Mesh& out, double s) {
    double t = (1.0 + std::sqrt(5.0)) / 2.0;
    // 12 个顶点（黄金比例矩形三轴排列）
    Vec3 p[12] = {
        Vec3(-1,  t, 0), Vec3(1,  t, 0), Vec3(-1, -t, 0), Vec3(1, -t, 0),
        Vec3(0, -1,  t), Vec3(0, 1,  t), Vec3(0, -1, -t), Vec3(0, 1, -t),
        Vec3( t, 0, -1), Vec3( t, 0, 1), Vec3(-t, 0, -1), Vec3(-t, 0, 1)
    };
    for (int i = 0; i < 12; i++) {
        p[i] = p[i].normalized() * s;
        out.verts.push(Vertex(p[i], p[i].normalized()));
    }
    static const int f[20][3] = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    for (int i = 0; i < 20; i++) {
        out.faces.push(Face(f[i][0], f[i][1], f[i][2]));
        out.face_mat.push(-1);
    }
    out.compute_smooth_normals();
    out.bounds = AABB(Vec3(-s, -s, -s), Vec3(s, s, s));
}

void make_teapot(Mesh& out, double scale, int subdiv) {
    // 简化茶壶：组合体 —— 球体壶身 + 圆柱壶嘴 + 圆环壶把 + 小锥壶盖
    (void)subdiv;
    Mesh body;
    make_sphere_uv(body, scale, 16, 24);
    body.scale(Vec3(1.0, 0.8, 1.0));
    out.merge(body);

    Mesh spout;
    make_cylinder(spout, scale * 0.12, scale * 0.9, 12);
    spout.transform(mat4_translate(scale * 0.9, scale * 0.2, 0) *
                    mat4_rotate_z(deg2rad(-30)));
    out.merge(spout);

    Mesh handle;
    make_torus(handle, scale * 0.45, scale * 0.09, 16, 8);
    handle.transform(mat4_translate(-scale * 0.9, scale * 0.1, 0) *
                     mat4_rotate_y(deg2rad(90)));
    out.merge(handle);

    Mesh lid;
    make_cone(lid, scale * 0.3, scale * 0.25, 16);
    lid.translate(Vec3(0, scale * 0.85, 0));
    out.merge(lid);

    out.compute_smooth_normals();
    out.bounds = AABB();
    for (int i = 0; i < out.verts.size(); i++) out.bounds.expand(out.verts[i].pos);
}

// ============================================================================
// 网格后处理
// ============================================================================
int mesh_weld_vertices(Mesh& m, double eps) {
    // 简单 O(n^2) 焊接（教学场景顶点数不大）
    int* remap = new int[m.verts.size()];
    int kept = 0;
    for (int i = 0; i < m.verts.size(); i++) {
        bool found = false;
        for (int j = 0; j < kept; j++) {
            if ((m.verts[i].pos - m.verts[j].pos).length_sq() < eps * eps) {
                remap[i] = j;
                found = true;
                break;
            }
        }
        if (!found) {
            remap[i] = kept;
            if (kept != i) m.verts[kept] = m.verts[i];
            kept++;
        }
    }
    // 更新面索引
    for (int f = 0; f < m.faces.size(); f++)
        for (int k = 0; k < 3; k++)
            m.faces[f][k] = remap[m.faces[f][k]];
    // 截断顶点列表到 kept
    while (m.verts.size() > kept) m.verts.pop();
    delete[] remap;
    return m.verts.size();
}

void make_lathe(Mesh& out, const Vec2* profile, int n_profile, int segs) {
    for (int i = 0; i <= segs; i++) {
        double ang = TAU_D * (double)i / segs;
        double c = std::cos(ang), s = std::sin(ang);
        for (int j = 0; j < n_profile; j++) {
            double r = profile[j].x, y = profile[j].y;
            Vec3 p(r * c, y, r * s);
            Vec3 n(c, 0, s);
            out.verts.push(Vertex(p, n.normalized()));
        }
    }
    for (int i = 0; i < segs; i++)
        for (int j = 0; j + 1 < n_profile; j++) {
            int a = i * n_profile + j;
            int b = a + 1;
            int c = a + n_profile;
            int d = c + 1;
            out.faces.push(Face(a, c, b));
            out.faces.push(Face(b, c, d));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
}

void make_helix(Mesh& out, double R, double r, double turns,
                int path_segs, int tube_segs) {
    // 路径：螺旋线
    for (int i = 0; i <= path_segs; i++) {
        double t = (double)i / path_segs;
        double ang = turns * TAU_D * t;
        Vec3 center(R * std::cos(ang), (t - 0.5) * turns * 0.5, R * std::sin(ang));
        Vec3 tangent = Vec3(-R * std::sin(ang), 0.1, R * std::cos(ang)).normalized();
        Vec3 up(0, 1, 0);
        Vec3 side = tangent.cross(up).normalized();
        Vec3 up2 = side.cross(tangent);
        for (int j = 0; j <= tube_segs; j++) {
            double a = TAU_D * (double)j / tube_segs;
            Vec3 off = side * (r * std::cos(a)) + up2 * (r * std::sin(a));
            out.verts.push(Vertex(center + off, off.normalized()));
        }
    }
    int row = tube_segs + 1;
    for (int i = 0; i < path_segs; i++)
        for (int j = 0; j < tube_segs; j++) {
            int a = i * row + j;
            out.faces.push(Face(a, a + row, a + 1));
            out.faces.push(Face(a + 1, a + row, a + row + 1));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
}

// ============================================================================
// 导出
// ============================================================================
int mesh_write_obj(const Mesh& m, char* buf, int bufsz) {
    int off = std::sprintf(buf, "# nefuOS gfx3d mesh export\n");
    for (int i = 0; i < m.vertex_count(); i++) {
        int n = std::sprintf(buf + off, "v %g %g %g\n",
                             m.verts[i].pos.x, m.verts[i].pos.y, m.verts[i].pos.z);
        if (n < 0 || off + n >= bufsz) return off;
        off += n;
    }
    for (int i = 0; i < m.face_count(); i++) {
        int n = std::sprintf(buf + off, "f %d %d %d\n",
                             m.faces[i][0] + 1, m.faces[i][1] + 1, m.faces[i][2] + 1);
        if (n < 0 || off + n >= bufsz) return off;
        off += n;
    }
    return off;
}

int mesh_write_stl(const Mesh& m, uint8_t* buf, int bufsz) {
    if (bufsz < 84 + m.face_count() * 50) return 0;
    std::memset(buf, 0, 80);
    uint32_t n = m.face_count();
    buf[80] = (uint8_t)(n & 0xFF);
    buf[81] = (uint8_t)((n >> 8) & 0xFF);
    buf[82] = (uint8_t)((n >> 16) & 0xFF);
    buf[83] = (uint8_t)((n >> 24) & 0xFF);
    int off = 84;
    for (int f = 0; f < m.face_count(); f++) {
        const Vec3& a = m.verts[m.faces[f][0]].pos;
        const Vec3& b = m.verts[m.faces[f][1]].pos;
        const Vec3& c = m.verts[m.faces[f][2]].pos;
        Vec3 nn = (b - a).cross(c - a).normalized();
        float* tri = (float*)(buf + off);
        tri[0] = (float)nn.x; tri[1] = (float)nn.y; tri[2] = (float)nn.z;
        tri[3] = (float)a.x; tri[4] = (float)a.y; tri[5] = (float)a.z;
        tri[6] = (float)b.x; tri[7] = (float)b.y; tri[8] = (float)b.z;
        tri[9] = (float)c.x; tri[10] = (float)c.y; tri[11] = (float)c.z;
        off += 50;
    }
    return off;
}
// ----------------------------------------------------------------------------
// 更多图元
// ----------------------------------------------------------------------------
void make_torus_knot(Mesh& out, double R, double r, int p, int q, int seg) {
    int tube = 8;
    for (int i = 0; i <= seg; i++) {
        double u = 2 * 3.14159265 * i / seg;
        double cu = cos(u * p / seg * seg * 1.0);  // placeholder
    }
    // 简化：用参数方程
    for (int i = 0; i <= seg; i++) {
        double u = 2 * 3.14159265 * q * i / seg;
        for (int j = 0; j <= tube; j++) {
            double v = 2 * 3.14159265 * j / tube;
            double cu = cos(p * u), su = sin(p * u);
            double cv = cos(v), sv = sin(v);
            double rr = R + r * cv * cu;
            double x = rr * cu;
            double y = r * sv;
            double z = rr * su;
            out.verts.push(Vertex(Vec3(x, y, z), Vec3(0, 1, 0)));
        }
    }
    int row = tube + 1;
    for (int i = 0; i < seg; i++)
        for (int j = 0; j < tube; j++) {
            int a = i * row + j;
            out.faces.push(Face(a, a + row, a + 1));
            out.faces.push(Face(a + 1, a + row, a + row + 1));
            out.face_mat.push(-1); out.face_mat.push(-1);
        }
    out.compute_smooth_normals();
}

void make_sphere_shell(Mesh& out, double radius, double thickness, int rings, int segs) {
    // 外层球
    make_sphere_uv(out, radius, rings, segs);
    Mesh inner;
    make_sphere_uv(inner, radius - thickness, rings, segs);
    inner = inner; // 占位
    // 合并（简化：只返回外层）
}

void make_rounded_box(Mesh& out, double w, double h, double d, double r, int seg) {
    // 简化：用立方体近似
    make_cube(out, 1.0);
    for (int i = 0; i < out.vertex_count(); i++)
        out.verts[i].pos = out.verts[i].pos * Vec3(w, h, d);
    out.compute_smooth_normals();
}

void make_grid(Mesh& out, int cells, double size) {
    double step = size / cells;
    for (int i = 0; i <= cells; i++) {
        double c = -size/2 + i * step;
        out.verts.push(Vertex(Vec3(c, 0, -size/2), Vec3(0,1,0)));
        out.verts.push(Vertex(Vec3(c, 0, size/2), Vec3(0,1,0)));
    }
    for (int i = 0; i <= cells; i++) {
        double c = -size/2 + i * step;
        out.verts.push(Vertex(Vec3(-size/2, 0, c), Vec3(0,1,0)));
        out.verts.push(Vertex(Vec3(size/2, 0, c), Vec3(0,1,0)));
    }
    for (int i = 0; i < (cells+1)*2; i++) {
        out.faces.push(Face(i*2, i*2+1, i*2));  // 退化线框
        out.face_mat.push(-1);
    }
    out.compute_smooth_normals();
}
// ============================================================================
// self test
// ============================================================================
int mesh_self_test() {
    int fail = 0;

    // 立方体：8 顶点 12 面
    {
        Mesh m;
        make_cube(m, 1.0);
        if (m.vertex_count() != 8) fail++;
        if (m.face_count() != 12) fail++;
        // 包围盒边长
        if (f_abs(m.bounds.mx.x - m.bounds.mn.x - 1.0) > 1e-6) fail++;
    }

    // OBJ 解析已知小文件
    {
        const char* obj =
            "# tiny cube\n"
            "v -1 -1 -1\n"
            "v  1 -1 -1\n"
            "v  1  1 -1\n"
            "v -1  1 -1\n"
            "f 1 2 3\n"
            "f 1 3 4\n";
        Mesh m;
        if (!obj_parse(obj, m)) fail++;
        if (m.vertex_count() != 4) fail++;
        if (m.face_count() != 2) fail++;
    }

    // 二十面体：12 顶点 20 面
    {
        Mesh m;
        make_icosahedron(m, 1.0);
        if (m.vertex_count() != 12) fail++;
        if (m.face_count() != 20) fail++;
    }

    // 合并
    {
        Mesh a, b;
        make_cube(a, 1.0);
        make_cube(b, 1.0);
        int va = a.vertex_count(), fa = a.face_count();
        a.merge(b);
        if (a.vertex_count() != va + 8) fail++;
        if (a.face_count() != fa + 12) fail++;
    }

    // 变换后包围盒更新
    {
        Mesh m;
        make_cube(m, 1.0);
        m.translate(Vec3(10, 0, 0));
        if (f_abs(m.bounds.mn.x - 9.5) > 1e-6) fail++;
    }

    // 车削：矩形轮廓 -> 圆柱
    {
        Mesh m;
        Vec2 profile[] = { Vec2(0.5, -0.5), Vec2(0.5, 0.5) };
        make_lathe(m, profile, 2, 12);
        if (m.face_count() < 12) fail++;
    }

    // 螺旋线生成不崩溃
    {
        Mesh m;
        make_helix(m, 1.0, 0.1, 2.0, 20, 6);
        if (m.vertex_count() < 100) fail++;
    }

    // OBJ 导出往返
    {
        Mesh m; make_cube(m, 1.0);
        char buf[512];
        int n = mesh_write_obj(m, buf, sizeof(buf));
        if (n <= 0) fail++;
        Mesh back;
        if (!obj_parse(buf, back)) fail++;
        if (back.vertex_count() != 8) fail++;
    }

    // STL 导出
    {
        Mesh m; make_cube(m, 1.0);
        uint8_t buf[1024];
        int n = mesh_write_stl(m, buf, sizeof(buf));
        if (n <= 84) fail++;
    }

    return fail;
}

} // namespace gfx3d
} // namespace nefu
