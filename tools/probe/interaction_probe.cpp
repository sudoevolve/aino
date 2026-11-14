// 存量控件的交互断言
//
// widget_probe 覆盖的是 0.3.0 之后新增的控件；这个文件补的是更早那批
// —— button / input / textarea / checkbox / switch / slider / tabs /
// segmented / select / radio / stepper / rating / accordion / menu /
// dialog / tooltip / scroll / nav_item。
//
// 全部离屏（32bpp DIB + 内存 DC），直接写 impl->input 驱动。每帧都从
// 一个**空的 Input** 开始重建，避免上一帧的 pressed / text_input 残留
// 造成假通过 —— 真实窗口里这些瞬时状态由消息循环逐帧清空，探针必须自己负责。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; std::printf("  [ ok ] %s\n", what); }
    else    { ++g_fail; std::printf("  [FAIL] %s  %s\n", what, detail.c_str()); }
}

// ---------------------------------------------------------------- 离屏画布
struct Surface {
    HBITMAP bmp = nullptr;
    HDC dc = nullptr;
    void* bits = nullptr;
    aino::detail::Canvas cv;
    int w = 0, h = 0;

    Surface(int W, int H) : w(W), h(H) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        SelectObject(dc, bmp);
        cv.px = (uint8_t*)bits;
        cv.w = W; cv.h = H; cv.stride = W * 4;
        cv.platform_dc = dc;
    }
    ~Surface() {
        if (dc) DeleteDC(dc);
        if (bmp) DeleteObject(bmp);
    }
    aino::Color at(float x, float y) const {
        const int ix = int(x), iy = int(y);
        if (ix < 0 || iy < 0 || ix >= w || iy >= h) return aino::Color(0, 0, 0, 0);
        const uint8_t* q = cv.px + size_t(iy) * size_t(cv.stride) + size_t(ix) * 4u;
        return aino::Color(q[aino::detail::CH_R], q[aino::detail::CH_G],
                           q[aino::detail::CH_B], q[aino::detail::CH_A]);
    }
};

struct Harness {
    aino::Ui ui;
    aino::detail::GdiTextRenderer tr;
    Surface* surf = nullptr;

    explicit Harness(Surface* s) : surf(s) {
        tr.init(1.0f, std::string(), std::string());
        ui.impl->text = &tr;
        ui.impl->canvas = &surf->cv;
        ui.set_theme(aino::Theme::light());
    }
    ~Harness() { tr.release(); }
};

// ---------------------------------------------------------------- 帧驱动
struct Driver {
    aino::Ui& ui;
    float w = 0.0f, h = 0.0f;

    aino::Input& in() { return ui.impl->input; }

    void begin() {
        ui.impl->input = aino::Input{};     // 瞬时状态逐帧重建
        ui.begin_frame(w, h, 1.0f / 60.0f, 1.0f);
    }
    void end() { ui.end_frame(); }

    void idle(const std::function<void()>& draw) {
        begin();
        draw();
        end();
    }

    void move(float x, float y, const std::function<void()>& draw) {
        begin();
        in().mouse = aino::Vec2(x, y);
        draw();
        end();
    }

    void press(float x, float y, const std::function<void()>& draw) {
        begin();
        in().mouse = aino::Vec2(x, y);
        in().down[0] = true;
        in().pressed[0] = true;
        draw();
        end();
    }

    void release(float x, float y, const std::function<void()>& draw) {
        begin();
        in().mouse = aino::Vec2(x, y);
        in().released[0] = true;
        draw();
        end();
    }

    void click(float x, float y, const std::function<void()>& draw) {
        press(x, y, draw);
        release(x, y, draw);
    }

    void right_click(float x, float y, const std::function<void()>& draw) {
        begin();
        in().mouse = aino::Vec2(x, y);
        in().down[1] = true;
        in().pressed[1] = true;
        draw();
        end();
        begin();
        in().mouse = aino::Vec2(x, y);
        in().released[1] = true;
        draw();
        end();
    }

    // 按下 → 保持按下移动 → 抬起
    void drag(float x0, float y0, float x1, float y1, const std::function<void()>& draw) {
        press(x0, y0, draw);
        begin();
        in().mouse = aino::Vec2(x1, y1);
        in().down[0] = true;
        draw();
        end();
        release(x1, y1, draw);
    }

    void type(const std::string& t, const std::function<void()>& draw) {
        begin();
        in().text_input = t;
        draw();
        end();
    }

    void key(int k, const std::function<void()>& draw) {
        begin();
        in().key_pressed[k] = true;
        in().key_down[k] = true;
        draw();
        end();
    }

    void key_ctrl(int k, const std::function<void()>& draw) {
        begin();
        in().key_pressed[k] = true;
        in().key_down[k] = true;
        in().key_ctrl[k] = true;
        in().key_down[aino::KEY_CTRL] = true;
        draw();
        end();
    }

