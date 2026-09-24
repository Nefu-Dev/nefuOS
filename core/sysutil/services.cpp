// nefuOS 系统工具扩展库 —— 服务管理模块实现
#include "services.h"
#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

ServiceManager g_svcmgr;

namespace {
void copy_str(char* dst, int dstsz, const char* src) {
    if (!dst || dstsz <= 0) return;
    if (!src) src = "";
    int i = 0;
    for (; i < dstsz - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}
} // namespace

ServiceManager::ServiceManager() { reset(); }

void ServiceManager::reset() {
    svcs_.clear();
}

int ServiceManager::index_of(const char* name) const {
    if (!name) return -1;
    for (int i = 0; i < svcs_.size(); i++)
        if (strcmp(svcs_[i].name, name) == 0) return i;
    return -1;
}

const Service* ServiceManager::find(const char* name) const {
    int i = index_of(name);
    return i >= 0 ? &svcs_[i] : 0;
}

Service* ServiceManager::find_mut(const char* name) {
    int i = index_of(name);
    return i >= 0 ? &svcs_[i] : 0;
}

bool ServiceManager::exists(const char* name) const {
    return index_of(name) >= 0;
}

int ServiceManager::register_service(const char* name, const char* const* deps,
                                     bool auto_restart) {
    if (!name) return -1;
    int existing = index_of(name);
    if (existing >= 0) return existing;
    Service s;
    copy_str(s.name, SVC_NAME_MAX, name);
    s.state = SVC_STOPPED;
    s.pid = 0;
    s.enabled = true;
    s.auto_restart = auto_restart;
    s.restart_count = 0;
    s.last_ping_ms = 0;
    s.watchdog_ms = 0;
    s.dep_count = 0;
    for (int i = 0; i < SVC_DEP_MAX; i++) s.deps[i][0] = 0;
    if (deps) {
        for (int i = 0; deps[i] && s.dep_count < SVC_DEP_MAX; i++) {
            copy_str(s.deps[s.dep_count], SVC_NAME_MAX, deps[i]);
            s.dep_count++;
        }
    }
    svcs_.push(s);
    return svcs_.size() - 1;
}

bool ServiceManager::set_enabled(const char* name, bool on) {
    Service* s = find_mut(name);
    if (!s) return false;
    s->enabled = on;
    return true;
}

bool ServiceManager::set_watchdog(const char* name, uint32_t timeout_ms) {
    Service* s = find_mut(name);
    if (!s) return false;
    s->watchdog_ms = timeout_ms;
    return true;
}

bool ServiceManager::start_one(Service* s, uint32_t now_ms) {
    if (!s) return false;
    // 先启动依赖
    for (int i = 0; i < s->dep_count; i++) {
        Service* d = find_mut(s->deps[i]);
        if (!d) return false;   // 依赖未注册
        if (d->state != SVC_RUNNING && d->state != SVC_STARTING) {
            if (!start_one(d, now_ms)) return false;
        }
    }
    s->state = SVC_RUNNING;
    s->last_ping_ms = now_ms;
    s->pid = s->pid ? s->pid : (index_of(s->name) + 1);
    return true;
}

bool ServiceManager::start(const char* name) {
    Service* s = find_mut(name);
    if (!s) return false;
    if (s->state == SVC_RUNNING) return true;
    s->state = SVC_STARTING;
    bool ok = start_one(s, 0);
    if (!ok) s->state = SVC_FAILED;
    return ok;
}

bool ServiceManager::stop(const char* name) {
    Service* s = find_mut(name);
    if (!s) return false;
    // 先停依赖（这里简化：直接把自己置 STOPPED；真实系统会反向拓扑排序）
    s->state = SVC_STOPPING;
    s->state = SVC_STOPPED;
    return true;
}

bool ServiceManager::ping(const char* name) {
    Service* s = find_mut(name);
    if (!s) return false;
    if (s->state != SVC_RUNNING) return false;
    // last_ping 由 watchdog_tick 传入的 now 更新；这里只标记收到心跳
    s->last_ping_ms = 0xFFFFFFFFu;   // 哨兵：在 watchdog_tick 里清除
    return true;
}

int ServiceManager::watchdog_tick(uint32_t now_ms) {
    int restarted = 0;
    for (int i = 0; i < svcs_.size(); i++) {
        Service& s = svcs_[i];
        if (s.state != SVC_RUNNING) continue;
        if (s.watchdog_ms == 0) continue;
        // ping() 把 last_ping_ms 置为哨兵；在 tick 里恢复成 now_ms
        if (s.last_ping_ms == 0xFFFFFFFFu) s.last_ping_ms = now_ms;
        if (now_ms - s.last_ping_ms > s.watchdog_ms) {
            s.state = SVC_FAILED;
            if (s.auto_restart) {
                s.restart_count++;
                s.state = SVC_RUNNING;
                s.last_ping_ms = now_ms;
                restarted++;
            }
        }
    }
    return restarted;
}

// 拓扑排序（Kahn 算法）+ 环检测
int ServiceManager::start_order(char (*out_names)[SVC_NAME_MAX], int max) const {
    int n = svcs_.size();
    if (n > max) n = max;
    // indegree[i] = 该服务依赖的、尚未出队的服务数
    int indeg[16];
    for (int i = 0; i < n; i++) indeg[i] = 0;
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < svcs_[i].dep_count; d++) {
            int dep = index_of(svcs_[i].deps[d]);
            if (dep >= 0 && dep < n) indeg[i]++;
        }
    }
    int order[16];
    int placed = 0;
    bool done[16];
    for (int i = 0; i < 16; i++) done[i] = false;
    while (placed < n) {
        bool progressed = false;
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            if (indeg[i] == 0) {
                order[placed++] = i;
                done[i] = true;
                progressed = true;
                // 把依赖它的服务的 indegree 减一
                for (int j = 0; j < n; j++) {
                    if (done[j]) continue;
                    for (int d = 0; d < svcs_[j].dep_count; d++) {
                        if (strcmp(svcs_[j].deps[d], svcs_[i].name) == 0) indeg[j]--;
                    }
                }
            }
        }
        if (!progressed) return -1;   // 有环
    }
    for (int i = 0; i < placed; i++) {
        const char* nm = svcs_[order[i]].name;
        int j = 0;
        for (; nm[j] && j < SVC_NAME_MAX - 1; j++) out_names[i][j] = nm[j];
        out_names[i][j] = 0;
    }
    return placed;
}

