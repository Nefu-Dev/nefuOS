#include <cstdio>
#include "simlib/sim_all.h"

int main() {
    int f = nefu::simx::Life::self_test();
    printf("life=%d\n", f);
    f = nefu::simx::EventSim::self_test();
    printf("queue=%d\n", f);
    f = nefu::simx::World2D::self_test();
    printf("world=%d\n", f);
    f = nefu::simx::Boids::self_test();
    printf("boids=%d\n", f);
    f = nefu::simx::Epidemic::self_test();
    printf("epidemic=%d\n", f);
    f = nefu::simx::TrafficFlow::self_test();
    printf("traffic=%d\n", f);
    f = nefu::simx::PerlinNoise::self_test();
    printf("perlin=%d\n", f);
    f = nefu::simx::LangtonAnt::self_test();
    printf("langton=%d\n", f);
    return 0;
}
