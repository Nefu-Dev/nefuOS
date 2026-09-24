// nefuOS simlib —— 交通流模拟 traffic
// 教学版：一维道路跟车模型（Nagel-Schreckenberg 元胞自动机）。
// 每辆车一个格点，规则：加速 -> 保持 -> 随机减速 -> 制动防撞。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace simx {

// 交通流模拟器（环形道路）
class TrafficFlow {
public:
    // 构造：路长 cells，限速 vmax（格/步）
    TrafficFlow(int cells, int vmax);

    // 布置车辆：在位置 pos 放一辆初始速度 0 的车（pos 取模路长）
    void add_car(int pos);
    // 清空道路
    void clear();

    // 推进一个时间步（NS 规则），返回平均速度
    double step();

    // 统计：车辆数
    int car_count() const;
    // 平均速度（最近一次 step 结果缓存）
    double avg_speed() const { return last_avg; }
    // 道路拥堵率（速度 0 车辆占比）
    double jam_ratio() const;

    // 序列化：'.' 空位 / 数字 车辆速度（0~9）
    std::string to_text() const;

    // ---- self test ----
    static int self_test();

private:
    // 将车流规则应用到单条道上（环形）
    int cells, vmax;
    std::vector<int> v;        // 每格车速，-1 表示空
    double last_avg;
};

} // namespace simx
} // namespace nefu
