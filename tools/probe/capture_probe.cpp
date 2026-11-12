// 跨平台验证 active 生命周期；Windows 使用不可见消息窗口检查真实捕获消息。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>

static int passed = 0, failed = 0;
static void check(bool ok, const char* message) {
    if (ok) ++passed;
    else { ++failed; std::printf("[FAIL] %s\n", message); }
}
static void frame(aino::Ui& ui) { ui.begin_frame(400, 300, .016f, 1); }
static aino::detail::Interact control(aino::Ui& ui, uint64_t id) {
    return aino::detail::interact(ui.impl, id, {0, 0, 100, 30}, true);
}
static void click(aino::Ui& ui) {
    auto& input = ui.impl->input;
    input.mouse = {10, 10}; input.down[0] = true; input.pressed[0] = true;
    frame(ui); check(control(ui, 2).pressed, "next control accepts a new press"); ui.end_frame();
    input.down[0] = false; input.released[0] = true;
    frame(ui); check(control(ui, 2).clicked, "next control accepts a new click"); ui.end_frame();
}
static void shared_lifecycle() {
    for (bool release_delivered : {true, false}) {
        aino::Ui ui;
        auto& input = ui.impl->input;
        input.mouse = {10, 10}; input.down[0] = true; input.pressed[0] = true;
        frame(ui); check(control(ui, 1).pressed, "original control owns the press"); ui.end_frame();
        // 刷新、切页或布局变更后原控件已消失；取消输入也可能不含 released。
        input.down[0] = false; input.released[0] = release_delivered;
        frame(ui); ui.end_frame();
        check(ui.impl->active == 0, "missing control cannot retain active after release or cancellation");
        click(ui);
    }
    aino::Ui ui;
    auto& input = ui.impl->input;
    input.mouse = {10, 10}; input.down[0] = true; input.pressed[0] = true;
    frame(ui); control(ui, 1); ui.end_frame();
    frame(ui); check(control(ui, 1).held, "held drag survives across frames"); ui.end_frame();
    check(ui.impl->active == 1, "frame end preserves a held control");
    input.down[0] = false; input.released[0] = true;
    bool clicked = false;
    frame(ui);
    ui.deferred([&] { clicked = control(ui, 1).clicked; });
    ui.end_frame();
    check(clicked, "deferred popup control consumes release before frame-end cleanup");
    click(ui);
}

