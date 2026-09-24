// ============================================================================
// nefuOS 音频DSP库 —— 振荡器模块 (osc.h)
// ----------------------------------------------------------------------------
// 提供：
//   * 正弦查表振荡器 (512 点表 + 线性插值)
//   * 方波 / 锯齿波 / 三角波 / 脉冲波 (pulse, 占空比可调)
//   * 噪声源：白噪声 / 粉红噪声 / 棕噪声
//   * FM 算子 (带调制输入的正弦振荡器)
//   * 通用波表 (wavetable) 振荡器
//
// 设计要点：
//   - 所有振荡器以 double 相位累加器工作，相位范围 [0,1)，频率由采样率换算；
//   - 正弦查表使用 512 点定点表预计算，线性插值消除锯齿；
//   - 噪声使用确定性 LCG/简化 Voss-McCartney 算法，便于 self_test 复现；
//   - 所有模块不抛异常，不使用 STL 容器；缓冲区由 new[] 分配。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace audsp {

// ---- 全局采样率（库级默认，可由宿主设置） ----
extern double g_sample_rate;          // 默认 44100.0
void set_sample_rate(double sr);      // 修改全局采样率

// ---- 波形枚举 ----
enum Waveform {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_SAWTOOTH,
    WAVE_TRIANGLE,
    WAVE_PULSE,
    WAVE_NOISE_WHITE,
    WAVE_NOISE_PINK,
    WAVE_NOISE_BROWN,
    WAVE_COUNT
};

const char* waveform_name(int w);     // 返回波形名（用于调试/UI）

// ============================================================================
// 正弦查表
// ============================================================================
// 表长 512，存 sin(2*pi*i/512) 在 [-1,1] 内的 double 值。
// 用线性插值读取任意相位，避免在热路径上调用 sin()。
#define SINE_TABLE_LEN 512

// 初始化正弦表（进程内只需调一次，幂等）
void sine_table_init();
// 查表取 [0,1) 相位处的正弦值，线性插值
double sine_lookup(double phase);

// ============================================================================
// 基础振荡器 Oscillator
// ----------------------------------------------------------------------------
// 一个通用振荡器：可选择波形，set_frequency() 后每周期调用 process() 生成
// 一个采样，输出 [-1,1]。相位自动回绕。
// ============================================================================
struct Oscillator {
    double phase;          // 当前相位 [0,1)
    double freq;           // 频率 Hz
    double duty;           // 脉冲波占空比 [0,1]，默认 0.5
    int    wave;           // Waveform
    double noise_state;    // 噪声内部状态（白噪声 LCG 种子 / 粉红累积）
    double brown_state;    // 棕噪声积分状态
    uint32_t lcg;          // 白噪声 LCG 状态

    void init();                       // 清零所有状态
    void set_frequency(double hz);     // 设置频率（自动按全局采样率换算相位增量）
    void set_waveform(int w);
    void set_duty(double d);           // 仅脉冲波有效
    double process();                  // 生成下一个采样 [-1,1]
    // 批量生成 n 个采样到 out（必须有足够空间）
    void process_block(double* out, int n);
};

// ============================================================================
// FM 算子 FmOperator
// ----------------------------------------------------------------------------
// 一个带相位调制输入的正弦振荡器：
//   phase += 2*pi*(freq + mod_depth * mod_input) / sr
//   output = sin(phase)
// 经典 Yamaha DX7 风格：每个 voice 由多个 FmOperator 级联。
// ============================================================================
struct FmOperator {
    double phase;
    double freq;
    double mod_depth;     // 调制灵敏度（单位：Hz 或相位系数）
    double output_level;  // 输出增益 0..1

    void init();
    void set_frequency(double hz);
    void set_mod_depth(double d);
    void set_output_level(double l);
    // mod_input: 上一级算子的输出 [-1,1]；返回本算子输出
    double process(double mod_input);
    void process_block(const double* mod_in, double* out, int n);
};

// ============================================================================
// 波表振荡器 WavetableOsc
// ----------------------------------------------------------------------------
// 使用用户提供的任意双精度波表（一周期），支持任意表长与循环播放。
// 用于加法合成 / 自定义波形。
// ============================================================================
struct WavetableOsc {
    double* table;         // 波表数据（一周期），长度 table_len
    int     table_len;     // 表长
    double  phase;         // [0,1)
    double  freq;

    WavetableOsc();
    ~WavetableOsc();
    bool load_table(const double* data, int len);  // 拷贝一份表
    void set_frequency(double hz);
    double process();
    void process_block(double* out, int n);
};

// ============================================================================
// 噪声发生器 NoiseGenerator
// ----------------------------------------------------------------------------
// white:  平坦频谱 (LCG 均匀分布映射到 [-1,1])
// pink:   -3 dB/oct (Paul Kellet 简化滤波，一阶递归)
// brown:  -6 dB/oct (对白噪声做积分)
// ============================================================================
struct NoiseGenerator {
    int    type;           // WAVE_NOISE_WHITE / PINK / BROWN
    uint32_t lcg;
    double pink_b0, pink_b1, pink_b2, pink_b3, pink_b4, pink_b5, pink_b6;
    double brown_last;

    void init(int type);
    double process();
    void process_block(double* out, int n);
};

// ============================================================================
// 自检：返回失败条数（0 = 全部通过）
// ============================================================================
int osc_self_test();

} // namespace audsp
} // namespace nefu
