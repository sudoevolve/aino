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
    native_lifecycle();
    std::printf("[capture] %d passed, %d failed\n[capture] verdict: %s\n",
                passed, failed, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
