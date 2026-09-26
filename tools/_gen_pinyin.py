# -*- coding: utf-8 -*-
# Generate core/ime/pinyin.cpp with the built-in common-word dictionary
import io

# (pinyin, hanzi) common-word dictionary, roughly by usage frequency.
D = [
("a","啊阿"),("ai","爱哀挨矮哎唉癌碍艾"),("an","安按案暗岸俺氨"),
("ang","昂"),("ao","奥澳傲熬凹袄"),
("ba","吧把八巴爸拔罢霸坝"),("bai","白百败摆拜柏"),("ban","半办班板版般伴搬斑扮"),
("bang","帮邦榜棒绑膀磅"),("bao","报保包宝抱暴薄饱爆胞"),("bei","被北备背杯悲辈贝倍碑"),
("ben","本奔笨苯"),("beng","蹦崩绷"),("bi","比必笔币避闭壁毕臂逼碧鼻"),
("bian","变边便遍编辨辩鞭扁"),("biao","表标彪镖"),("bie","别憋"),
("bin","宾滨彬斌"),("bing","并病冰兵饼丙柄"),("bo","波播伯博泊薄驳脖"),
("bu","不步部布补捕堡"),
("ca","擦"),("cai","才菜采彩财猜裁"),("can","参残餐惨灿"),
("cang","藏仓苍舱"),("cao","草操曹槽"),("ce","测侧策册厕"),
("ceng","层曾"),("cha","查差茶察叉插刹"),("chai","拆柴"),
("chan","产缠颤铲馋"),("chang","长常场厂唱尝昌敞"),("chao","超朝潮吵抄炒巢"),
("che","车彻撤扯"),("chen","陈沉晨尘臣衬"),("cheng","成城程称诚承乘盛橙"),
("chi","吃持迟尺池赤翅斥"),("chong","冲充虫崇"),("chou","抽愁臭仇丑绸"),
("chu","出处初除楚触厨锄"),("chuan","传船穿川串喘"),("chuang","床创窗闯"),
("chui","吹垂锤"),("chun","春纯唇"),("ci","次此词磁慈辞刺"),
("cong","从聪丛葱"),("cou","凑"),("cu","粗促醋"),
("cuan","窜"),("cui","催脆翠"),("cun","存村寸"),
("cuo","错搓挫"),
("da","大达答打搭"),("dai","带代待戴袋呆贷"),("dan","但单担蛋淡弹旦胆"),
("dang","当党档荡"),("dao","到道倒岛导刀盗稻"),("de","的得德"),
("deng","等登灯邓"),("di","地第底低帝弟滴敌抵"),("dian","点店电典垫殿颠"),
("diao","掉调钓雕"),("die","爹跌叠"),("ding","定丁顶订钉"),
("dong","东动懂洞冬董冻"),("dou","都斗豆抖逗"),("du","读度独毒堵肚杜渡"),
("duan","段断短端"),("dui","对队堆兑"),("dun","顿蹲盾吨"),
("duo","多夺朵躲惰"),
("e","饿额恶鹅"),("en","恩"),("er","而二儿尔耳"),
("fa","发法罚乏伐"),("fan","反饭犯翻凡烦繁范"),("fang","方放房防访仿"),
("fei","非飞费废肥菲肺"),("fen","分份粉奋愤纷焚"),("feng","风封峰丰疯逢缝"),
("fo","佛"),("fou","否"),("fu","付服福副复夫负富附府父妇符"),
("ga","嘎"),("gai","该改盖概钙"),("gan","干敢感赶甘肝杆"),
("gang","刚钢港岗杠"),("gao","高告搞稿糕"),("ge","个各哥歌格革隔"),
("gei","给"),("gen","跟根"),("geng","更耕"),
("gong","工公共供功宫攻贡"),("gou","够狗沟购构勾"),("gu","古故顾股骨谷姑鼓"),
("gua","挂瓜刮"),("guai","怪拐"),("guan","关管观官馆贯冠"),
("guang","光广逛"),("gui","归贵鬼跪柜规"),("gun","滚棍"),
("guo","过国果锅裹"),
("ha","哈"),("hai","还海孩害嗨"),("han","汉含喊寒汗韩旱"),
("hang","行航杭"),("hao","好号毫豪耗浩"),("he","和何喝河合荷贺盒"),
("hei","黑嘿"),("hen","很恨狠"),("heng","横恒"),
("hong","红洪宏轰哄"),("hou","后候厚侯猴"),("hu","户呼胡湖虎互护忽壶"),
("hua","话化花画华划滑"),("huai","坏怀淮"),("huan","换还欢环缓患幻"),
("huang","黄皇慌晃荒"),("hui","回会灰挥辉悔毁惠"),("hun","混婚魂"),
("huo","或火活获伙货祸"),
("ji","几机级记即己急技计集际及极击剂济季纪"),("jia","家加价假架甲佳驾"),
("jian","见间件建简检减坚监键荐"),("jiang","将讲江奖降疆"),("jiao","叫教交角脚觉较焦胶"),
("jie","接节街姐界解结阶介借"),("jin","今进金近紧仅尽禁津"),("jing","经京精景警静竟镜净"),
("jiong","窘"),("jiu","就九久酒救旧究"),("ju","局据举句巨具居剧聚"),
("juan","卷捐"),("jue","决绝觉角掘"),("jun","军君均菌俊"),
("ka","卡咖"),("kai","开凯慨"),("kan","看砍刊堪"),
("kang","康抗扛炕"),("kao","考靠烤"),("ke","可课克科刻客颗渴"),
("ken","肯垦恳"),("keng","坑"),("kong","空恐孔控"),
("kou","口扣寇"),("ku","苦库哭裤酷"),("kua","跨夸"),
("kuai","快块筷"),("kuan","宽款"),("kuang","况矿框狂"),
("kui","亏愧"),("kun","困昆"),("kuo","括扩阔"),
("la","拉啦辣蜡"),("lai","来赖"),("lan","蓝兰烂拦览懒"),
("lang","浪狼朗"),("lao","老劳牢姥"),("le","了乐勒"),
("lei","累泪类雷垒"),("leng","冷愣"),("li","里力立离理利例李粒历礼丽厉励"),
("lia","俩"),("lian","连联脸练恋廉链"),("liang","两量亮凉粮梁"),
("liao","了料聊疗辽"),("lie","列烈裂猎"),("lin","林临邻淋"),
("ling","另领令灵零铃"),("liu","六流留刘柳"),("long","龙笼隆"),
("lou","楼漏"),("lu","路录陆鲁露炉卢"),("luan","乱"),
("lun","论轮伦"),("luo","落罗络裸"),("lv","绿旅律虑铝"),
("lve","略"),
("ma","吗妈马嘛麻骂码"),("mai","买卖迈麦埋"),("man","满慢曼漫"),
("mang","忙盲茫"),("mao","毛猫冒帽矛"),("me","么"),
("mei","没美每妹眉梅媒"),("men","们门闷"),("meng","梦猛蒙"),
("mi","米密迷谜蜜眯"),("mian","面免棉绵"),("miao","秒苗妙描"),
("mie","灭"),("min","民敏"),("ming","名明命鸣"),
("miu","谬"),("mo","没摸莫默模磨末"),("mou","某谋"),
("mu","目木母幕慕牧"),
("na","那拿哪纳"),("nai","奶耐乃"),("nan","难南男"),
("nang","囊"),("nao","脑闹"),("ne","呢"),
("nei","内"),("nen","嫩"),("neng","能"),
("ni","你尼泥拟逆"),("nian","年念"),("niang","娘"),
("niao","鸟"),("nie","捏"),("nin","您"),
("ning","宁"),("niu","牛扭"),("nong","弄农浓"),
("nu","努怒"),("nuan","暖"),("nuo","诺"),
("nv","女"),("nve","虐"),
("o","哦"),("ou","偶欧"),
("pa","怕爬帕"),("pai","排派拍牌"),("pan","盘盼判"),
("pang","旁胖"),("pao","跑炮泡抛"),("pei","陪配赔佩"),
("pen","喷盆"),("peng","朋碰棚蓬"),("pi","皮批披脾疲匹"),
("pian","片篇偏骗"),("piao","票漂飘"),("pie","撇"),
("pin","品贫"),("ping","平瓶凭评苹"),("po","破坡婆迫"),
("pou","剖"),("pu","普扑铺谱"),
("qi","其起气期七齐器奇汽棋妻"),("qia","恰"),("qian","前钱千签牵浅欠"),
("qiang","强枪墙抢"),("qiao","桥瞧巧敲"),("qie","切且"),
("qin","亲琴勤"),("qing","请情清轻青庆晴"),("qiong","穷"),
("qiu","求球秋"),("qu","去取区趣曲"),("quan","全权圈劝泉"),
("que","却确缺雀"),("qun","群"),
("ran","然染燃"),("rang","让嚷"),("rao","绕饶"),
("re","热"),("ren","人任认忍仁"),("reng","仍"),
("ri","日"),("rong","容荣融"),("rou","肉柔"),
("ru","如入乳"),("ruan","软"),("rui","瑞"),
("run","润"),("ruo","若"),
("sa","洒"),("sai","赛塞"),("san","三散伞"),
("sang","丧"),("sao","扫嫂"),("se","色"),
("sen","森"),("sha","沙杀纱"),("shan","山闪善衫"),
("shang","上伤商赏尚"),("shao","少烧绍"),("she","社设射蛇舍"),
("shen","身深什神审伸"),("sheng","生声省胜圣升"),("shi","是时十使事视实世市识式士示石师"),
("shou","手受收首守"),("shu","书数术树熟输束述属"),("shua","刷"),
("shuai","帅摔"),("shuan","拴"),("shuang","双爽"),
("shui","水谁睡"),("shun","顺"),("shuo","说"),
("si","死四思斯司私丝似"),("song","送松宋"),("sou","搜"),
("su","素速苏诉宿"),("suan","算酸"),("sui","岁随虽碎"),
("sun","孙损"),("suo","所缩锁"),
("ta","他她它塔踏"),("tai","太台态抬"),("tan","谈探弹毯"),
("tang","唐堂糖躺"),("tao","讨逃桃掏"),("te","特"),
("teng","疼"),("ti","题体提替踢梯"),("tian","天田填甜"),
("tiao","条跳调"),("tie","铁贴"),("ting","听停厅挺"),
("tong","同通痛统童"),("tou","头偷投透"),("tu","图土突途涂"),
("tuan","团"),("tui","推腿退"),("tun","吞"),
("tuo","脱拖托"),
("wa","挖哇娃瓦"),("wai","外歪"),("wan","完晚万玩湾弯碗"),
("wang","王望往网忘亡"),("wei","为位未围卫微味伟危委"),("wen","问文温闻稳"),
("weng","翁"),("wo","我握窝卧"),("wu","无五物务午武屋误"),
("xi","系希西息喜细洗习戏"),("xia","下夏吓虾"),("xian","现先线县险显献鲜"),
("xiang","想向相响项香乡象"),("xiao","小笑消校晓"),("xie","写些谢鞋协"),
("xin","新心信辛欣"),("xing","行型星兴形醒性"),("xiong","兄胸熊"),
("xiu","修休秀"),("xu","需许续虚须徐"),("xuan","选宣旋"),
("xue","学雪血"),("xun","寻训讯"),
("ya","呀压牙亚雅鸭"),("yan","眼严言烟沿研演盐"),("yang","样养阳扬羊洋"),
("yao","要药摇腰咬"),("ye","也夜业叶野爷"),("yi","一以已意义易衣医依议亿"),
("yin","因音引银印"),("ying","应影英营迎硬"),("yo","哟"),
("yong","用永勇拥"),("you","有又由友右油游"),("yu","于与雨语遇育鱼玉余"),
("yuan","远元原员院圆愿园"),("yue","月越约乐阅"),("yun","云运允"),
("za","杂"),("zai","在再载灾"),("zan","咱暂赞"),
("zang","脏"),("zao","早造遭糟"),("ze","则责"),
("zen","怎"),("zeng","增"),("zha","扎炸眨"),
("zhai","摘宅"),("zhan","站战展占"),("zhang","张章长掌涨"),
("zhao","找照赵招"),("zhe","着这折"),("zhen","真阵镇针震"),
("zheng","正整争政证征"),("zhi","之只知直制指支止纸志"),("zhong","中重众钟终"),
("zhou","周州洲粥"),("zhu","主住注助猪竹筑祝"),("zhua","抓"),
("zhuai","拽"),("zhuan","转专赚"),("zhuang","装状庄撞"),
("zhui","追"),("zhun","准"),("zhuo","桌捉"),
("zi","自子字紫资仔"),("zong","总宗纵"),("zou","走奏"),
("zu","组足族祖"),("zuan","钻"),("zui","最罪嘴"),
("zun","尊"),("zuo","做作坐座左昨"),
]