// 自绘输入与原生组件共享浮层闸门；关闭回调不得留下下一帧闸门。
static void overlay_input_checks() {
    using namespace aino;
    Ui ui; auto& input = ui.impl->input;
    const Rect trigger{350, 265, 30, 30};
    bool open = true, expect_blocked = false, masked_ok = true;
    bool close_inside = false, close_before_end = false, visible = false;
    bool inner_press = false, raw_press = false;
    int callbacks = 0, inner_clicks = 0, background_clicks = 0;
    auto sample = [&] {
        if (!expect_blocked) return;
        const auto& in = ui.input();
        masked_ok = masked_ok && !ui.pointer_available() && in.wheel == 0 &&
            !in.double_click && in.click_count == 0 && !in.dragging &&
            in.mouse_delta.x == 0 && in.mouse_delta.y == 0 &&
            in.mouse.x == input.mouse.x && in.mouse.y == input.mouse.y;
        for (int i = 0; i < 3; ++i)
            masked_ok = masked_ok && !in.down[i] && !in.pressed[i] && !in.released[i];
        check(in.key_pressed[KEY_ENTER] == input.key_pressed[KEY_ENTER] &&
              in.text_input == input.text_input, "pointer filtering preserves keyboard input");
    };
    auto draw = [&] {
        ui.begin_frame(400, 300, .016f, 1);
        sample();
        if (detail::interact(ui.impl, 8101, {0, 0, 340, 300}, true).clicked) ++background_clicks;
        visible = ui.popover("popup", &open, trigger, {200, 100}, [&](const Rect& inner) {
            ++callbacks;
            inner_press = inner_press || (ui.pointer_available() && ui.input().pressed[0]);
            if (ui.button("inner", inner)) ++inner_clicks;
            if (close_inside) open = false;
        });
        sample();
        if (detail::interact(ui.impl, 8102, {0, 0, 340, 300}, true).clicked) ++background_clicks;
        raw_press = ui.raw_input().pressed[0];
        if (close_before_end) open = false;
        ui.end_frame();
    };
    draw();
    check(ui.impl->gate_next_allow_n == 2, "popover registers only its live trigger and panel");
    const auto panel = ui.impl->gate_next_allow[1];
    input.mouse = {panel.cx(), panel.cy()};
    for (int i = 0; i < 3; ++i) input.down[i] = input.pressed[i] = true;
    input.wheel = 2; input.double_click = true; input.click_count = 2;
    input.mouse_delta = {3, 4}; input.dragging = true;
    input.key_pressed[KEY_ENTER] = true; input.text_input = "a";
    expect_blocked = true; draw();
    check(masked_ok && inner_press && raw_press,
          "custom background input is masked while the popup and raw_input retain the press");
    for (int i = 0; i < 3; ++i) { input.down[i] = false; input.released[i] = true; }
    input.dragging = false; draw();
    check(masked_ok && inner_clicks == 1 && background_clicks == 0,
          "popup button receives release without leaking to native or custom background controls");
    const int before = callbacks;
    input.mouse = {10, 10}; input.down[0] = true; input.pressed[0] = true;
    draw();
    check(!open && !visible && callbacks == before && !ui.impl->gate_next && masked_ok,
          "outside press dismisses without running the closed callback or renewing its gate");
    expect_blocked = false; input.down[0] = false; input.released[0] = true; draw();
    check(!ui.impl->gate_on && background_clicks == 0, "closing gesture does not activate background controls");
    input.down[0] = true; input.pressed[0] = true; draw();
    input.down[0] = false; input.released[0] = true; draw();
    check(background_clicks == 1, "next background click works immediately after dismissal");
    open = true; draw();
    const int before_escape = callbacks;
    input.key_pressed[KEY_ESC] = true; expect_blocked = true; draw();
    check(!open && !visible && callbacks == before_escape && !ui.impl->gate_next,
          "Escape dismisses without executing a stale popover callback");
    expect_blocked = false; open = true; close_inside = true; draw();
    check(!open && !ui.impl->gate_next, "closing inside the deferred callback leaves no stale gate");
    close_inside = false; close_before_end = true; open = true;
    const int before_cancel = callbacks; draw();
    check(callbacks == before_cancel && !ui.impl->gate_next,
          "popover cancelled before frame end does not execute or retain input");

    input.mouse = {10, 10}; input.pressed[0] = input.down[0] = true;
    ui.begin_frame(400, 300, .016f, 1);
    ui.push_clip({20, 20, 30, 30});
    check(!ui.pointer_available() && !ui.input().pressed[0] && ui.raw_input().pressed[0],
          "custom input obeys clipping without consuming the original event");
    ui.pop_clip();
    check(ui.pointer_available() && ui.input().pressed[0], "leaving clip restores visible input");
    ui.begin_measure();
    check(!ui.pointer_available() && !ui.input().pressed[0], "measurement cannot activate custom input");
    ui.end_measure();
    check(ui.input().pressed[0], "measurement preserves the real frame input");
    ui.end_frame();
}

