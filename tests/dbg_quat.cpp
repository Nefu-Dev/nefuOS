#include "gfx3d/gfx3d_all.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
namespace nefu { void* kalloc(size_t s){return std::calloc(1,s?s:1);} void kfree(void*){} void* krealloc(void*p,size_t s){return std::realloc(p,s);} void platform_dbg(const char*){} }
using namespace nefu::gfx3d;
int main(){
  Quat q = Quat::from_euler(0.3,0.7,-0.2);
  Vec3 e = quat_to_euler(q);
  std::printf("euler: %g %g %g (orig 0.3 0.7 -0.2)\n", e.x, e.y, e.z);
  Quat q2 = Quat::from_euler(e.x,e.y,e.z);
  Vec3 v(1,2,3);
  Vec3 r1=q*v, r2=q2*v;
  std::printf("r1 %g %g %g\n", r1.x,r1.y,r1.z);
  std::printf("r2 %g %g %g\n", r2.x,r2.y,r2.z);
  std::printf("diff %g\n", (r1-r2).length());
}
