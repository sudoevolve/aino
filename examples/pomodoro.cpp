#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using aino::Rect;
using aino::Style;
using Clock = std::chrono::steady_clock;

struct Pomodoro {
    int mode = 0;
    float minutes[3] = {25, 5, 15};
    double remaining = 25 * 60;
    bool running = false;
    int completed = 0;
    Clock::time_point deadline{};

    double duration() const { return double(minutes[mode]) * 60; }

    void reset() {
        running = false;
        remaining = duration();
    }

    void select(int next) {
        if (mode == next) return;
        mode = next;
        reset();
    }

    void update(Clock::time_point now) {
        if (!running) return;
        remaining = std::max(0.0, std::chrono::duration<double>(deadline - now).count());
        if (remaining == 0) {
            running = false;
            if (mode == 0) ++completed;
        }
    }

    void toggle(Clock::time_point now) {
        update(now);
        if (running) {
            running = false;
        } else {
            if (remaining == 0) remaining = duration();
            deadline = now + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(remaining));
            running = true;
        }
    }
};

// 后台只负责唤醒窗口，计时状态由 UI 线程独占；暂停和退出都能立即打断等待。
class SecondTicker {
    aino::Window& window;
    std::mutex mutex;
    std::condition_variable changed;
    bool active = false, quit = false;
    std::thread worker;

public:
    explicit SecondTicker(aino::Window& win) : window(win), worker([this] {
        std::unique_lock<std::mutex> lock(mutex);
        while (!quit) {
            changed.wait(lock, [&] { return active || quit; });
            if (quit) break;
            if (!changed.wait_for(lock, std::chrono::seconds(1), [&] { return !active || quit; }))
                window.wake();
        }
    }) {}

    ~SecondTicker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quit = true;
        }
        changed.notify_one();
        worker.join();
    }

    void set_running(bool running) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (active == running) return;
            active = running;
        }
        changed.notify_one();
    }
};

