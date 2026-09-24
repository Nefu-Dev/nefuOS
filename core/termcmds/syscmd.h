// =============================================================================
//  syscmd.h —— 系统命令: ps/uptime/free/hostname/uname/whoami/date/cal/clear/echo
// -----------------------------------------------------------------------------
//  date/cal 依赖真实的历法算法(epoch→年月日、星期计算),而非硬编码字符串,
//  因此可以 self_test 校验已知日期。
// =============================================================================
#pragma once

#include "termcmds_all.h"

namespace nefu {
namespace termcmds {

// ---- 历法纯算法(可测) ----
// 把 Unix 时间戳(秒)拆成 UTC 年月日时分秒。
struct DateTime {
    int year, month, day;
    int hour, minute, second;
    int dow;   // 0=周日 .. 6=周六
};
void epoch_to_datetime(uint32_t epoch, DateTime* out);
// 某年是否闰年
bool is_leap(int year);
// 某年某月有几天(month 1..12)
int  days_in_month(int year, int month);
// 计算某天是星期几(已知年月日, Tomohiko Sakamoto 算法)
int  day_of_week(int year, int month, int day);

int syscmd_self_test();

} // namespace termcmds
} // namespace nefu
