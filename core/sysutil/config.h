// nefuOS 系统工具扩展库 —— 配置管理模块
// 支持 INI 与一个子集 JSON 的读写、类型化取值、默认值、配置校验、热重载。
// 配置在内部表示为 (section, key, value) 三元组列表；序列化回 INI 文本。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

struct ConfigEntry {
    String section;     // "general" / "network" ...
    String key;
    String value;       // 一律按字符串存，取值时再转类型
};

// 校验规则
struct ConfigRule {
    const char* section;
    const char* key;
    bool  required;     // 必须存在
    char  type;         // 'i' = int, 's' = string, 'b' = bool
    int   min_i;        // int 下限
    int   max_i;        // int 上限
};

class ConfigStore {
public:
    ConfigStore();

    // 解析文本配置。fmt: "ini" 或 "json"。返回解析失败行数（0 = 成功）。
    int parse_ini(const char* text);
    int parse_json(const char* text);

    // 序列化回 INI 文本（追加到 out）。
    void to_ini(String& out) const;
    // 序列化为扁平 JSON 对象（{"section.key":"value",...}）。
    void to_json(String& out) const;
    // 序列化为单行 KEY=VAL 列表（调试用，追加到 out）。
    void dump(String& out) const;
    // 点路径取值："network.port" 自动拆 section=network key=port。
    int  get_int_dotted(const char* path, int dflt) const;
    const char* get_str_dotted(const char* path, const char* dflt) const;
    // 类型化强转：尝试把字符串解析成整数（支持 0x 十六进制）。
    static int parse_int(const char* v, int dflt);
    // 静态：字符串转布尔。
    static bool parse_bool(const char* v, bool dflt);

    // 取值（带默认值）
    int         get_int(const char* section, const char* key, int dflt) const;
    const char* get_str(const char* section, const char* key, const char* dflt) const;
    bool        get_bool(const char* section, const char* key, bool dflt) const;

    // 设值（不存在则新增）
    void set_int(const char* section, const char* key, int v);
    void set_str(const char* section, const char* key, const char* v);
    void set_bool(const char* section, const char* key, bool v);

    // 校验：按规则表检查，返回第一条失败的规则下标（-1 = 全部通过）。
    int validate(const ConfigRule* rules, int count) const;

    // 热重载：version 自增表示配置变了；调用方对比上次 version 决定是否重读。
    int  version() const { return version_; }
    void bump() { version_++; }
    // 清空
    void clear();

    int count() const { return entries_.size(); }
    const ConfigEntry* at(int i) const { return &entries_[i]; }
    // 导出所有不重复的段名。
    int  sections(const char** out, int max) const;
    // 导出某段下所有键名。
    int  keys_of(const char* section, const char** out, int max) const;
    // 删除某段下所有键。
    void clear_section(const char* section);
    // get_or_set：存在则返回现值，不存在则写入默认值并返回。
    const char* get_or_set(const char* section, const char* key, const char* dflt);
    // 是否存在某键。
    bool has_key(const char* section, const char* key) const;
    // 不重复段数。
    int  section_count() const;
    // 删除某键（不存在返回 false）。
    bool remove_key(const char* section, const char* key);
    // 重命名段：把 section 下所有条目的 section 改成 newname。
    bool rename_section(const char* section, const char* newname);

    int self_test();

private:
    ConfigEntry* find_mut(const char* section, const char* key);
    const ConfigEntry* find(const char* section, const char* key) const;
    List<ConfigEntry> entries_;
    int version_;
};

extern ConfigStore g_config;

} // namespace sysutil
} // namespace nefu