    void wheel(float x, float y, float d, const std::function<void()>& draw) {
        begin();
        in().mouse = aino::Vec2(x, y);
        in().wheel = d;
        draw();
        end();
    }

    // mouse 参数：有些控件（tooltip）依赖指针位置，而 begin() 会把 Input 清零，
    // 所以这类断言必须把指针位置一起带进这一帧。
    int defers(const std::function<void()>& draw, float mx = -1e6f, float my = -1e6f) {
        begin();
        if (mx > -1e5f) in().mouse = aino::Vec2(mx, my);
        draw();
        const int n = int(ui.impl->defer.size());
        end();
        return n;
    }
};

using aino::Rect;

static const char* s(float v) { static char b[32]; std::snprintf(b, sizeof b, "%.3f", double(v)); return b; }
static std::string num(int v) { return std::to_string(v); }

// ---------------------------------------------------------------- 1. 按钮
static void test_button() {
    std::printf("[1] Button\n");
    Surface sf(320, 120);
    Harness h(&sf);
    Driver d{h.ui, 320.0f, 120.0f};
    aino::Ui& ui = h.ui;

    const Rect box(20.0f, 20.0f, 120.0f, 36.0f);
    int hits = 0;
    auto draw = [&]() { if (ui.button("确定", box)) ++hits; };

    d.click(box.cx(), box.cy(), draw);
    check(hits == 1, "点击触发一次", "hits=" + num(hits));

    d.press(box.cx(), box.cy(), draw);
    check(hits == 1, "只按下不抬起不触发", "hits=" + num(hits));
    d.release(box.cx(), box.cy(), draw);
    check(hits == 2, "抬起才算一次", "hits=" + num(hits));

    d.click(box.right() + 60.0f, box.cy(), draw);
    check(hits == 2, "点在框外不触发", "hits=" + num(hits));

    // 按下后移出再松开：这是"取消点击"的标准行为
    d.press(box.cx(), box.cy(), draw);
    d.release(box.right() + 50.0f, box.cy(), draw);
    check(hits == 2, "按下后移出再抬起 → 取消", "hits=" + num(hits));

    aino::Style dis;
    dis.disabled = true;
    int dh = 0;
    auto ddraw = [&]() { if (ui.button("禁用", box, dis)) ++dh; };
    d.click(box.cx(), box.cy(), ddraw);
    check(dh == 0, "disabled 不响应点击", "hits=" + num(dh));
}

// ---------------------------------------------------------------- 2. 图标按钮
static void test_icon_button() {
    std::printf("[2] Icon Button\n");
    Surface sf(320, 120);
    Harness h(&sf);
    Driver d{h.ui, 320.0f, 120.0f};
    aino::Ui& ui = h.ui;

    const Rect box(20.0f, 20.0f, 36.0f, 36.0f);
    int hits = 0;
    auto draw = [&]() { if (ui.icon_button("+", box)) ++hits; };
    d.click(box.cx(), box.cy(), draw);
    check(hits == 1, "点击触发", "hits=" + num(hits));

    // 图标按钮必须以 box 为准，不能按字形宽度缩
    d.click(box.x + 3.0f, box.cy(), draw);
    check(hits == 2, "贴着左边缘也算命中", "hits=" + num(hits));
}

// ---------------------------------------------------------------- 3. 单行输入
static void test_input_edit() {
    std::printf("[3] Input 编辑\n");
    Surface sf(360, 200);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 200.0f};
    aino::Ui& ui = h.ui;

    std::string v;
    const Rect box(20.0f, 20.0f, 220.0f, 36.0f);
    auto draw = [&]() { ui.input(&v, box); };

    d.click(box.cx(), box.cy(), draw);
    d.type("abc", draw);
    check(v == "abc", "点击聚焦后可输入", "v=" + v);

    d.key(aino::KEY_BACKSPACE, draw);
    check(v == "ab", "退格删一个字符", "v=" + v);

    d.key(aino::KEY_HOME, draw);
    d.type("X", draw);
    check(v == "Xab", "Home 后插入到行首", "v=" + v);

    d.key(aino::KEY_END, draw);
    d.type("Y", draw);
    check(v == "XabY", "End 后追加到行尾", "v=" + v);

    d.key(aino::KEY_LEFT, draw);
    d.type("Z", draw);
    check(v == "XabZY", "左移一格后插入", "v=" + v);

    // 光标在最左时退格不能把值弄坏，也不能越界
    d.key(aino::KEY_HOME, draw);
    d.key(aino::KEY_BACKSPACE, draw);
    check(v == "XabZY", "行首退格不改变内容", "v=" + v);

    d.type("中文", draw);
    check(v == "中文XabZY", "多字节 UTF-8 按码点编辑", "v=" + v);

    // 点外部失焦后，键盘输入不应再进入这个框
    d.click(330.0f, 160.0f, draw);
    const std::string before = v;
    d.type("nope", draw);
    check(v == before, "失焦后键入不进入输入框", "v=" + v);

    // Esc 也应失焦
    d.click(box.cx(), box.cy(), draw);
    d.key(aino::KEY_ESC, draw);
    const std::string b2 = v;
    d.type("q", draw);
    check(v == b2, "Esc 失焦", "v=" + v);
}

