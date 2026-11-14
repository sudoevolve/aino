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
    std::puts("[window-wake] idle update and background close; verdict: PASS");
    return 0;
}
