#include <cstdio>
#include "audlib/aud_all.h"

int main(int argc, char** argv) {
    using namespace nefu::audx;
    int which = argc > 1 ? atoi(argv[1]) : 0;
    switch (which) {
        case 1: printf("wave=%d\n", WaveIO::self_test()); break;
        case 2: printf("synth=%d\n", Synth::self_test()); break;
        case 3: printf("env=%d\n", Envelope::self_test()); break;
        case 4: printf("biquad=%d\n", BiquadFilter::self_test()); break;
        case 5: printf("lp1=%d\n", LowPass1::self_test()); break;
        case 6: printf("note=%d\n", Note::self_test()); break;
        case 7: printf("seq=%d\n", Sequencer::self_test()); break;
        case 8: printf("mixer=%d\n", Mixer::self_test()); break;
        case 9: printf("delay=%d\n", Delay::self_test()); break;
        case 10: printf("reverb=%d\n", Reverb::self_test()); break;
        case 11: printf("mod=%d\n", Modulator::self_test()); break;
        case 12: printf("pitch=%d\n", Pitch::self_test()); break;
        default: printf("usage: am1 <1..12>\n");
    }
    return 0;
}
