// nefuOS UI 组件库 —— 聚合头
//   #include "uiwidgets/uiwidgets_all.h"
// 暴露 nefu::ui:全部组件类与各模块自检入口。
#pragma once

#include "widget.h"
#include "controls.h"
#include "tableview.h"
#include "chart.h"
#include "dialog.h"
#include "menu.h"
#include "painter.h"

namespace nefu {
namespace ui {

// 汇总自检:返回所有模块失败数之和(0=全部通过)
inline int uiwidgets_self_test() {
    int f = 0;
    f += widget_self_test();
    f += controls_self_test();
    f += tableview_self_test();
    f += chart_self_test();
    f += dialog_self_test();
    f += menu_self_test();
    f += painter_self_test();
    return f;
}

} // namespace ui
} // namespace nefu
