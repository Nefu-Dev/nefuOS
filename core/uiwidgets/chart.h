// nefuOS UI 组件库 —— Chart 图表组件
// 折线图(多系列/图例/坐标轴/网格)、柱状图(分组/堆叠)、饼图(扇区/百分比)、
// 面积图、散点图、雷达图,支持实时数据更新。
#pragma once

#include "widget.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

enum ChartType {
    ChartLine = 0,
    ChartBar,
    ChartPie,
    ChartArea,
    ChartScatter,
    ChartRadar
};

struct ChartPoint {
    double x, y;
};

class Chart : public Widget {
public:
    explicit Chart(Widget* parent = 0);

    void set_type(ChartType t) { type_ = t; invalidate(); }
    ChartType type() const { return type_; }

    // ---- 系列管理 ----
    int  add_series(const char* name, gfxlib::Pixel color);
    int  series_count() const { return names_.size(); }
    void clear_series(int s);

    // 折线/柱状/面积:按索引追加一个 y 值
    void add_value(int s, double v);
    // 散点:追加一个 (x,y) 点
    void add_point(int s, double x, double y);
    int  point_count(int s) const;

    // ---- 坐标轴范围 ----
    void set_y_range(double lo, double hi) { ylo_ = lo; yhi_ = hi; }
    void set_x_range(double lo, double hi) { xlo_ = lo; xhi_ = hi; }
    double y_min() const { return ylo_; }
    double y_max() const { return yhi_; }

    // ---- 数据->像素映射(自检核心) ----
    // 把数据值映射到控件本地坐标的像素点
    int  value_to_px(double v) const;   // y 值 -> 本地 y
    int  index_to_px(int i, int n) const; // 第 i 个点(n 个) -> 本地 x

    // ---- 实时更新 ----
    void push_realtime(double v);   // 把最新值推入第一个系列(滚动)

    // ---- 绘制 ----
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;

private:
    int  plot_left()   const { return 30; }
    int  plot_right()  const { return w - 10; }
    int  plot_top()    const { return 10; }
    int  plot_bottom() const { return h - 24; }
    void draw_axes(gfxlib::Buffer b, int ox, int oy);
    void draw_line_chart(gfxlib::Buffer b, int ox, int oy);
    void draw_bar_chart(gfxlib::Buffer b, int ox, int oy);
    void draw_pie_chart(gfxlib::Buffer b, int ox, int oy);
    void draw_area_chart(gfxlib::Buffer b, int ox, int oy);
    void draw_scatter(gfxlib::Buffer b, int ox, int oy);
    void draw_radar(gfxlib::Buffer b, int ox, int oy);

    ChartType type_;
    List<String> names_;
    List<gfxlib::Pixel> colors_;
    List< List<double> > values_;     // 每系列一组数值(折线/柱/面积)
    List< List<ChartPoint> > points_; // 散点
    double ylo_, yhi_, xlo_, xhi_;
    int    realtime_max_;
};

// ===================== 模块自检 =====================
int chart_self_test();

} // namespace ui
} // namespace nefu