// ---------------------------------------------------------------- 4. 多行输入
static void test_textarea() {
    std::printf("[4] Textarea\n");
    Surface sf(360, 220);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 220.0f};
    aino::Ui& ui = h.ui;

    std::string v = "ab";
    const Rect box(20.0f, 20.0f, 240.0f, 90.0f);
    auto draw = [&]() { ui.textarea(&v, box); };

    d.click(box.cx(), box.cy(), draw);
    d.key(aino::KEY_END, draw);
    d.key(aino::KEY_ENTER, draw);
    d.type("cd", draw);
    check(v == "ab\ncd", "Enter 插入换行", "v=" + v);

    d.key(aino::KEY_BACKSPACE, draw);
    d.key(aino::KEY_BACKSPACE, draw);
    check(v == "ab\n", "退格到行首后停在换行前", "v=" + v);
    d.key(aino::KEY_BACKSPACE, draw);
    check(v == "ab", "再退格把换行删掉（行合并）", "v=" + v);
}

// ---------------------------------------------------------------- 5. 复选框
static void test_checkbox() {
    std::printf("[5] Checkbox\n");
    Surface sf(360, 200);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 200.0f};
    aino::Ui& ui = h.ui;

    bool v = false;
    const Rect box(20.0f, 20.0f, 200.0f, 24.0f);
    auto draw = [&]() { ui.checkbox(&v, "记住这台设备", box); };

    d.click(box.x + 10.0f, box.cy(), draw);
    check(v, "点方块切换为 true");

    d.click(box.x + 10.0f, box.cy(), draw);
    check(!v, "再点变回 false");

    // 整行可点：点标签文字也要生效
    d.click(box.x + 120.0f, box.cy(), draw);
    check(v, "点标签文字也能切换");

    aino::Style dis;
    dis.disabled = true;
    bool dv = false;
    auto ddraw = [&]() { ui.checkbox(&dv, "禁用", box, dis); };
    d.click(box.x + 10.0f, box.cy(), ddraw);
    check(!dv, "disabled 不响应");
}

// ---------------------------------------------------------------- 6. 开关
static void test_switch() {
    std::printf("[6] Switch\n");
    Surface sf(360, 160);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 160.0f};
    aino::Ui& ui = h.ui;

    bool v = false;
    const Rect box(20.0f, 20.0f, 44.0f, 26.0f);
    auto draw = [&]() { ui.toggle(&v, box); };

    d.click(box.cx(), box.cy(), draw);
    check(v, "点击打开");
    d.click(box.cx(), box.cy(), draw);
    check(!v, "再点关闭");

    // 拖到轨道右端（按下在左端、松开在右端）也应视为点击
    d.press(box.x + 6.0f, box.cy(), draw);
    d.release(box.right() - 4.0f, box.cy(), draw);
    check(v, "按在轨道上、在右端松开 → 切换");
}

// ---------------------------------------------------------------- 7. 滑块
static void test_slider() {
    std::printf("[7] Slider\n");
    Surface sf(400, 160);
    Harness h(&sf);
    Driver d{h.ui, 400.0f, 160.0f};
    aino::Ui& ui = h.ui;

    float v = 0.0f;
    const Rect box(30.0f, 40.0f, 300.0f, 24.0f);
    auto draw = [&]() { ui.slider(&v, 0.0f, 1.0f, box); };

    d.drag(box.x + box.w * 0.5f, box.cy(), box.x + box.w * 0.5f, box.cy(), draw);
    check(v > 0.42f && v < 0.58f, "拖到中点约为 0.5", std::string("v=") + s(v));

    d.drag(box.cx(), box.cy(), box.x - 40.0f, box.cy(), draw);
    check(v <= 0.001f, "拖出左端夹取到 min", std::string("v=") + s(v));

    d.drag(box.cx(), box.cy(), box.right() + 40.0f, box.cy(), draw);
    check(v >= 0.999f, "拖出右端夹取到 max", std::string("v=") + s(v));

    // 必须按在轨道上才开始拖，否则鼠标只是划过就会改值
    v = 0.3f;
    d.drag(box.cx(), box.y - 30.0f, box.right(), box.y - 30.0f, draw);
    check(v > 0.29f && v < 0.31f, "从轨道外起拖不改变值", std::string("v=") + s(v));
}

