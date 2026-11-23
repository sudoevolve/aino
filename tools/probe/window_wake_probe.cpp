// 真窗口按需主循环：后台更新必须叫醒空闲窗口，后台关闭必须结束阻塞。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <condition_variable>
#include <memory>

int main() {
#if defined(_WIN32)
    // Windows 不允许删除仍由 CRT 打开的文件，分别检查替换和析构两条释放路径。
    const auto temp = std::filesystem::temp_directory_path();
    const auto suffix = std::to_string(GetCurrentProcessId());
    const std::string first = (temp / ("aino-frame-" + suffix + ".log")).string();
    const std::string second = (temp / ("aino-prof-" + suffix + ".log")).string();
    const char* old_frame = std::getenv("AINO_FRAME_LOG");
    const char* old_prof = std::getenv("AINO_PROF");
    const std::string frame_env = old_frame ? old_frame : "", prof_env = old_prof ? old_prof : "";
    _putenv_s("AINO_FRAME_LOG", first.c_str());
    _putenv_s("AINO_PROF", second.c_str());
    bool replaced_closed = false;
    {
        aino::Window logged(aino::WindowConfig{320, 200, "aino log lifetime probe"});
        replaced_closed = logged.ok() && std::remove(first.c_str()) == 0;
    }
    const bool destroyed_closed = std::remove(second.c_str()) == 0;
    _putenv_s("AINO_FRAME_LOG", frame_env.c_str());
    _putenv_s("AINO_PROF", prof_env.c_str());
    if (!replaced_closed || !destroyed_closed) return 6;
#endif
    aino::Window window(aino::WindowConfig{320, 200, "aino window wake probe"});
    if (!window.ok()) return 1;
    window.render([](aino::Ui& ui) { ui.clear(ui.theme().window_bg); });
    const auto* old_pixels = window.impl->canvas.px;
    const int old_width = window.impl->canvas.w, old_height = window.impl->canvas.h;
#if defined(AINO_PLATFORM_WINDOWS)
    auto* surface = static_cast<aino::detail::Win32State*>(window.impl->native);
    aino::detail::win_create_backbuffer(surface, 16385, 1);
    aino::detail::win_create_backbuffer(surface, 16384, 16384);
#elif defined(AINO_PLATFORM_X11)
    auto* surface = static_cast<aino::detail::X11State*>(window.impl->native);
    aino::detail::x11_create_backbuffer(surface, 16385, 1);
    aino::detail::x11_create_backbuffer(surface, 16384, 16384);
#elif defined(AINO_PLATFORM_MACOS)
    auto* surface = static_cast<aino::detail::CocoaState*>(window.impl->native);
    aino::detail::cocoa_create_backbuffer(surface, 16385, 1);
    aino::detail::cocoa_create_backbuffer(surface, 16384, 16384);
    aino::WindowConfig unscaled_config;
    unscaled_config.high_dpi = false;
    aino::Window unscaled(unscaled_config);
    if (!unscaled.ok() || unscaled.impl->scale != 1.0f) return 9;
#endif
#if !defined(AINO_PLATFORM_WAYLAND)
    if (window.impl->canvas.px != old_pixels || window.impl->canvas.w != old_width || window.impl->canvas.h != old_height)
        return 9;
#else
    (void)old_pixels; (void)old_width; (void)old_height;
#endif
#if defined(AINO_PLATFORM_X11)
    {
        aino::Window owner(aino::WindowConfig{160, 100, "clipboard owner"});
        aino::Window reader(aino::WindowConfig{160, 100, "clipboard reader"});
        if (!owner.ok() || !reader.ok() || !owner.try_set_clipboard("async UTF-8 \xE4\xB8\xAD")) return 8;
        bool requested = false, delivered = false, owner_serviced = false;
        int owner_frames = 0;
        window.set_frame_callback([&](aino::Ui& ui) {
            ui.clear(ui.theme().window_bg);
            ++owner_frames;
            ui.request_frame_in(.02f);
        });
        std::mutex mutex;
        std::condition_variable done;
        bool finished = false;
        std::thread timeout([&] {
            std::unique_lock<std::mutex> lock(mutex);
            if (!done.wait_for(lock, std::chrono::seconds(5), [&] { return finished; })) reader.close();
        });
        const auto started = std::chrono::steady_clock::now();
        reader.run([&](aino::Ui& ui) {
            ui.clear(ui.theme().window_bg);
            if (!requested) {
                requested = true;
                reader.request_clipboard([&](std::string text) {
                    delivered = text == "async UTF-8 \xE4\xB8\xAD";
                    reader.close();
                });
            }
            // 选区 owner 刻意晚 120ms 服务请求；期间另一个已注册窗口必须继续绘制。
            if (!owner_serviced && std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(120)) {
                owner_serviced = true;
                owner.poll();
            }
            ui.request_frame_in(.02f);
        });
        window.set_frame_callback({});
        { std::lock_guard<std::mutex> lock(mutex); finished = true; }
        done.notify_one(); timeout.join();
        if (!delivered || owner_frames < 3 || std::chrono::steady_clock::now() - started >= std::chrono::seconds(2)) {
            std::printf("[window-wake] async selection delivered=%d sibling_frames=%d\n", int(delivered), owner_frames);
            return 8;
        }
    }
#endif
#if defined(_WIN32)
    {
        aino::Window sibling(aino::WindowConfig{320, 200, "aino sibling close probe"});
        if (!sibling.ok()) return 7;
        auto* native = static_cast<aino::detail::Win32State*>(sibling.impl->native);
        SendMessageW(native->hwnd, WM_CLOSE, 0, 0);
        if (!sibling.should_close() || !window.poll() || window.should_close()) {
            std::puts("[window-wake] closing a sibling stopped the surviving window");
            return 7;
        }
    }
#endif
    int manual_frames = 0;
    window.render([&](aino::Ui& ui) { ui.clear(ui.theme().window_bg); ++manual_frames; });
    window.render([&](aino::Ui& ui) {
        ++manual_frames;
        ui.input(nullptr, {0, 0, 100, 30});
        window.render([&](aino::Ui&) { manual_frames += 100; }); // 不允许帧内重入。
    });
    if (manual_frames != 2 || window.impl->frame_cb_ptr) return 5;
    window.request_repaint(); // 手动绘制已消费首帧；测试主循环前显式请求新帧。
    std::atomic<int> frames{0};
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        auto await_frame = [&](int count) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (frames.load() < count) {
                if (std::chrono::steady_clock::now() >= deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        };
        if (!await_frame(1)) failed = true;
        for (int i = 2; i <= 21 && !failed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            window.wake();
            if (!await_frame(i)) failed = true;
        }
        window.close();
    });
    window.run([&](aino::Ui& ui) { ui.clear(ui.theme().window_bg); ++frames; });
    producer.join();
    if (failed || frames < 21) {
        std::printf("[window-wake] frames=%d timeout=%d\n", frames.load(), int(failed.load()));
        return 2;
    }
    int caret_frames = 0;
    std::string text = "caret";
    aino::Window caret_window(aino::WindowConfig{320, 200, "aino caret timer probe"});
    if (!caret_window.ok()) return 3;
    std::thread closer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2400));
        caret_window.close();
    });
    caret_window.run([&](aino::Ui& ui) {
        if (!caret_frames) {
            auto id = ui.impl->widget_id("input", {20, 20, 220, 36}, 0,
                uint64_t(reinterpret_cast<uintptr_t>(&text)));
            aino::detail::grab_focus(ui.impl, id);
        }
        ui.input(&text, {20, 20, 220, 36});
        ++caret_frames;
    });
    closer.join();
    std::printf("[caret-timer] %d frames in 2.4 seconds\n", caret_frames);
    if (caret_frames < 5 || caret_frames > 30) return 4;
    // 同一 UI Thread 动态创建子窗口；延迟帧、关闭通知、回收都由一个 run() 驱动。
    {
        aino::Window parent(aino::WindowConfig{320, 200, "aino scheduled parent probe"});
        if (!parent.ok()) return 9;
        std::unique_ptr<aino::Window> child;
        int child_frames = 0;
        bool collected = false;
        std::mutex completion_mutex;
        std::condition_variable completion;
        bool done = false;
        std::thread watchdog([&] {
            std::unique_lock<std::mutex> lock(completion_mutex);
            if (!completion.wait_for(lock, std::chrono::seconds(5), [&] { return done; })) parent.close();
        });
        parent.run([&](aino::Ui& ui) {
            ui.clear(ui.theme().window_bg);
            if (!child) {
                child = std::make_unique<aino::Window>(aino::WindowConfig{320, 200, "aino scheduled child probe"});
                if (!child->ok()) { parent.close(); return; }
                child->set_frame_callback([&](aino::Ui& child_ui) {
                    child_ui.clear(child_ui.theme().window_bg);
                    if (++child_frames == 2) child->close();
                    else child_ui.request_frame_in(.03f);
                });
            } else if (child->should_close()) {
                child.reset(); collected = true; parent.close();
            }
        });
        { std::lock_guard<std::mutex> lock(completion_mutex); done = true; }
        completion.notify_one(); watchdog.join();
        if (!collected || child_frames != 2) return 9;
    }
#if defined(_WIN32)
    aino::Window quit_window(aino::WindowConfig{320, 200, "aino explicit quit probe"});
    if (!quit_window.ok()) return 8;
    PostQuitMessage(0);
    if (quit_window.poll() || !quit_window.should_close()) {
        std::puts("[window-wake] explicit WM_QUIT was ignored");
        return 8;
    }
#endif
    std::puts("[window-wake] idle update and background close; verdict: PASS");
    return 0;
}