static void overlay_menu_lifecycle_checks() {
    using namespace aino;
    for (int kind = 0; kind < 3; ++kind) {
        Ui ui; auto& input = ui.impl->input;
        const Rect trigger{20, 20, 80, 24};
        const std::vector<std::string> items{"one", "two", "three"};
        int choice = -1, selected = 0, background = 0;
        auto draw = [&] {
            ui.begin_frame(400, 300, .016f, 1);
            if (detail::interact(ui.impl, 8201, {0, 80, 400, 220}, true).clicked) ++background;
            if (kind == 0) ui.menu("menu", items, &choice, trigger);
            else if (kind == 1) ui.select(items, &selected, trigger);
            else ui.context_menu(trigger, items, &choice);
            ui.end_frame();
        };
        auto press = [&](float x, float y, int button) {
            input.mouse = {x, y}; input.down[button] = input.pressed[button] = true; draw();
        };
        auto release = [&](int button) {
            input.down[button] = false; input.released[button] = true; draw();
        };
        auto open = [&] { const int button = kind == 2 ? 1 : 0; press(trigger.cx(), trigger.cy(), button); release(button); };
        open();
        check(ui.impl->gate_next_allow_n > 0, "open menu registers its live gate");
        const auto panel = ui.impl->gate_next_allow[ui.impl->gate_next_allow_n - 1];
        press(panel.cx(), panel.y + 4 + 24 + 12, 0); release(0);
        check((kind == 1 ? selected : choice) == 1 && !ui.impl->gate_next && background == 0,
              "selecting an item does not renew the closed menu gate or click through");
        open(); press(10, 280, 0);
        check(!ui.impl->gate_next && background == 0, "outside press closes menu without renewing its gate");
        release(0); draw();
        check(!ui.impl->gate_on && background == 0, "closed menu releases the background gate");
        open(); input.key_pressed[KEY_ESC] = true; draw();
        check(!ui.impl->gate_next, "Escape closes every menu type without a stale gate");
    }
}

