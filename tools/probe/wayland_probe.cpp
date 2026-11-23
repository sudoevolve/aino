// 发布时在独立 Weston 会话中运行；日常只编译此目标。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <chrono>
#include <thread>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

uint64_t checksum(const aino::detail::WaylandState::Buffer& buffer) {
    const auto* bytes = static_cast<const uint8_t*>(buffer.pixels);
    uint64_t value = 14695981039346656037ull;
    for (size_t i = 0; i < buffer.bytes; ++i) value = (value ^ bytes[i]) * 1099511628211ull;
    return value;
}
} // namespace

int main() {
    aino::WindowConfig config;
    config.width = 640;
    config.height = 480;
    config.min_width = 320;
    config.min_height = 200;
    config.title = "aino native Wayland lifecycle";
    aino::Window window(config);
    if (!window.ok()) return 2;
    auto* state = static_cast<aino::detail::WaylandState*>(window.impl->native);
    check(!window.try_set_clipboard("no serial"), "clipboard write requires an input serial");

    int explicit_frames = 0;
    window.render([&](aino::Ui& ui) {
        ++explicit_frames;
        ui.clear(aino::Color::hex(0x123456));
    });
    check(explicit_frames == 1, "explicit render invokes its callback before the first configure");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (state->buffers.empty() && window.poll() && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{aino::detail::wl::api().get_fd(state->display), POLLIN, 0};
        ::poll(&descriptor, 1, 20);
    }
    check(!state->buffers.empty(), "configured surface presents a shared-memory buffer");
    if (state->buffers.empty()) return 3;
    auto* attached = state->buffers.front().get();
    const uint64_t before = checksum(*attached);
    // 不派发 compositor 事件；显式重绘不能覆盖仍被服务器持有的帧。
    for (int i = 0; i < 8; ++i) {
        window.render([&](aino::Ui& ui) {
            ++explicit_frames;
            ui.clear(aino::Color::rgba(90 + i, 20, 30));
        });
    }
    check(explicit_frames == 9, "presentation pacing does not drop explicit drawing callbacks");
    check(checksum(*attached) == before, "queued CPU drawing preserves the attached buffer bytes");
    check(state->buffers.size() <= 3, "undispatched drawing cannot grow the shared buffer pool");

    bool resize_requested = false;
    bool resized = false;
    int frames = 0;
    std::thread timeout([&] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        window.close();
    });
    aino::detail::ScopeExit join([&] { timeout.join(); });
    window.run([&](aino::Ui& ui) {
        ++frames;
        ui.clear(aino::Color::hex(0xf5f5f7));
        ui.label("Wayland 中文 / CPU", {20, 20, 240, 30});
        if (!resize_requested) {
            const auto* pixels = window.impl->canvas.px;
            window.set_size(480, 320);
            check(window.impl->canvas.px == pixels, "set_size inside a frame preserves its drawing storage");
            resize_requested = true;
        } else {
            resized = window.width() == 480 && window.height() == 320;
            check(ui.width() == window.width() && ui.height() == window.height(), "content coordinates exclude window decorations");
            check(state->buffers.size() <= 3, "resize preserves the shared buffer limit");
            if (resized) window.close();
        }
    });
    check(frames >= 2 && resized, "resize schedules a later frame without continuous repaint");
    std::printf("[wayland] %d failures\n", failures);
    return failures ? 1 : 0;
}