// ---------------------------------------------------------------- 8. 标签页
static void test_tabs() {
    std::printf("[8] Tabs / Segmented\n");
    Surface sf(400, 200);
    Harness h(&sf);
    Driver d{h.ui, 400.0f, 200.0f};
    aino::Ui& ui = h.ui;

    int idx = 0;
    const std::vector<std::string> items = {"概览", "分析", "设置"};
    const Rect tbox(20.0f, 20.0f, 300.0f, 38.0f);
    auto tdraw = [&]() { ui.tabs(items, &idx, tbox); };

    // tabs 是 AppKit 语义：项宽 = 文字宽 + 36、左对齐，**不是**等分整条 300
    // （三条加起来才 ~186，右侧那 100 多像素是空的 —— 点它不该有反应）。
    const float tw = ui.text_width(items[0], 13.0f, 500) + 36.0f;
    d.click(tbox.x + tw * 2.5f, tbox.cy(), tdraw);
    check(idx == 2, "点第三项 → 2", "idx=" + num(idx));

    d.click(tbox.x + tw * 2.5f, tbox.cy(), tdraw);
    check(idx == 2, "再点当前项不变", "idx=" + num(idx));

    d.click(tbox.x + tw * 0.5f, tbox.cy(), tdraw);
    check(idx == 0, "点第一项 → 0", "idx=" + num(idx));

    d.click(tbox.right() - 6.0f, tbox.cy(), tdraw);
    check(idx == 0, "点标题栏空白处不改选中", "idx=" + num(idx));

    int seg = 0;
    const Rect sbox(20.0f, 90.0f, 240.0f, 32.0f);
    auto sdraw = [&]() { ui.segmented(items, &seg, sbox); };
    d.click(sbox.x + sbox.w * 5.0f / 6.0f, sbox.cy(), sdraw);
    check(seg == 2, "胶囊点第三段 → 2", "seg=" + num(seg));

    d.click(sbox.x + sbox.w / 6.0f, sbox.cy(), sdraw);
    check(seg == 0, "胶囊点第一段 → 0", "seg=" + num(seg));

    int closed = 99;
    std::vector<aino::Color> dots(3, aino::Color::hex(0x34C759));
    aino::TabOptions options;
    options.close_index = &closed;
    options.indicators = &dots;
    options.max_width = 90.0f;
    aino::Style style;
    auto closable = [&] { ui.tabs(items, &idx, tbox, options, style); };
    d.idle(closable);
    check(closed == -1, "无关闭请求时输出 -1");
    d.click(tbox.x + 120, tbox.cy(), closable);
    check(idx == 1 && closed == -1, "可关闭页签正常切换");
    d.click(tbox.x + 270 - 15, tbox.cy(), closable);
    check(closed == 2 && idx == 1, "关闭未选页签不切换选中项");
    d.idle(closable);
    check(closed == -1, "关闭请求只返回一次");
    d.press(tbox.x + 270 - 15, tbox.cy(), closable);
    d.release(tbox.right() + 10, tbox.cy(), closable);
    check(closed == -1, "关闭按钮拖出后释放不触发");
    style.disabled = true;
    d.click(tbox.x + 30, tbox.cy(), closable);
    d.click(tbox.x + 270 - 15, tbox.cy(), closable);
    check(idx == 1 && closed == -1, "禁用时不切换或关闭页签");
    style.disabled = false;
    auto clipped = [&] {
        ui.push_clip(Rect(tbox.x, tbox.y, 80, tbox.h));
        closable();
        ui.pop_clip();
    };
    d.click(tbox.x + 270 - 15, tbox.cy(), clipped);
    check(closed == -1 && idx == 1, "裁剪区外的关闭按钮不能点击");

    const Rect narrow(20, 20, 140, 38);
    auto overflow = [&] { ui.tabs(items, &idx, narrow, options, style); };
    idx = 2;
    d.idle(overflow);
    d.click(93, narrow.cy(), overflow);
    check(closed == 2, "程序选择末项时自动显示该项和关闭按钮");
    for (int n = 0; n < 3; ++n) d.click(narrow.right() - 36, narrow.cy(), overflow);
    d.click(50, narrow.cy(), overflow);
    check(idx == 0, "溢出页签可向前翻到首项");
    for (int n = 0; n < 3; ++n) d.click(narrow.right() - 12, narrow.cy(), overflow);
    d.click(50, narrow.cy(), overflow);
    check(idx == 2, "溢出页签可向后翻到末项");
    d.idle(overflow);
    check(closed == -1, "翻页不会误关闭页签");
    ui.set_theme(aino::Theme::dark());
    d.idle(overflow);
    d.click(93, narrow.cy(), overflow);
    check(closed == 2 && idx == 2, "深色主题下复用相同关闭交互");
    auto empty = [&] { ui.tabs({}, &idx, narrow, options, {}); };
    d.idle(empty);
    check(closed == -1, "空列表清除关闭请求");
}

