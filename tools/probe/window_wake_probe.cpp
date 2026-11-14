// 真窗口按需主循环：后台更新必须叫醒空闲窗口，后台关闭必须结束阻塞。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>

int main() {
    aino::Window window(320, 200, "aino window wake probe");
    if (!window.ok()) return 1;
    int manual_frames = 0;
    window.render([&](aino::Ui& ui) { ui.clear(ui.theme().background); ++manual_frames; });
    window.render([&](aino::Ui& ui) {
        ++manual_frames;
        ui.input(nullptr, {0, 0, 100, 30});
        window.render([&](aino::Ui&) { manual_frames += 100; }); // 不允许帧内重入。
    });
    if (manual_frames != 2 || window.impl->frame_cb_ptr) return 5;
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
    window.run([&](aino::Ui& ui) { ui.clear(ui.theme().background); ++frames; });
    producer.join();
    if (failed || frames < 21) return 2;
    int caret_frames = 0;
    std::string text = "caret";
    aino::Window caret_window(320, 200, "aino caret timer probe");
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
    std::puts("[window-wake] idle update and background close; verdict: PASS");
    return 0;
}
