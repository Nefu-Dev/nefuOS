// nefuOS 系统工具扩展库 —— 服务管理模块
// 服务注册 / 启动 / 停止 / 状态 / 依赖管理 / 看门狗。
// 服务是后台守护进程的抽象：这里只维护状态机与依赖图，真正的服务体由
// 上层（init/nefud）在启动时挂接。看门狗用虚拟时钟判定超时。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

enum ServiceState {
    SVC_STOPPED = 0,
    SVC_STARTING,
    SVC_RUNNING,
    SVC_STOPPING,
    SVC_FAILED
};

const int SVC_DEP_MAX = 8;
const int SVC_NAME_MAX = 24;

struct Service {
    char    name[SVC_NAME_MAX];
    ServiceState state;
    int     pid;                 // 关联进程 PID（0 = 未关联）
    bool    enabled;              // 是否开机自启
    bool    auto_restart;        // 崩溃后自动拉起
    int     restart_count;        // 已重启次数
    uint32_t last_ping_ms;        // 最后一次心跳
    uint32_t watchdog_ms;         // 看门狗超时（0 = 不看门狗）
    char    deps[SVC_DEP_MAX][SVC_NAME_MAX];  // 依赖的服务名
    int     dep_count;
    int     restart_pol;        // 0=never 1=on-failure 2=always
};

class ServiceManager {
public:
    ServiceManager();

    // 注册一个服务；deps 为空结尾数组，可传 0。重复注册返回已有 id。
    int  register_service(const char* name, const char* const* deps, bool auto_restart);
    bool set_enabled(const char* name, bool on);
    bool set_watchdog(const char* name, uint32_t timeout_ms);

    // 启动：递归启动依赖，再把本服务置 RUNNING。返回是否成功（依赖缺失则失败）。
    bool start(const char* name);
    // 停止：先停依赖（反向），再停自己。
    bool stop(const char* name);
    // 看门狗心跳：服务运行中定期调用。
    bool ping(const char* name);
    // 推进虚拟时钟，检查所有 RUNNING 服务是否看门狗超时；超时则置 FAILED
    // 并在 auto_restart 时自动拉起。返回这一轮被重启的服务数。
    int  watchdog_tick(uint32_t now_ms);

    ServiceState state_of(const char* name) const;
    int  count() const { return svcs_.size(); }
    const Service* at(int i) const { return &svcs_[i]; }
    bool exists(const char* name) const;
    // 拓扑排序：把所有服务按依赖先后导出到 out_names（最多 max 个）。
    // 检测到循环依赖时返回 -1。
    int  start_order(char (*out_names)[SVC_NAME_MAX], int max) const;
    // 是否存在依赖环。
    bool has_cycle() const;
    // 反向依赖：导出所有直接依赖 name 的服务名到 out（最多 max）。
    int  reverse_deps(const char* name, char (*out)[SVC_NAME_MAX], int max) const;
    // 重启策略
    enum RestartPolicy { RESTART_NEVER = 0, RESTART_ON_FAILURE, RESTART_ALWAYS };
    bool set_restart_policy(const char* name, RestartPolicy p);
    RestartPolicy restart_policy(const char* name) const;
    // 统计各状态服务数：out[0..4] 对应 SVC_STOPPED..SVC_FAILED。
    void count_by_state(int out[5]) const;
    // 导出处于指定状态的所有服务名。
    int  list_by_state(ServiceState st, char (*out)[SVC_NAME_MAX], int max) const;
    // 清零某服务的重启计数。
    bool reset_restart_count(const char* name);
    // 按关联进程 pid 查服务名（找不到返回 0）。
    const char* name_of_pid(int pid) const;
    void reset();
    int self_test();

private:
    Service* find_mut(const char* name);
    const Service* find(const char* name) const;
    bool start_one(Service* s, uint32_t now_ms);
    int  index_of(const char* name) const;

    List<Service> svcs_;
};

// 状态名转换
const char* svc_state_name(ServiceState s);

extern ServiceManager g_svcmgr;

} // namespace sysutil
} // namespace nefu