#ifdef AINO_PLATFORM_WINDOWS
static void native_lifecycle() {
    aino::Window::Impl owner;
    aino::detail::Win32State state;
    state.owner = &owner;
    const auto instance = GetModuleHandleW(nullptr);
    const wchar_t* name = L"AinoCaptureProbe";
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpszClassName = name;
    cls.lpfnWndProc = aino::detail::win_proc;
    const ATOM registered = RegisterClassW(&cls);
    check(registered != 0, "register isolated capture probe window");
    if (!registered) return;
    const HWND window = CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                        nullptr, instance, &state);
    check(window != nullptr, "create invisible message-only window");
    if (window) {
        owner.ui.impl->ime_owner = true;
        aino::detail::win_set_ime_pos(&state, 48, 32, 54);
        struct Position { DWORD size,index; POINT point; UINT height; RECT document; } position{};
        position.size = sizeof position;
        check(SendMessageW(window, WM_IME_REQUEST, 0x0006, reinterpret_cast<LPARAM>(&position)) == TRUE,
              "IME character-position request is handled");
        POINT expected{48,32}; ClientToScreen(window,&expected);
        check(position.point.x == expected.x && position.point.y == expected.y && position.height == 22,
              "IME candidate anchor uses screen coordinates and caret height");
        for (UINT message : {WM_KILLFOCUS, WM_CAPTURECHANGED, WM_CANCELMODE}) {
            owner.ui.impl->active = 1; owner.need_frame = false;
            for (int i = 0; i < 3; ++i)
                state.btn_down[i] = state.btn_pressed[i] = state.btn_released[i] = true;
            SetCapture(window);
            if (message == WM_CAPTURECHANGED) ReleaseCapture();
            else SendMessageW(window, message, 0, 0);
            bool cleared = owner.ui.impl->active == 0;
            for (int i = 0; i < 3; ++i)
                cleared = cleared && !state.btn_down[i] && !state.btn_pressed[i] && !state.btn_released[i];
            check(cleared, "focus/capture loss and cancel mode clear pending mouse state without a click");
            check(GetCapture() != window, "cancelled interaction releases native mouse capture");
            check(owner.need_frame, "cancelled interaction schedules a repaint");
            aino::detail::win_feed_input(&state);
            click(owner.ui);
        }
        const LPARAM point = MAKELPARAM(10, 10);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
        aino::detail::win_feed_input(&state);
        frame(owner.ui); check(control(owner.ui, 1).pressed, "native press claims the control"); owner.ui.end_frame();
        check(GetCapture() == window, "native press acquires mouse capture");
        SendMessageW(window, WM_LBUTTONUP, 0, point);
        check(state.btn_released[0] && !state.btn_down[0], "normal ReleaseCapture preserves pending mouse-up");
        aino::detail::win_feed_input(&state);
        frame(owner.ui); check(control(owner.ui, 1).clicked, "normal native mouse-up still clicks"); owner.ui.end_frame();
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
        aino::detail::win_feed_input(&state);
        frame(owner.ui); control(owner.ui, 1); owner.ui.end_frame();
        ReleaseCapture();
        check(!state.btn_down[0] && owner.ui.impl->active == 0,
              "real capture loss while held cancels the active control");
        DestroyWindow(window);
    }
    UnregisterClassW(name, instance);
}
#elif defined(AINO_PLATFORM_X11)
static void native_lifecycle() {
    aino::detail::X11TextRenderer renderer;
    renderer.init(1, "sans", "monospace"); renderer.measure("A", 14, 400);
    const auto before_missing = renderer.cache_stats().faces;
    renderer.measure("\xf4\x8f\xbf\xbf", 14, 400);
    check(renderer.cache_stats().faces == before_missing, "missing glyph does not open unrelated font files");
    renderer.release();
    for (const auto icon : {aino::SystemIcon::Play, aino::SystemIcon::Pause, aino::SystemIcon::PreviousTrack,
                           aino::SystemIcon::NextTrack, aino::SystemIcon::Shuffle, aino::SystemIcon::RepeatOne,
                           aino::SystemIcon::RepeatAll, aino::SystemIcon::Timer}) {
        const auto mask = aino::detail::load_system_icon(icon, 16);
        check(mask.width > 0 && !mask.alpha.empty(), "media icons come from native Linux icon themes");
    }
    aino::Window window(320, 200, "aino capture probe");
    check(window.ok(), "create X11 capture probe window");
    if (!window.ok()) return;
    auto* state = static_cast<aino::detail::X11State*>(window.impl->native);
    XWindowAttributes attributes{};
    const bool queried = XGetWindowAttributes(state->dpy, state->win, &attributes) != 0;
    check(queried && (attributes.your_event_mask & FocusChangeMask), "X11 subscribes to focus-change events");
    window.impl->ui.impl->active = 1; window.impl->need_frame = false;
    for (int i = 0; i < 3; ++i)
        state->btn_down[i] = state->btn_pressed[i] = state->btn_released[i] = true;
    state->key_down[aino::KEY_CTRL] = true;
    XEvent event{};
    event.xfocus.type = FocusOut; event.xfocus.display = state->dpy; event.xfocus.window = state->win;
    event.xfocus.mode = NotifyNormal; event.xfocus.detail = NotifyNonlinear;
    XSendEvent(state->dpy, state->win, False, FocusChangeMask, &event);
    XSync(state->dpy, False);
    bool received = false;
    while (XPending(state->dpy)) {
        XNextEvent(state->dpy, &event);
        if (event.type == FocusOut && event.xfocus.send_event) {
            received = true; aino::detail::x11_handle(state, event);
        }
    }
    check(received, "focus event reaches the subscribed X11 window");
    bool cleared = window.impl->ui.impl->active == 0 && !state->key_down[aino::KEY_CTRL];
    for (int i = 0; i < 3; ++i)
        cleared = cleared && !state->btn_down[i] && !state->btn_pressed[i] && !state->btn_released[i];
    check(cleared, "X11 FocusOut clears active, pending mouse input and held keys");
    check(window.impl->need_frame, "X11 FocusOut schedules a repaint");
    aino::detail::x11_feed_input(state);
    click(window.impl->ui);
}
#else
static void native_lifecycle() {}
#endif

int main() {
    shared_lifecycle();
    overlay_input_checks();
    overlay_menu_lifecycle_checks();
    native_lifecycle();
    std::printf("[capture] %d passed, %d failed\n[capture] verdict: %s\n",
                passed, failed, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