bool ServiceManager::set_restart_policy(const char* name, RestartPolicy p) {
    Service* s = find_mut(name);
    if (!s) return false;
    s->restart_pol = (int)p;
    return true;
}

int ServiceManager::list_by_state(ServiceState st, char (*out)[SVC_NAME_MAX], int max) const {
    int n = 0;
    for (int i = 0; i < svcs_.size() && n < max; i++) {
        if (svcs_[i].state == st) {
            int j = 0;
            const char* nm = svcs_[i].name;
            for (; nm[j] && j < SVC_NAME_MAX - 1; j++) out[n][j] = nm[j];
            out[n][j] = 0;
            n++;
        }
    }
    return n;
}
const char* ServiceManager::name_of_pid(int pid) const {
    for (int i = 0; i < svcs_.size(); i++)
        if (svcs_[i].pid == pid && pid != 0) return svcs_[i].name;
    return 0;
}
const char* svc_state_name(ServiceState s) {
    switch (s) {
    case SVC_STOPPED:   return "STOPPED";
    case SVC_STARTING:  return "STARTING";
    case SVC_RUNNING:   return "RUNNING";
    case SVC_STOPPING:  return "STOPPING";
    case SVC_FAILED:    return "FAILED";
    }
    return "?";
}
bool ServiceManager::reset_restart_count(const char* name) {
    Service* s = find_mut(name);
    if (!s) return false;
    s->restart_count = 0;
    return true;
}
void ServiceManager::count_by_state(int out[5]) const {
    for (int i = 0; i < 5; i++) out[i] = 0;
    for (int i = 0; i < svcs_.size(); i++) {
        int st = (int)svcs_[i].state;
        if (st >= 0 && st < 5) out[st]++;
    }
}
ServiceManager::RestartPolicy ServiceManager::restart_policy(const char* name) const {
    const Service* s = find(name);
    return s ? (RestartPolicy)s->restart_pol : RESTART_NEVER;
}
int ServiceManager::reverse_deps(const char* name, char (*out)[SVC_NAME_MAX], int max) const {
    int n = 0;
    for (int i = 0; i < svcs_.size() && n < max; i++) {
        for (int d = 0; d < svcs_[i].dep_count; d++) {
            if (strcmp(svcs_[i].deps[d], name) == 0) {
                int j = 0;
                const char* nm = svcs_[i].name;
                for (; nm[j] && j < SVC_NAME_MAX - 1; j++) out[n][j] = nm[j];
                out[n][j] = 0;
                n++;
                break;
            }
        }
    }
    return n;
}
bool ServiceManager::has_cycle() const {
    char buf[16][SVC_NAME_MAX];
    return start_order(buf, 16) < 0;
}
ServiceState ServiceManager::state_of(const char* name) const {
    const Service* s = find(name);
    return s ? s->state : SVC_STOPPED;
}