void draw_pomodoro(aino::Ui& ui, Pomodoro& timer, bool& dark, Clock::time_point now) {
    timer.update(now);
    const auto& theme = ui.theme();
    const char* modes[] = {"专注", "短休息", "长休息"};
    const float sidebar = 200;
    const float width = ui.width(), height = ui.height();
    ui.clear((theme.window_bg.luminance() < .5f ? theme.window_bg : aino::Color::hex(0xF5F5F7)));
    ui.fill_rect(Rect(0, 0, sidebar, height), theme.sidebar_bg);
    ui.fill_rect(Rect(sidebar - 1, 0, 1, height), theme.separator);
    ui.fill_rect({20, 22, 32, 32}, theme.tint.scaled_alpha(.13f), 8);
    ui.system_icon(aino::SystemIcon::Timer, {27, 29, 18, 18}, theme.tint);
    ui.label("番茄钟", {64, 20, 116, 23}, Style().font(16).semi_bold());
    ui.draw_text("留一点时间给专注", {64, 45, 116, 17}, theme.label2, Style().font(11));

    ui.muted_text("计时器", Rect(20, 76, 160, 18), Style().font(11).semi_bold().left());
    auto nav = ui.vbox(Rect(10, 102, sidebar - 20, 92), 3);
    for (int i = 0; i < 3; ++i) {
        const Rect row = nav.next(28);
        if (ui.nav_item(modes[i], timer.mode == i, row, Style().font(13).padding(36))) timer.select(i);
        ui.system_icon(i == 0 ? aino::SystemIcon::Loader : aino::SystemIcon::Sun,
                       {row.x + 12, row.cy() - 8, 16, 16}, timer.mode == i ? theme.tint : theme.label2);
    }
    char completed[48];
    std::snprintf(completed, sizeof(completed), "已完成 %d 次专注", timer.completed);
    ui.muted_text(completed, Rect(20, height - 46, 160, 22), Style().font(12).left());

    const Rect toolbar(sidebar, 0, width - sidebar, 60);
    ui.fill_rect(toolbar, (theme.window_bg.luminance() < .5f ? theme.window_bg : aino::Color::hex(0xF5F5F7)));
    ui.label(modes[timer.mode], Rect(sidebar + 24, 18, 160, 24),
             Style().font(22).semi_bold().left());
    const Rect appearance(width - 48, 18, 24, 24);
    if (ui.icon_button("", appearance, Style().ghost())) dark = !dark;
    ui.system_icon(dark ? aino::SystemIcon::Sun : aino::SystemIcon::Moon,
                   appearance.inset(4), theme.label);
    ui.tooltip(appearance, "切换外观");

    const Rect settings(sidebar, height - 84, width - sidebar, 84);
    ui.panel({settings.x + 24, settings.y, settings.w - 48, 68}, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
    ui.label("本轮时长", Rect(settings.x + 40, settings.y + 14, 100, 24), Style().left());
    const Rect stepper(settings.right() - 154, settings.y + 22, 88, 24);
    Style edit;
    if (timer.running) edit.off();
    if (ui.stepper(&timer.minutes[timer.mode], 1, 1, 120, stepper, edit)) timer.reset();
    ui.muted_text("分钟", Rect(stepper.right() + 10, stepper.y, 40, 24), Style().left());
    ui.muted_text(timer.running ? "暂停后可调整时长" : "完成后手动选择下一轮",
                  Rect(settings.x + 40, settings.y + 40, settings.w - 210, 18),
                  Style().font(11).left());

    const Rect content(sidebar, toolbar.bottom(), width - sidebar, settings.y - toolbar.bottom());
    ui.panel({content.x + 24, content.y + 18, content.w - 48, content.h - 36}, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
    const Rect ring = Rect::centered(content.cx(), content.cy() - 28, 220, 220);
    const float buttons_y = ring.bottom() + 28;
    if (ui.button("重置", Rect(content.cx() - 100, buttons_y, 88, 28), Style().standard()))
        timer.reset();
    const char* action = timer.running ? "暂停" : timer.remaining == 0 ? "再来一轮"
                        : timer.remaining < timer.duration() ? "继续" : "开始";
    if (ui.button(action, Rect(content.cx(), buttons_y, 100, 28))) timer.toggle(now);

    ui.progress_ring(float(timer.remaining / timer.duration()), ring, Style().stroke(4, theme.track));
    const int seconds = int(std::ceil(timer.remaining));
    char time[16];
    std::snprintf(time, sizeof(time), "%02d:%02d", seconds / 60, seconds % 60);
    ui.draw_mono_text(time, ring.cx() - ui.mono_width(time, 48) * 0.5f,
                      ring.cy() - 36, theme.label, 48);
    const char* status = timer.running ? "计时中" : timer.remaining == 0 ? "本轮已完成"
                        : timer.remaining < timer.duration() ? "已暂停" : "准备开始";
    ui.muted_text(status, Rect(ring.x, ring.cy() + 28, ring.w, 20), Style().center());
}

int main() {
#if defined(AINO_PLATFORM_X11)
    // X11 的后台 wake() 需要在首次建窗前启用线程支持。
    if (!XInitThreads()) return 1;
#endif
    aino::WindowConfig cfg;
    cfg.width = 880;
    cfg.height = 600;
    cfg.min_width = 720;
    cfg.min_height = 520;
    cfg.title = "aino · 番茄钟";
    aino::Window win(cfg);
    if (!win.ok()) return 1;

    const char* env = std::getenv("AINO_DARK");
    bool dark = env && std::strcmp(env, "1") == 0;
    win.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
    Pomodoro timer;
    SecondTicker ticker(win);
    win.run([&](aino::Ui& ui) {
        draw_pomodoro(ui, timer, dark, Clock::now());
        win.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
        ticker.set_running(timer.running);
    });
    return 0;
}