D.sort(key=lambda x: x[0])

lines = []
lines.append("// nefuOS built-in Pinyin IME - generated data + engine (common-word dictionary)")
lines.append("// Self-contained: works on both the Win32 host and the bare-metal kernel.")
lines.append('#include "pinyin.h"')
lines.append('#include "../gui/gfx.h"')
lines.append('#include "../klib/klib.h"')
lines.append("")
lines.append("namespace nefu {")
lines.append("namespace ime {")
lines.append("")
lines.append("// ---- common-word dictionary (pinyin -> UTF-8 hanzi, by frequency) ----")
lines.append("struct PyEntry { const char* py; const char* chars; };")
lines.append("static const PyEntry ENTRIES[] = {")
for py, chars in D:
    lines.append('    { "%s", "%s" },' % (py, chars))
lines.append("};")
lines.append("#define N_ENTRIES (int)(sizeof(ENTRIES) / sizeof(ENTRIES[0]))")
lines.append("")
lines.append("// ---- IME state ----")
lines.append("static bool s_on = false;")
lines.append("static char s_py[16];          // current pinyin composition (lowercase a-z)")
lines.append("static int s_py_len = 0;")
lines.append("struct Cand { int entry; int chi; };")
lines.append("static Cand s_all[64];          // all matches for the current pinyin")
lines.append("static int s_all_n = 0;")
lines.append("static int s_page = 0;          // candidate page")
lines.append("static int s_sel = 0;           // selected candidate on the page")
lines.append("static Cand s_page_cand[8];     // candidates of the current page")
lines.append("static int s_page_n = 0;")
lines.append("")
lines.append("void init() { s_on = false; s_py_len = 0; s_py[0] = 0; s_all_n = 0; s_page = 0; s_sel = 0; s_page_n = 0; }")
lines.append("bool enabled() { return s_on; }")
lines.append("void set_enabled(bool on) { s_on = on; if (!on) { s_py_len = 0; s_py[0] = 0; s_all_n = 0; s_page_n = 0; } }")
lines.append("void toggle() { set_enabled(!s_on); }")
lines.append("")
lines.append("static void reset_comp() { s_py_len = 0; s_py[0] = 0; s_all_n = 0; s_page = 0; s_sel = 0; s_page_n = 0; }")
lines.append("")
lines.append("static void update_page() {")
lines.append("    s_page_n = 0;")
lines.append("    int start = s_page * 8;")
lines.append("    if (start >= s_all_n && s_all_n > 0) { s_page = 0; start = 0; }")
lines.append("    for (int i = start; i < s_all_n && s_page_n < 8; i++) s_page_cand[s_page_n++] = s_all[i];")
lines.append("    if (s_sel >= s_page_n) s_sel = 0;")
lines.append("}")
lines.append("")
lines.append("static void lookup() {")
lines.append("    s_all_n = 0;")
lines.append("    if (!s_py[0]) { s_page_n = 0; return; }")
lines.append("    int py_len = s_py_len;")
lines.append("    for (int i = 0; i < N_ENTRIES && s_all_n < 64; i++) {")
lines.append("        const char* e = ENTRIES[i].py;")
lines.append("        int el = (int)strlen(e);")
lines.append("        if (el >= py_len && strncmp(e, s_py, (size_t)py_len) == 0) {")
lines.append("            const unsigned char* p = (const unsigned char*)ENTRIES[i].chars;")
lines.append("            int ci = 0;")
lines.append("            while (*p && s_all_n < 64) {")
lines.append("                s_all[s_all_n].entry = i;")
lines.append("                s_all[s_all_n].chi = ci;")
lines.append("                s_all_n++;")
lines.append("                int adv = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0 ? 2 : ((*p & 0xF0) == 0xE0 ? 3 : 4));")
lines.append("                p += adv; ci++;")
lines.append("            }")
lines.append("        }")
lines.append("    }")
lines.append("    s_page = 0;")
lines.append("    s_sel = 0;")
lines.append("    update_page();")
lines.append("}")
lines.append("")
lines.append("static void commit_cand(int idx, char* out, int out_max) {")
lines.append("    out[0] = 0;")
lines.append("    if (idx < 0 || idx >= s_page_n) return;")
lines.append("    Cand c = s_page_cand[idx];")
lines.append("    const unsigned char* p = (const unsigned char*)ENTRIES[c.entry].chars;")
lines.append("    int cnt = 0;")
lines.append("    while (*p) {")
lines.append("        int adv = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0 ? 2 : ((*p & 0xF0) == 0xE0 ? 3 : 4));")
lines.append("        if (cnt == c.chi) {")
lines.append("            int n = adv < out_max - 1 ? adv : out_max - 1;")
lines.append("            memcpy(out, p, (size_t)n);")
lines.append("            out[n] = 0;")
lines.append("            return;")
lines.append("        }")
lines.append("        p += adv; cnt++;")
lines.append("    }")
lines.append("}")
lines.append("")
lines.append("static void commit_raw_pinyin(char* out, int out_max) {")
lines.append("    int n = s_py_len < out_max - 1 ? s_py_len : out_max - 1;")
lines.append("    memcpy(out, s_py, (size_t)n);")
lines.append("    out[n] = 0;")
lines.append("}")
lines.append("")
lines.append("bool handle_key(int keycode, char ascii, uint8_t mods, char* out_utf8, int out_max) {")
lines.append("    out_utf8[0] = 0;")
lines.append("    const bool ctrl = (mods & 0x02) != 0;")
lines.append("    const bool alt  = (mods & 0x04) != 0;")
lines.append("    // Ctrl+Space toggles the IME (always handled, even when off)")
lines.append("    if (keycode == KEY_SPACE && ctrl && !alt) { toggle(); return true; }")
lines.append("    if (!s_on) return false;")
lines.append("    // Ctrl/Alt combos (Ctrl+S, Ctrl+C, ...) always pass through to the app")
lines.append("    if (ctrl || alt) return false;")
lines.append("    // letters: compose pinyin")
lines.append("    if (ascii >= 'a' && ascii <= 'z') {")
lines.append("        if (s_py_len < 15) { s_py[s_py_len++] = ascii; s_py[s_py_len] = 0; }")
lines.append("        lookup();")
lines.append("        return true;")
lines.append("    }")
lines.append("    // digits: pick a candidate (1-9); 0 / no candidates let digits through")
lines.append("    if (ascii >= '1' && ascii <= '9') {")
lines.append("        if (s_page_n > 0 && s_py_len > 0) {")
lines.append("            int idx = ascii - '1';")
lines.append("            if (idx < s_page_n) { commit_cand(idx, out_utf8, out_max); reset_comp(); }")
lines.append("            return true;")
lines.append("        }")
lines.append("        return false;")
lines.append("    }")
lines.append("    if (ascii >= '0' && ascii <= '9') return false;")
lines.append("    // Space: first candidate, or commit the raw pinyin")
lines.append("    if (keycode == KEY_SPACE) {")
lines.append("        if (s_page_n > 0) { commit_cand(0, out_utf8, out_max); reset_comp(); }")
lines.append("        else if (s_py_len > 0) { commit_raw_pinyin(out_utf8, out_max); reset_comp(); }")
lines.append("        return true;")
lines.append("    }")
lines.append("    // Enter: commit the raw pinyin as latin letters")
lines.append("    if (keycode == KEY_ENTER) {")
lines.append("        if (s_py_len > 0) { commit_raw_pinyin(out_utf8, out_max); reset_comp(); return true; }")
lines.append("        return false;")
lines.append("    }")
lines.append("    // Backspace: remove one pinyin letter (falls through when empty)")
lines.append("    if (keycode == KEY_BACKSPACE) {")
lines.append("        if (s_py_len > 0) { s_py[--s_py_len] = 0; lookup(); return true; }")
lines.append("        return false;")
lines.append("    }")
lines.append("    // page flip: '=' / ']' / '.' next, '-' / '[' / ',' previous")
lines.append("    if (ascii == '=' || ascii == ']' || ascii == '.') {")
lines.append("        if ((s_page + 1) * 8 < s_all_n) { s_page++; update_page(); }")
lines.append("        return true;")
lines.append("    }")
lines.append("    if (ascii == '-' || ascii == '[' || ascii == ',') {")
lines.append("        if (s_page > 0) { s_page--; update_page(); }")
lines.append("        return true;")
lines.append("    }")
lines.append("    // ESC cancels the composition")
lines.append("    if (keycode == KEY_ESC) { reset_comp(); return true; }")
lines.append("    // any other key ends the composition and is passed to the app")
lines.append("    reset_comp();")
lines.append("    return false;")
lines.append("}")
lines.append("")
lines.append("void paint(Surface& fb) {")
lines.append("    if (!s_on) return;")
lines.append("    if (s_py_len == 0 && s_all_n == 0) return;")
lines.append("    int W = fb.width, H = fb.height;")
lines.append("    int bar_h = 30;")
lines.append("    int x = 16, y = H - bar_h - 4;")
lines.append("    int bw = W - 32;")
lines.append("    gfx::fillrect(fb, x, y, bw, bar_h, 0xEE22262B);")
lines.append("    gfx::rect(fb, x, y, bw, bar_h, 0xAA55667A);")
lines.append("    char head[32];")
lines.append("    int hl = ksprintf(head, sizeof(head), \"拼: %s\", s_py);")
lines.append("    gfx::text(fb, x + 8, y + 7, head, 0xFF8AB4F8, 0xEE22262B);")
lines.append("    int px = x + 8 + gfx::text_width(head);")
lines.append("    for (int i = 0; i < s_page_n; i++) {")
lines.append("        char item[24];")
lines.append("        Cand c = s_page_cand[i];")
lines.append("        const unsigned char* p = (const unsigned char*)ENTRIES[c.entry].chars;")
lines.append("        int cnt = 0, adv = 1;")
lines.append("        while (*p) {")
lines.append("            adv = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0 ? 2 : ((*p & 0xF0) == 0xE0 ? 3 : 4));")
lines.append("            if (cnt == c.chi) break;")
lines.append("            p += adv; cnt++;")
lines.append("        }")
lines.append("        char ch[8] = {0};")
lines.append("        if (*p) { memcpy(ch, p, (size_t)adv); ch[adv] = 0; }")
lines.append("        ksprintf(item, sizeof(item), \"%d.%s\", i + 1, ch);")
lines.append("        uint32_t fg = (i == s_sel) ? 0xFFFFFFFF : 0xFFD6E2D8;")
lines.append("        uint32_t bg = (i == s_sel) ? 0x883D4B66 : 0xEE22262B;")
lines.append("        int iw = gfx::text_width(item) + 8;")
lines.append("        if (px + iw > W - 24) break;")
lines.append("        gfx::fillrect(fb, px, y + 4, iw, bar_h - 8, bg);")
lines.append("        gfx::text(fb, px + 4, y + 7, item, fg, bg);")
lines.append("        px += iw + 6;")
lines.append("    }")
lines.append("    if ((s_page + 1) * 8 < s_all_n) {")
lines.append("        char tip[16];")
lines.append("        ksprintf(tip, sizeof(tip), \"%d/%d\", s_page + 1, (s_all_n + 7) / 8);")
lines.append("        gfx::text(fb, W - 16 - gfx::text_width(tip), y + 7, tip, 0xFF8AB4F8, 0xEE22262B);")
lines.append("    }")
lines.append("}")
lines.append("")
lines.append("} // namespace ime")
lines.append("} // namespace nefu")
lines.append("")
open(r"D:\mycppos1\nefuos\core\ime\pinyin.cpp", "wb").write("\n".join(lines).encode("utf-8"))
print("pinyin.cpp generated:", len(D), "entries,", len(lines), "lines")
