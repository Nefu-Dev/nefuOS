// nefuOS 系统工具扩展库 —— 聚合头
// 一次性引入全部系统工具模块，并提供统一的汇总自检入口。
//
// 模块一览：
//   process    进程表 / 创建 / 终止 / 调度 / 优先级 / IPC
//   sysinfo    CPU / 内存 / 磁盘 / 运行时间 / 环境变量
//   eventlog   分级日志 / 查询过滤 / 环形轮转 / 持久化
//   scheduler  一次性 / 周期 / cron 表达式任务调度
//   permissions 用户/组/权限位/ACL/sudo(模拟)
//   services   服务注册/启停/依赖/看门狗
//   config     INI/JSON 配置读写/校验/热重载
#pragma once

#include "process.h"
#include "sysinfo.h"
#include "eventlog.h"
#include "scheduler.h"
#include "permissions.h"
#include "services.h"
#include "config.h"

namespace nefu {
namespace sysutil {

// 运行所有子模块自检，返回总失败条数（0 = 全部通过）。
inline int sysutil_self_test() {
    int f = 0;
    f += g_procman.self_test();
    f += sysinfo_self_test();
    f += g_syslog.self_test();
    f += g_scheduler.self_test();
    f += g_perms.self_test();
    f += g_svcmgr.self_test();
    f += g_config.self_test();
    return f;
}

} // namespace sysutil
} // namespace nefu

// 聚合头结束。使用方只需 #include <sysutil/sysutil_all.h> 即可获得全部模块。
// nefu::sysutil 扩展库 v0.1.0
