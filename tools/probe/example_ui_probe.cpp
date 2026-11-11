// 默认离线；SSH 的 --local-transfer 额外验证本机测试服务器，不读取用户连接配置。
#include <fstream>
#include <iterator>
#ifdef AINO_PROBE_SSH
#define WinMain aino_ssh_entry
#include "../../examples/ssh_client.cpp"
#undef WinMain
#elif defined(AINO_PROBE_POMODORO)
#define main aino_pomodoro_entry
#include "../../examples/pomodoro.cpp"
#undef main
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

int main(int argc, char** argv) {
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
    g_block_input = false;
    {
        App app;
        for (const char* title : {"one", "two", "three"}) {
            auto session = std::make_shared<Session>();
            session->title = title;
            app.tabs.push_back(session);
        }
        app.active = 0;
        auto tabs = [&] { draw_tabs(f.ui, app, f.ui.theme(), 40); };
        const float first_w = f.ui.text_width("one", 12.5f, 500) + 70.0f;
        f.click(tabs, 8 + first_w + 25, 20);
        expect(app.active == 1 && !app.show_start, "native tabs return from home to selected session");
        const auto selected = app.tabs[1];
        g_block_input = true;
        f.click(tabs, 30, 20);
        f.click(tabs, 8 + first_w - 15, 20);
        f.click(tabs, 936, 20);
        expect(app.active == 1 && app.tabs.size() == 3 && !app.show_start,
               "modal blocks tab selection, close and new controls");
        g_block_input = false;
        f.click(tabs, 8 + first_w - 15, 20);
        expect(app.tabs.size() == 2 && app.active == 0 && app.tabs[0] == selected,
               "closing earlier tab preserves the selected session");
        f.click(tabs, 936, 20);
        expect(app.show_start && app.tabs.size() == 2, "native new button opens home without closing sessions");
        f.click(tabs, 30, 20);
        expect(!app.show_start && app.tabs[app.active] == selected, "selected session remains reachable from home");
        const float second_w = f.ui.text_width("two", 12.5f, 500) + 70.0f;
        f.click(tabs, 8 + second_w - 15, 20);
        expect(app.tabs.size() == 1 && app.active == 0 && !app.show_start,
               "closing active tab selects remaining session");
        const float last_w = f.ui.text_width("three", 12.5f, 500) + 70.0f;
        f.click(tabs, 8 + last_w - 15, 20);
        expect(app.tabs.empty() && app.active == -1 && app.show_start, "last native tab closes safely to home");
        for (int i = 0; i < 9; ++i) {
            auto session = std::make_shared<Session>();
            session->title = std::string(80, 'x') + std::to_string(i);
            app.tabs.push_back(session);
        }
        app.active = 8;
        app.show_start = false;
        f.frame(tabs);
        f.click(tabs, 936, 20);
        expect(app.show_start && app.tabs.size() == 9, "overflow tabs leave new button reachable");
        app.shutdown_all();
    }
    if (argc > 1 && std::string(argv[1]) == "--local-transfer") {
        Session session;
        std::string error;
        const int port = argc > 2 ? std::atoi(argv[2]) : 2222;
        const bool connected = session.engine.connect("127.0.0.1", port, "demo", "demo", "", &error);
        expect(connected, "local SSH connection");
        if (!connected) { std::printf("%s\n", error.c_str()); return 1; }
        expect(session.engine.open_shell(80, 24, &error), "local shell opens");
        session.engine.start_reader();
        expect(session.engine.open_sftp(&error), "local SFTP opens");
        std::string body(1024 * 1024, '\0');
        for (size_t i = 0; i < body.size(); ++i) body[i] = char(i % 251);
        const std::string local = "build/session-upload.bin", back = "build/session-download.bin";
        { std::ofstream out(local, std::ios::binary); out.write(body.data(), body.size()); }
        auto wait_transfer = [&] {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (session.xfer_running && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return !session.xfer_running;
        };
        expect(start_transfer(session, true, local, "/session-probe.bin", nullptr), "upload starts");
        session.engine.request_pty_resize(90, 28);
        session.engine.request_pty_resize(100, 32);
        expect(wait_transfer(), "upload completes without UI frames");
        auto transfer = session.transfer_snapshot();
        expect(transfer.finished && transfer.error.empty() && transfer.done == body.size(),
               "upload snapshot has exact byte count");
        const auto resize_end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (session.engine.pty_cols() != 100 && std::chrono::steady_clock::now() < resize_end)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        expect(session.engine.pty_cols() == 100 && session.engine.pty_rows() == 32,
               "background reader applies latest PTY size");
        expect(start_transfer(session, false, back, "/session-probe.bin", nullptr), "download starts");
        expect(wait_transfer(), "download completes without UI frames");
        { std::ifstream in(back, std::ios::binary);
          std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
          expect(got == body, "download exactly matches upload"); }
        session.engine.unlink("/missing-probe.bin", &error);
        expect(start_transfer(session, false, back, "/missing-probe.bin", nullptr), "missing download starts");
        expect(wait_transfer() && !session.transfer_snapshot().error.empty(), "missing remote reports error");
        expect(start_transfer(session, true, local, "/missing-probe/file.bin", nullptr), "invalid target starts");
        expect(wait_transfer() && !session.transfer_snapshot().error.empty(), "invalid target reports error");
        expect(session.engine.transfer().rh == nullptr && session.engine.transfer().fh == nullptr,
               "failed setup closes transfer handles");
        expect(start_transfer(session, false, "build/missing-probe/file.bin", "/session-probe.bin", nullptr),
               "unwritable local target starts");
        expect(wait_transfer() && !session.transfer_snapshot().error.empty(), "local open failure reports error");
        expect(session.engine.transfer().rh == nullptr, "local failure closes remote handle");
        expect(start_transfer(session, true, local, "/session-cancel.bin", nullptr), "cancel transfer starts");
        session.xfer_cancel = true;
        expect(wait_transfer() && session.transfer_snapshot().error == "已取消", "transfer cancels in background");
        session.engine.send("unicode\r");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        update_session(session);
        expect(!session.engine.has_output(), "hidden session output is consumed");
        std::string screen;
        for (int row = 0; row < session.term.total_lines(); ++row) screen += session.term.line_text(row);
        expect(screen.find("你好，世界！") != std::string::npos, "hidden terminal processes UTF-8 output");
        expect(start_transfer(session, true, local, "/session-shutdown.bin", nullptr), "shutdown transfer starts");
        session.shutdown();
        expect(!session.xfer_running && !session.engine.connected(), "shutdown joins worker before disconnect");
    }
#elif defined(AINO_PROBE_POMODORO)
    (void)argc; (void)argv;
    const auto start = Clock::time_point{};
    auto after = [&](int seconds) { return start + std::chrono::seconds(seconds); };
    Pomodoro timer;
    expect(!timer.running && timer.remaining == 1500, "timer starts paused at 25 minutes");
    timer.toggle(start);
    expect(timer.running, "start begins countdown");
    timer.update(after(100));
    expect(timer.remaining == 1400, "late frame uses elapsed clock time");
    timer.toggle(after(100));
    timer.update(after(1000));
    expect(!timer.running && timer.remaining == 1400, "pause freezes remaining time");
    timer.toggle(after(1000));
    timer.update(after(1001));
    expect(timer.running && timer.remaining == 1399, "resume preserves remaining time");
    timer.select(0);
    expect(timer.running && timer.remaining == 1399, "reselecting active mode preserves timer");
    timer.select(1);
    expect(!timer.running && timer.remaining == 300, "short break starts paused at five minutes");
    timer.select(2);
    expect(timer.remaining == 900, "long break uses fifteen minutes");
    timer.minutes[2] = 20;
    timer.reset();
    expect(timer.remaining == 1200, "reset uses configured duration");
    timer.toggle(start);
    timer.update(after(1201));
    expect(!timer.running && timer.remaining == 0 && timer.completed == 0,
           "break completion stops without counting a focus session");
    timer.select(0);
    timer.toggle(start);
    timer.update(after(1501));
    expect(!timer.running && timer.remaining == 0 && timer.completed == 1,
           "focus completion stops and counts once");
    timer.update(after(1600));
    expect(timer.completed == 1, "completed session cannot be counted twice");
    timer.toggle(after(1600));
    expect(timer.running && timer.remaining == 1500, "completed timer can restart");
    timer.reset();
    expect(!timer.running && timer.remaining == 1500 && timer.completed == 1,
           "reset preserves completed sessions");

    bool dark = false;
    auto now = start;
    auto draw = [&] { draw_pomodoro(f.ui, timer, dark, now); };
    f.frame(draw);
    expect(!f.ui.animating(), "idle timer does not enable continuous animation");
    f.click(draw, 630, 432);
    expect(timer.running, "start button begins countdown");
    f.click(draw, 885, 584);
    expect(timer.minutes[0] == 25, "duration control is disabled while running");
    now = after(2);
    f.click(draw, 630, 432);
    expect(!timer.running && timer.remaining == 1498, "pause button freezes countdown");
    f.click(draw, 885, 584);
    expect(timer.minutes[0] == 26 && timer.remaining == 1560, "stepper updates and resets duration");
    f.click(draw, 630, 432);
    f.click(draw, 520, 432);
    expect(!timer.running && timer.remaining == 1560, "reset button stops running timer");
    f.click(draw, 80, 147);
    expect(timer.mode == 1 && timer.remaining == 300, "sidebar selects short break");
    f.click(draw, 630, 432);
    now = after(12);
    f.click(draw, 80, 147);
    expect(timer.running && timer.remaining == 290, "active sidebar item does not reset countdown");
    f.click(draw, 80, 178);
    expect(timer.mode == 2 && !timer.running && timer.remaining == 1200,
           "switching sidebar mode stops and restores its duration");
    f.click(draw, 924, 30);
    expect(dark, "appearance button switches to dark theme");
    f.ui.set_theme(aino::Theme::dark());
    f.frame(draw);
    f.click(draw, 924, 30);
    expect(!dark, "appearance button returns to light theme");
    f.click(draw, 630, 432);
    expect(!f.ui.animating(), "running countdown does not enable continuous animation");
#else
    (void)argc; (void)argv;
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

    UiState plot;
    plot.refresh();
    plot.reset_params();
    expect(!plot.app.anim, "viewer starts paused");
    Rect card(254, 96, 684, 300);
    auto draw_plot_only = [&] { draw_plot(f.ui, plot, card, f.ui.theme()); };
    static int evaluations = 0;
    plot.presets[0].curve = [](float x, const float* p, float t) {
        ++evaluations;
        return std::sin(x * p[0] + t);
    };
    f.frame(draw_plot_only);
    expect(evaluations > 64, "first plot samples curve");
    int before = evaluations;
    aino::Input hover;
    hover.mouse = {500, 200};
    f.frame(draw_plot_only, hover);
    expect(evaluations - before <= 1, "crosshair reuses curve samples");
    before = evaluations;
    f.ui.set_theme(aino::Theme::dark());
    f.frame(draw_plot_only);
    expect(evaluations == before, "theme change reuses samples");
    auto changed = [&](auto change, const char* message) {
        const int count = evaluations;
        change();
        f.frame(draw_plot_only);
        expect(evaluations > count + 64, message);
    };
    changed([&] { plot.app.p[0] += 0.25f; }, "parameter invalidates samples");
    changed([&] { plot.app.zoom *= 0.9f; }, "zoom invalidates samples");
    changed([&] { plot.app.cx += 0.2f; }, "pan invalidates samples");
    changed([&] { plot.app.t += 0.1f; }, "time invalidates samples");
    changed([&] { card.w -= 20; }, "plot size invalidates samples");
    changed([&] { plot.app.deriv = true; }, "derivative toggle invalidates samples");
    expect(!plot.plot_cache.samples.derivative.empty(), "derivative samples are populated");

    plot.app.cat = kCustomCat;
    plot.refresh();
    for (int mode = 0; mode < 5; ++mode) {
        plot.app.custom_mode = mode;
        plot.app.custom_src = "sin(";
        f.frame(draw_plot_only, hover);
        expect(!plot.app.prog.ok && !plot.plot_cache.valid,
               "invalid custom formula is safe with crosshair");
    }
    plot.app.custom_mode = 1;
    plot.app.custom_src = "cos(u)";
    plot.app.custom_src2 = "sin(";
    f.frame(draw_plot_only, hover);
    expect(plot.app.prog.ok && !plot.app.prog2.ok && !plot.plot_cache.valid,
           "invalid second parameter formula is safe");
    plot.app.custom_mode = 0;
    plot.app.custom_src = "sin(x)";
    f.frame(draw_plot_only);
    const auto samples = plot.plot_cache.samples.points;
    plot.app.custom_src = "cos(x)";
    f.frame(draw_plot_only);
    const auto& updated = plot.plot_cache.samples.points;
    expect(!updated.empty() && samples.front().y != updated.front().y,
           "custom source invalidates samples");

    Preset placeholder;
    auto editor = [&] {
        draw_plot_only();
        draw_controls(f.ui, plot, Rect(254, 410, 684, 220), f.ui.theme(), Kind::Curve, placeholder);
    };
    f.click(editor, 420, 440);
    expect(f.ui.focused_id() != nullptr, "formula input receives focus");
    f.key(editor, aino::KEY_SPACE);
    expect(!plot.app.anim, "space in formula does not start playback");
    plot.app.zoom = 2;
    f.key(editor, 'R');
    expect(plot.app.zoom == 2, "R in formula does not reset view");
    f.click(editor, 500, 200);
    f.key(editor, aino::KEY_SPACE);
    expect(plot.app.anim, "space toggles playback after focusing plot");
    f.key(editor, 'R', true);
    expect(plot.app.zoom == 2, "Ctrl+R does not reset view");
    aino::Input quick_ctrl;
    quick_ctrl.key_pressed['R'] = true;
    quick_ctrl.key_ctrl['R'] = true;
    f.frame(editor, quick_ctrl);
    expect(plot.app.zoom == 2, "released Ctrl still protects key combination");
    f.key(editor, 'R');
    expect(plot.app.zoom == 1, "R resets plot view");
    for (int cat = 1; cat < kCustomCat; ++cat) {
        UiState cached;
        cached.app.cat = cat;
        cached.refresh();
        cached.reset_params();
        auto& preset = cached.presets[0];
        if (preset.kind == Kind::Param)
            preset.param = [](float u, const float*, float, float& x, float& y) {
                ++evaluations; x = std::cos(u); y = std::sin(u);
            };
        else if (preset.kind == Kind::Polar)
            preset.curve = [](float x, const float*, float) { ++evaluations; return 1 + std::cos(x); };
        else
            preset.field = [](float x, float y, const float*, float) { ++evaluations; return x * y; };
        auto render = [&] { draw_plot(f.ui, cached, card, f.ui.theme()); };
        before = evaluations;
        f.frame(render);
        expect(evaluations > before + 64, "each plot mode populates samples");
        before = evaluations;
        f.frame(render, hover);
        expect(evaluations <= before + 1, "each plot mode reuses samples on hover");
        before = evaluations;
        cached.app.p[1] += 1;
        f.frame(render);
        expect(evaluations > before + 64, "each plot mode refreshes changed parameters");
        if (preset.kind == Kind::Field) {
            before = evaluations;
            cached.app.contour = true;
            f.frame(render);
            expect(evaluations > before + 64, "contour toggle refreshes grid resolution");
        }
    }
#endif
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