// ---------------- 自检 ----------------
int ServiceManager::self_test() {
    int fails = 0;
    ServiceManager m;

    // 注册：network <- dhcp；ssh 依赖 network
    const char* net_deps[] = {0};
    m.register_service("network", net_deps, true);
    const char* ssh_deps[] = {"network", 0};
    m.register_service("sshd", ssh_deps, true);
    const char* dns_deps[] = {"network", 0};
    m.register_service("dnsmasq", dns_deps, false);

    if (m.count() != 3) fails++;

    // 启动 sshd：应连带启动 network
    if (!m.start("sshd")) fails++;
    if (m.state_of("network") != SVC_RUNNING) fails++;
    if (m.state_of("sshd") != SVC_RUNNING) fails++;

    // 重复启动幂等
    if (!m.start("sshd")) fails++;

    // 停止 sshd
    if (!m.stop("sshd")) fails++;
    if (m.state_of("sshd") != SVC_STOPPED) fails++;
    // network 仍在跑（这里简化不级联停）
    if (m.state_of("network") != SVC_RUNNING) fails++;

    // 依赖缺失：启动一个未注册的服务
    if (m.start("no_such") ) fails++;

    // 看门狗：network 设 100ms 超时，不 ping，应超时重启
    m.reset();
    const char* d0[] = {0};
    m.register_service("svcA", d0, true);
    m.set_watchdog("svcA", 100);
    m.start("svcA");
    // 0ms 时不超时
    if (m.watchdog_tick(0) != 0) fails++;
    // 50ms 不超时
    if (m.watchdog_tick(50) != 0) fails++;
    // 200ms 超时，auto_restart 拉起 1 次
    int r = m.watchdog_tick(200);
    if (r != 1) fails++;
    const Service* a = nullptr;
    for (int i = 0; i < m.count(); i++) {
        if (strcmp(m.at(i)->name, "svcA") == 0) a = m.at(i);
    }
    if (!a) fails++;
    else {
        if (a->restart_count != 1) fails++;
        if (a->state != SVC_RUNNING) fails++;
    }

    // ping 后不再超时
    m.ping("svcA");
    if (m.watchdog_tick(250) != 0) fails++;

    // 拓扑排序：network -> dnsmasq -> sshd
    ServiceManager m2;
    const char* e0[] = {0};
    const char* nd[] = {"network", 0};
    const char* sd[] = {"sshd", "network", 0};
    m2.register_service("network", e0, false);
    m2.register_service("sshd", nd, false);
    m2.register_service("dnsmasq", nd, false);
    m2.register_service("combo", sd, false);
    char order[8][SVC_NAME_MAX];
    int on = m2.start_order(order, 8);
    if (on != 4) fails++;
    if (strcmp(order[0], "network") != 0) fails++;   // network 必须最先
    if (m2.has_cycle()) fails++;
    // 造一个环：a 依赖 b，b 依赖 a
    ServiceManager m3;
    const char* ab[] = {"b", 0};
    const char* ba[] = {"a", 0};
    m3.register_service("a", ab, false);
    m3.register_service("b", ba, false);
    if (!m3.has_cycle()) fails++;

    // count_by_state
    int cbs[5];
    m2.count_by_state(cbs);
    if (cbs[0] + cbs[1] + cbs[2] + cbs[3] + cbs[4] != 4) fails++;

    // list_by_state：先 start network，再查 RUNNING
    m2.start("network");
    char lbuf[8][SVC_NAME_MAX];
    int lr = m2.list_by_state(SVC_RUNNING, lbuf, 8);
    if (lr < 1) fails++;

    if (nefu::strcmp(svc_state_name(SVC_RUNNING), "RUNNING") != 0) fails++;
    return fails;
}

} // namespace sysutil
} // namespace nefu