// ---------------------------------------------------------------- 9. 下拉选择
static void test_select() {
    std::printf("[9] Select\n");
    Surface sf(400, 320);
    Harness h(&sf);
    Driver d{h.ui, 400.0f, 320.0f};
    aino::Ui& ui = h.ui;

    int idx = 0;
    const std::vector<std::string> items = {"免费版", "专业版", "企业版"};
    const Rect box(20.0f, 20.0f, 200.0f, 36.0f);
    auto draw = [&]() { ui.select(items, &idx, box); };

    const int closed = d.defers(draw);
    check(closed == 0, "未展开时没有延迟绘制", "defers=" + num(closed));

    d.click(box.cx(), box.cy(), draw);
    const int opened = d.defers(draw);
    check(opened > 0, "点击后列表走帧末绘制", "defers=" + num(opened));

    // 列表紧贴控件下方，每项 34 高：第二项中心约 box.bottom() + 4 + 34 + 17
    d.click(box.cx(), box.bottom() + 4.0f + 34.0f * 1.5f, draw);
    check(idx == 1, "点第二项 → 1", "idx=" + num(idx));
    check(d.defers(draw) == 0, "选中后列表关闭");

    d.click(box.cx(), box.cy(), draw);
    d.click(370.0f, 300.0f, draw);
    check(d.defers(draw) == 0, "点外部关闭");

    d.click(box.cx(), box.cy(), draw);
    d.key(aino::KEY_ESC, draw);
    check(d.defers(draw) == 0, "Esc 关闭");
}

// ---------------------------------------------------------------- 10. 单选组
static void test_radio() {
    std::printf("[10] Radio Group\n");
    Surface sf(360, 220);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 220.0f};
    aino::Ui& ui = h.ui;

    int idx = 1;
    const std::vector<std::string> items = {"免费版", "专业版", "企业版"};
    const Rect box(20.0f, 20.0f, 220.0f, 90.0f);
    auto draw = [&]() { ui.radio_group(items, &idx, box, aino::Style().font(13.0f)); };

    d.click(box.x + 20.0f, box.y + 15.0f, draw);
    check(idx == 0, "点第一项 → 0", "idx=" + num(idx));

    d.click(box.x + 20.0f, box.y + 75.0f, draw);
    check(idx == 2, "点第三项 → 2", "idx=" + num(idx));

    d.click(box.x + 200.0f, box.y + 15.0f, draw);
    check(idx == 0, "点行尾也能选中", "idx=" + num(idx));
}

// ---------------------------------------------------------------- 11. 步进器与评分
static void test_stepper_rating() {
    std::printf("[11] Stepper / Rating\n");
    Surface sf(400, 220);
    Harness h(&sf);
    Driver d{h.ui, 400.0f, 220.0f};
    aino::Ui& ui = h.ui;

    float v = 5.0f;
    const Rect box(20.0f, 20.0f, 140.0f, 36.0f);
    auto draw = [&]() { ui.stepper(&v, 1.0f, 0.0f, 6.0f, box); };

    // NSStepper 的几何：右边缘 **20 宽竖条**，上半 = ＋、下半 = －（不是
    // "[−] 值 [+]" 那种横向三段，那是 Web 的 Number Input）。
    const float up_y = box.y + box.h * 0.25f, dn_y = box.y + box.h * 0.75f;
    d.click(box.right() - 10.0f, up_y, draw);
    check(v > 5.9f && v < 6.1f, "点上部三角 +1", std::string("v=") + s(v));

    d.click(box.right() - 10.0f, up_y, draw);
    check(v <= 6.001f, "到上限夹取", std::string("v=") + s(v));

    d.click(box.right() - 10.0f, dn_y, draw);
    check(v > 4.9f && v < 5.1f, "点下部三角 -1", std::string("v=") + s(v));

    v = 0.0f;
    d.click(box.x + 18.0f, box.cy(), draw);
    check(v >= -0.001f, "到下限夹取", std::string("v=") + s(v));

    int stars = 4;
    const Rect rbox(20.0f, 100.0f, 150.0f, 26.0f);
    auto rdraw = [&]() { ui.rating(&stars, 5, rbox); };
    d.click(rbox.x + rbox.w * 0.1f, rbox.cy(), rdraw);
    check(stars == 1, "点第一颗 → 1", "stars=" + num(stars));
    d.click(rbox.x + rbox.w * 0.3f, rbox.cy(), rdraw);
    check(stars == 2, "点第二颗 → 2", "stars=" + num(stars));
}

