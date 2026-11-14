// 不创建窗口，验证通知合并、等待与 drain 并发；三平台共用同一通知实现。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <thread>
#include <chrono>
#include <cstdio>

int main() {
    aino::detail::WakeSignal signal;
    if (!signal.valid() || signal.consume()) return 1;
    for (int i = 0; i < 10000; ++i) signal.notify();
    signal.wait(1000);
    if (!signal.consume() || signal.consume()) return 2;
    std::atomic<int> acknowledged{0};
    std::atomic<bool> failed{false};
    std::thread sender([&] {
        for (int i = 1; i <= 2000; ++i) {
            signal.notify();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (acknowledged.load() < i && !failed.load()) {
                if (std::chrono::steady_clock::now() >= deadline) { failed = true; break; }
                std::this_thread::yield();
            }
            if (failed) return;
        }
    });
    for (int i = 1; i <= 2000 && !failed; ++i) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!signal.consume()) {
            signal.wait(100);
            if (std::chrono::steady_clock::now() >= deadline) { failed = true; break; }
        }
        acknowledged = i;
    }
    sender.join();
    if (failed || acknowledged != 2000) return 3;
    std::puts("[wake] 2000 notifications delivered; verdict: PASS");
    return 0;
}
