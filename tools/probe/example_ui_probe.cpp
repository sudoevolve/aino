// 示例组合的离线交互回归；SSH 模式不创建 App、不读取配置、不连接网络。
#ifdef AINO_PROBE_SSH
#define WinMain aino_ssh_entry
#include "../../examples/ssh_client.cpp"
#undef WinMain
#else
#define main aino_function_entry
#include "../../examples/function_viewer.cpp"
#undef main
#endif

namespace {
int passed = 0, failed = 0;
void expect(bool ok, const char* name) {
    if (ok) ++passed;
    else { ++failed; std::printf("FAIL: %s\n", name); }
}

struct ExampleFrame {
    aino::Ui ui;
    aino::detail::GdiTextRenderer text;
    aino::detail::Canvas canvas;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr, previous = nullptr;
    ExampleFrame() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 960;
        info.bmiHeader.biHeight = -640;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        previous = HBITMAP(SelectObject(dc, bitmap));
        canvas.px = static_cast<uint8_t*>(pixels);
        canvas.w = 960; canvas.h = 640; canvas.stride = 960 * 4;
        canvas.platform_dc = dc;
        text.init(1, {}, {});
        ui.impl->canvas = &canvas;
        ui.impl->text = &text;
    }
    ~ExampleFrame() {
        text.release();
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    template<class Draw> void frame(Draw draw, aino::Input in = {}) {
        ui.impl->input = in;
        ui.begin_frame(960, 640, 1.f / 60, 1);
        ui.clear(ui.theme().background);
        draw();
        ui.end_frame();
    }
    template<class Draw> void click(Draw draw, float x, float y) {
        aino::Input in;
        in.mouse = {x, y};
        in.down[0] = in.pressed[0] = true;
        frame(draw, in);
        in.down[0] = in.pressed[0] = false;
        in.released[0] = true;
        frame(draw, in);
    }
    template<class Draw> void key(Draw draw, int code, bool ctrl = false) {
        aino::Input in;
        in.key_pressed[code] = true;
        in.key_ctrl[code] = ctrl;
        in.key_down[aino::KEY_CTRL] = ctrl;
        frame(draw, in);
    }
};
}

int main() {
    ExampleFrame f;
#ifdef AINO_PROBE_SSH
    std::string password;
    bool reveal = false;
    const Rect box(40, 40, 240, 30);
    auto draw = [&] { password_field(f.ui, f.ui.theme(), &password, &reveal, box); };
    f.frame(draw);
    f.click(draw, 60, 55);
    aino::Input typed;
    typed.text_input = "密钥😀";
    f.frame(draw, typed);
    expect(password == "密钥😀", "password accepts UTF-8");
    f.key(draw, aino::KEY_BACKSPACE);
    expect(password == "密钥", "backspace removes a whole Unicode character");
    f.key(draw, 'A', true);
    // 与真实输入间隔一致：组合键后的短暂 WM_CHAR 抑制结束后再输入。
    for (int i = 0; i < 10; ++i) f.frame(draw);
    typed.text_input = "替换";
    f.frame(draw, typed);
    expect(password == "替换", "selection replaces password");
    f.key(draw, 'Z', true);
    expect(password == "密钥", "password supports undo");
    f.click(draw, 266, 55);
    expect(reveal && password == "密钥", "eye reveals without modifying password");
    f.click(draw, 266, 55);
    expect(!reveal && password == "密钥", "eye hides without modifying password");
    std::string clipboard;
    f.ui.impl->get_clipboard = [&] { return std::string("粘贴"); };
    f.ui.impl->set_clipboard = [&](const std::string& value) { clipboard = value; };
    f.click(draw, 60, 55);
    f.key(draw, 'A', true);
    f.key(draw, 'V', true);
    expect(password == "粘贴", "password supports paste");
    f.key(draw, 'A', true);
    f.key(draw, 'C', true);
    expect(clipboard.empty(), "secret input does not copy password");
    int clicks = 0;
    auto button = [&] {
        if (dlg_button(f.ui, f.ui.theme(), "连接", Rect(40, 120, 100, 28), Btn::Primary)) ++clicks;
    };
    g_block_input = true;
    f.click(button, 80, 134);
    expect(clicks == 0, "modal blocks underlying native button");
    g_block_input = false;
    f.click(button, 80, 134);
    expect(clicks == 1, "native dialog button activates after unblock");
    auto tool = [&] {
        if (tool_button(f.ui, f.ui.theme(), "删除", Rect(40, 180, 100, 28), true)) ++clicks;
    };
    g_block_input = true;
    f.click(tool, 80, 194);
    expect(clicks == 1, "modal blocks underlying tool button");
#else
    UiState st;
    auto draw = [&] { draw_sidebar(f.ui, st, f.ui.theme()); };
    const Rect list(0, 55, 232, 521);
    for (int cat = 0; cat < kCustomCat; ++cat) {
        st.app.cat = cat;
        st.app.sel = 0;
        st.refresh();
        f.frame(draw);
        expect(f.ui.scroll_offset(list) == 0, "category starts at top");
        aino::Input wheel;
        wheel.mouse = {100, 200};
        wheel.wheel = -1000;
        f.frame(draw, wheel);
        const float offset = f.ui.scroll_offset(list);
        const int last = int(st.presets.size()) - 1;
        const float cy = 65 + 27.f * last - offset + 13;
        f.click(draw, 100, cy);
        expect(st.app.sel == last, "last preset remains reachable at minimum window size");
        const int selected = st.app.sel;
        f.click(draw, 20, 610);
        expect(st.app.sel == selected, "footer cannot activate a clipped list item");
    }
    // 追加预设以覆盖列表溢出和末项点击。
    while (st.presets.size() < 40) {
        Preset copy = st.presets.front();
        copy.name += std::to_string(st.presets.size());
        st.presets.push_back(copy);
    }
    f.frame(draw);
    aino::Input wheel;
    wheel.mouse = {100, 200};
    wheel.wheel = -1000;
    f.frame(draw, wheel);
    const float offset = f.ui.scroll_offset(list);
    expect(offset > 0, "long preset list scrolls");
    f.click(draw, 100, 65 + 27.f * 39 - offset + 13);
    expect(st.app.sel == 39, "last item in long list is reachable");
    const bool dark = st.app.dark;
    f.click(draw, 192, 597);
    expect(st.app.dark != dark, "theme toggle stays outside list clipping");
    st.app.cat = kCustomCat;
    f.frame(draw);
    expect(f.ui.scroll_offset(list) == 0, "custom hints reset scroll position");
#endif
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