// ---------------------------------------------------------------- 12. 折叠面板
static void test_accordion() {
    std::printf("[12] Accordion\n");
    Surface sf(360, 200);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 200.0f};
    aino::Ui& ui = h.ui;

    bool open = false;
    const Rect box(20.0f, 20.0f, 260.0f, 40.0f);
    auto draw = [&]() { ui.accordion("渲染管线", &open, box); };

    d.click(box.cx(), box.cy(), draw);
    check(open, "点击展开");
    d.click(box.cx(), box.cy(), draw);
    check(!open, "再点收起");
    d.click(box.x + 240.0f, box.cy(), draw);
    check(open, "点行尾也能切换");
}

// ---------------------------------------------------------------- 13. 菜单
static void test_menu() {
    std::printf("[13] Menu\n");
    Surface sf(400, 320);
    Harness h(&sf);
    Driver d{h.ui, 400.0f, 320.0f};
    aino::Ui& ui = h.ui;

    int pick = -1;
    const std::vector<std::string> items = {"重命名", "复制", "删除"};
    const Rect box(20.0f, 20.0f, 120.0f, 36.0f);
    bool fired = false;
    auto draw = [&]() {
        if (ui.menu("操作", items, &pick, box, aino::Style().outline())) fired = true;
    };

    check(d.defers(draw) == 0, "未展开时无延迟绘制");
    d.click(box.cx(), box.cy(), draw);
    check(d.defers(draw) > 0, "点击后列表走帧末绘制");

    d.click(box.x + 40.0f, box.bottom() + 6.0f + 34.0f + 17.0f, draw);
    check(fired && pick == 1, "点第二项 → 回调 + index=1",
          "pick=" + num(pick));
    check(d.defers(draw) == 0, "选中后关闭");

    d.click(box.cx(), box.cy(), draw);
    d.click(370.0f, 300.0f, draw);
    check(d.defers(draw) == 0, "点外部关闭");
}

// ---------------------------------------------------------------- 14. 对话框
static void test_dialog() {
    std::printf("[14] Dialog\n");
    Surface sf(480, 360);
    Harness h(&sf);
    Driver d{h.ui, 480.0f, 360.0f};
    aino::Ui& ui = h.ui;

    bool open = false;
    const Rect box(90.0f, 80.0f, 300.0f, 180.0f);
    int inside = 0;
    auto draw = [&]() {
        if (ui.dialog("标题", "正文", &open, box)) ++inside;
    };

    d.idle(draw);
    check(inside == 0, "open=false 时不返回 true", "n=" + num(inside));

    open = true;
    d.idle(draw);
    check(inside > 0, "open=true 时每帧返回 true", "n=" + num(inside));

    // 标题区域没有可见关闭控件，点击不能关闭。
    d.click(box.right() - 19.0f, box.y + 27.0f, draw);
    check(open, "右上角没有隐藏关闭热区");

    open = true;
    d.key(aino::KEY_ESC, draw);
    check(!open, "Esc 关闭");

    open = true;
    d.click(box.cx(), box.cy(), draw);
    check(open, "点面板内部不关闭");

    d.click(10.0f, 340.0f, draw);
    check(!open, "点遮罩关闭");
}

// ---------------------------------------------------------------- 15. Tooltip
static void test_tooltip() {
    std::printf("[15] Tooltip\n");
    Surface sf(400, 260);
    Harness h(&sf);
    Driver d{h.ui, 400.0f, 260.0f};
    aino::Ui& ui = h.ui;

    const Rect anchor(40.0f, 40.0f, 120.0f, 36.0f);
    auto draw = [&]() { ui.tooltip(anchor, "提示文本"); };

    // 悬停 0.4 秒内隐藏，之后出现
    check(d.defers(draw, 380.0f, 240.0f) == 0, "指针不在锚点上 → 不绘制");
    check(d.defers(draw, anchor.cx(), anchor.cy()) == 0,
          "刚移入 → 延时内不显示（避免划过就闪）");
    for (int i = 0; i < 30; ++i) d.move(anchor.cx(), anchor.cy(), draw);
    check(d.defers(draw, anchor.cx(), anchor.cy()) > 0, "停留 0.5s 后出现");

    d.move(380.0f, 240.0f, draw);
    check(d.defers(draw, 380.0f, 240.0f) == 0, "移开即消失");
}

// ---------------------------------------------------------------- 16. 导航项
static void test_nav_item() {
    std::printf("[16] Nav Item\n");
    Surface sf(240, 200);
    Harness h(&sf);
    Driver d{h.ui, 240.0f, 200.0f};
    aino::Ui& ui = h.ui;

    const Rect box(10.0f, 10.0f, 200.0f, 32.0f);
    int hits = 0;
    auto draw = [&]() { if (ui.nav_item("Button", false, box)) ++hits; };
    d.click(box.cx(), box.cy(), draw);
    check(hits == 1, "点击返回 true", "hits=" + num(hits));

    int hits2 = 0;
    auto adraw = [&]() { if (ui.nav_item("Button", true, box)) ++hits2; };
    d.click(box.cx(), box.cy(), adraw);
    check(hits2 == 1, "已选中项仍然可点（用于重新聚焦）", "hits=" + num(hits2));
}

// ---------------------------------------------------------------- 17. 滚动
static void test_scroll() {
    std::printf("[17] Scroll\n");
    Surface sf(320, 300);
    Harness h(&sf);
    Driver d{h.ui, 320.0f, 300.0f};
    aino::Ui& ui = h.ui;

    const Rect vp(20.0f, 20.0f, 240.0f, 160.0f);
    const float ih = 30.0f;
    const int n = 20;
    float last_y = 0.0f;
    auto draw = [&]() {
        auto sc = ui.scroll(vp, ih * float(n));
        last_y = sc.y();
        for (int i = 0; i < n; ++i) ui.row(ih);
    };

    d.idle(draw);
    check(last_y > -0.001f && last_y < 0.001f, "初始在顶部", std::string("y=") + s(last_y));

    d.wheel(vp.cx(), vp.cy(), -3.0f, draw);
    const float scrolled = last_y;
    check(scrolled > 1.0f, "滚轮下滚把内容上移", std::string("y=") + s(scrolled));

    d.wheel(vp.cx(), vp.cy(), -50.0f, draw);
    check(last_y > scrolled, "继续下滚继续偏移", std::string("y=") + s(last_y));

    d.wheel(vp.cx(), vp.cy(), 200.0f, draw);
    check(last_y < 0.001f, "上滚到底夹在 0", std::string("y=") + s(last_y));

    // 指针在视口外时滚轮不应作用到这个视口
    d.wheel(vp.cx(), vp.cy(), -3.0f, draw);
    const float mid = last_y;
    d.wheel(300.0f, 290.0f, 5.0f, draw);
    check(last_y > 0.5f && last_y < mid + 1.0f, "视口外的滚轮不影响它",
          std::string("y=") + s(last_y));
}

// ---------------------------------------------------------------- 18. 闸门与裁剪
static void test_gate_and_clip() {
    std::printf("[18] 闸门 / 裁剪\n");
    Surface sf(360, 260);
    Harness h(&sf);
    Driver d{h.ui, 360.0f, 260.0f};
    aino::Ui& ui = h.ui;

    // 弹层打开后，下层按钮不该再收到点击
    int under = 0;
    const Rect ub(20.0f, 20.0f, 120.0f, 36.0f);
    int pick = -1;
    auto draw = [&]() {
        if (ui.button("下层", ub)) ++under;
        ui.menu("菜单", {"甲", "乙"}, &pick, Rect(20.0f, 90.0f, 120.0f, 36.0f));
    };
    d.click(80.0f, 108.0f, draw);          // 打开菜单
    const int before = under;
    d.click(ub.cx(), ub.cy(), draw);       // 点下层按钮
    check(under == before, "弹层展开时点到下层的按钮被闸门挡住",
          "under=" + num(under) + " before=" + num(before));

    // 裁剪：画到框外的文字不应出现在框外
    Surface sf2(200, 120);
    Harness h2(&sf2);
    Driver d2{h2.ui, 200.0f, 120.0f};
    const Rect box(40.0f, 40.0f, 80.0f, 40.0f);
    d2.idle([&]() {
        h2.ui.clear(aino::Color(255, 255, 255));
        h2.ui.push_clip(box);
        h2.ui.fill_rect(Rect(box.x, box.y, 200.0f, 40.0f), aino::Color(0, 0, 0), 0.0f);
        h2.ui.pop_clip();
    });
    const aino::Color outside = sf2.at(box.right() + 6.0f, box.cy());
    check(outside.r > 250, "裁剪区外的像素未被改写",
          "rgb=" + num(outside.r) + "," + num(outside.g) + "," + num(outside.b));
}

// ---------------------------------------------------------------- 19. 退化输入
static void test_degenerate() {
    std::printf("[19] 退化输入\n");
    Surface sf(200, 120);
    Harness h(&sf);
    Driver d{h.ui, 200.0f, 120.0f};
    aino::Ui& ui = h.ui;

    std::string empty;
    bool crashed = false;
    try {
        const Rect tiny(10.0f, 10.0f, 1.0f, 1.0f);
        d.idle([&]() {
            ui.input(&empty, tiny);
            ui.button("x", tiny);
            ui.checkbox(&crashed, "", tiny);
            ui.slider(nullptr, 0.0f, 1.0f, tiny);
            ui.tabs({"a"}, nullptr, tiny);
            ui.progress(-3.0f, tiny);
            ui.progress(9.0f, tiny);
        });
    } catch (...) {
        crashed = true;
    }
    check(!crashed, "空指针 / 1px 尺寸 / 越界进度值都不崩");

    bool ok2 = true;
    try {
        ui.input(nullptr, Rect(0, 0, 10, 10));
        ui.checkbox(nullptr, "x", Rect(0, 0, 10, 10));
        ui.toggle(nullptr, Rect(0, 0, 10, 10));
        ui.textarea(nullptr, Rect(0, 0, 10, 10));
    } catch (...) {
        ok2 = false;
    }
    check(ok2, "空指针参数被忽略而不是崩溃");
}

static void test_stable_identity() {
    std::printf("[20] stable editor identities\n");
    Surface sf(640, 480);
    Harness h(&sf);
    Driver d{h.ui, 640.0f, 480.0f};
    auto& ui = h.ui;
    for (bool multiline : {false, true}) {
        std::string a = "session-A", b = "session-B";
        std::string* current = &a;
        aino::Rect box(20, 20, 250, multiline ? 100 : 36);
        uint64_t explicit_id = 0, scope_id = 100;
        auto draw = [&] {
            auto owner = ui.state_scope(scope_id);
            if (multiline) ui.textarea(current, box, aino::Style().id(explicit_id));
            else ui.input(current, box, aino::Style().id(explicit_id));
        };
        d.click(100, 35, draw);
        d.type("X", draw);
        check(a != "session-A", "focused editor accepts text");
        const auto original_focus = ui.focused_id();
        box.x += 40;
        d.idle(draw);
        check(ui.focused_id() == original_focus, "moving editor preserves focus");
        d.key_ctrl('Z', draw);
        check(a == "session-A", "moving editor preserves undo history");
        d.type("Y", draw);
        current = &b;
        d.key_ctrl('Z', draw);
        check(b == "session-B", "different model at same position cannot consume another history");
        d.click(box.x + 70, 35, draw);
        d.key_ctrl('Z', draw);
        check(b == "session-B", "newly focused model has independent history");
        explicit_id = 77;
        d.click(box.x + 70, 35, draw);
        d.type("Z", draw);
        a = b;
        current = &a;
        d.key_ctrl('Z', draw);
        check(a == "session-B", "explicit ID preserves undo across model relocation");
        scope_id = 101;
        d.click(box.x + 70, 35, draw);
        d.key_ctrl('Z', draw);
        check(a == "session-B", "same explicit ID in another owner has independent history");
        ui.release_state_scope(100);
        ui.release_state_scope(101);
    }
    std::string amount;
    auto group = [&] { ui.input_group(&amount, "$", "USD", {20, 20, 250, 36}, aino::Style().id(88)); };
    d.click(25, 35, group);
    d.type("42", group);
    check(amount == "42", "input group prefix focuses the editor with its explicit ID");
}

static void test_caret_schedule() {
    Surface sf(640, 480);
    Harness h(&sf);
    Driver d{h.ui, 640, 480};
    auto& ui = h.ui;
    std::string text = "caret";
    for (bool multiline : {false, true}) {
        auto draw = [&] {
            if (multiline) ui.textarea(&text, {20, 20, 220, 100});
            else ui.input(&text, {20, 20, 220, 36});
        };
        d.click(70, 35, draw);
        for (float phase : {0.1f, 0.7f, 1.2f, 1.8f, 2.3f}) {
            bool immediate = false;
            ui.impl->request_repaint = [&] { immediate = true; };
            ui.impl->caret_blink = phase;
            d.idle(draw);
            check(ui.impl->repaint_timeout_ms() > 0 && ui.impl->repaint_timeout_ms() <= 620,
                  "visible and hidden caret phases both schedule their next edge");
            check(!immediate, "stable editor does not continuously repaint for caret blinking");
            ui.impl->request_repaint = nullptr;
        }
        ui.set_focus(nullptr);
        d.idle(draw);
        check(ui.impl->repaint_timeout_ms() == -1, "unfocused editor cancels caret timer");
    }
}

int main() {
    std::printf("== 存量控件交互断言 ==\n");
    test_button();
    test_icon_button();
    test_input_edit();
    test_textarea();
    test_checkbox();
    test_switch();
    test_slider();
    test_tabs();
    test_select();
    test_radio();
    test_stepper_rating();
    test_accordion();
    test_menu();
    test_dialog();
    test_tooltip();
    test_nav_item();
    test_scroll();
    test_gate_and_clip();
    test_degenerate();
    test_stable_identity();
    test_caret_schedule();
    std::printf("=== 通过 %d 项，失败 %d 项 ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
