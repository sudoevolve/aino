// X11 缩放闪白的不变量断言（对应 Windows 的 resize_probe）。
//
// 要抓的现象：快速改变窗口大小时窗口内成片闪白。
// 成因都在"窗口已经变了尺寸、客户端还没重画"的那段空档里：
//   ① 窗口背景是 WhitePixel。服务器每次改尺寸都先用背景像素重铺整窗，
//      应用重画之前屏幕上就是一片白；
//   ② 尺寸变化后的那一帧还要等 16ms 出帧预算，空档被拉长；
//   ③ 客户端没有应答 _NET_WM_SYNC_REQUEST，合成器不会为它保持上一帧；
//      而应答错了更糟——合成器会一直冻结这个窗口。
//
// 这个探针不去拍屏幕（合成器的行为没法可靠脚本化），而是钉住客户端一侧能确定的事实：
//   ① 服务器补出来的颜色 == 页面 clear() 的颜色。直接问服务器：改完尺寸、还没重画时
//      读新露出区域的像素；
//   ② 尺寸变化处理完之后，出帧预算必须已经到期，下一轮主循环会立刻出帧；
//   ③ 管理器的同步请求被应答成它要的计数器值（扩展协议：帧进行中奇数、画完偶数；
//      旧式协议：恰好等于目标值），请求之后没有跟着配置事件时也应答，且不空转；
//   ④ 帧进行中到达的配置事件不会卡在 Xlib 内部队列里：主循环阻塞前必须先看队列。
//
// 反例：把背景手工改回白色，同一套判据必须读出白色，否则①什么也没证明。
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <atomic>
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <thread>

using namespace aino;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    if (ok) {
        ++g_pass;
        std::printf("  [ok  ] %s %s\n", what, detail);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s %s\n", what, detail);
    }
}

// 与 XSyncApi 同源的 ABI 子集，只多一个读回计数器的入口给断言用。
struct XSyncValue {
    int hi;
    unsigned int lo;
};
using QueryCounter = int (*)(Display*, unsigned long, XSyncValue*);

int main() {
    std::printf("== X11 缩放闪白断言 ==\n");

    // 断言里的坐标按逻辑像素写死，不跟随桌面缩放。
    setenv("AINO_SCALE", "1", 1);
    aino::Window win(aino::WindowConfig{640, 460, "aino resize flash probe"}); // 不写 using：Xlib 也有 Window 类型
    if (!win.ok()) {
        std::printf("window failed\n");
        return 1;
    }
    auto* st = static_cast<detail::X11State*>(win.impl->native);
    Display* dpy = st->dpy;

    // 既不是白、也不是主题底色的页面色，读回时不可能与旧背景混淆。
    const Color kPage = Color::hex(0x2468AC);
    const unsigned long kPagePixel = detail::x11_color_pixel(st->visual, kPage);
    auto frame = [&](Ui& ui) {
        ui.clear(kPage);
    };

    // 改到目标尺寸并等服务器真正应用。有窗口管理器时请求要绕一圈，不能假定立即生效。
    bool resized_ok = true;
    auto resize_to = [&](int w, int h) {
        for (int i = 0; i < 200; ++i) {
            XWindowAttributes wa{};
            XGetWindowAttributes(dpy, st->win, &wa);
            if (wa.width == w && wa.height == h)
                return true;
            XResizeWindow(dpy, st->win, unsigned(w), unsigned(h));
            XFlush(dpy);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    // 读一个像素。尺寸竞态下 XGetImage 会报 BadMatch，由调用方判失败。
    auto pixel_at = [&](int x, int y, unsigned long& out) {
        XImage* image = XGetImage(dpy, st->win, x, y, 1, 1, AllPlanes, ZPixmap);
        if (!image)
            return false;
        out = XGetPixel(image, 0, 0);
        XDestroyImage(image);
        return true;
    };
    auto server_paints_page = [&](int w, int h) {
        unsigned long right = 0, bottom = 0;
        // 落在新增的右边条带和下边条带里：这些位置客户端还没画过，只能是服务器补的。
        return pixel_at(w - 8, h / 2, right) && pixel_at(w / 2, h - 8, bottom) && right == kPagePixel &&
               bottom == kPagePixel;
    };

    win.render(frame); // 背景从此跟随页面底色
    XSync(dpy, False);

    // ---- ① 服务器补出来的是页面底色 ----
    resized_ok = resize_to(700, 500) && resized_ok; // 还没 poll：应用不知道尺寸变了，没有重画
    {
        char d[96];
        std::snprintf(d, sizeof d, "(页面色 0x%lX)", kPagePixel);
        check(server_paints_page(700, 500), "改尺寸后新露出的区域是页面底色", d);
    }

    // ---- ② 尺寸变化之后不等出帧预算 ----
    {
        win.impl->next_frame_at = std::chrono::steady_clock::now() + std::chrono::hours(1); // 假装刚出过一帧
        const bool alive = win.poll(); // 这里处理 ConfigureNotify
        const bool due = win.impl->next_frame_at <= std::chrono::steady_clock::now();
        check(alive && due && win.impl->need_frame, "尺寸变化后出帧预算已到期，下一轮立刻出帧");
    }
    win.render(frame);

    // ---- 反例：背景是白色时，同一套判据读出白色 ----
    {
        XSetWindowBackground(dpy, st->win, WhitePixel(dpy, DefaultScreen(dpy)));
        st->background = WhitePixel(dpy, DefaultScreen(dpy));
        resized_ok = resize_to(760, 540) && resized_ok;
        check(!server_paints_page(760, 540), "反例：背景为 WhitePixel 时读到的不是页面底色");
        win.poll();
        win.render(frame); // 帧末会把背景换回页面底色
    }

    // ---- ③ _NET_WM_SYNC_REQUEST 握手 ----
    QueryCounter query_counter = nullptr;
    if (void* lib = dlopen("libXext.so.6", RTLD_LAZY | RTLD_LOCAL))
        query_counter = reinterpret_cast<QueryCounter>(dlsym(lib, "XSyncQueryCounter"));
    if (!st->sync_counter || !st->sync_counter_ext || !query_counter) {
        std::printf("  [skip ] 没有 XSync（libXext 或 SYNC 扩展不可用），跳过同步握手断言\n");
    } else {
        auto counter = [&](unsigned long id) -> long long {
            XSyncValue value{};
            if (!query_counter(dpy, id, &value))
                return -1;
            return (static_cast<long long>(value.hi) << 32) | value.lo;
        };
        auto request = [&](uint64_t target, bool extended) {
            XEvent e{};
            e.xclient.type = ClientMessage;
            e.xclient.window = st->win;
            e.xclient.message_type = st->wm_protocols;
            e.xclient.format = 32;
            e.xclient.data.l[0] = static_cast<long>(st->sync_request);
            e.xclient.data.l[1] = 0;
            e.xclient.data.l[2] = static_cast<long>(target & 0xffffffffu);
            e.xclient.data.l[3] = static_cast<long>(target >> 32);
            e.xclient.data.l[4] = extended ? 1 : 0;
            XSendEvent(dpy, st->win, False, NoEventMask, &e);
            XFlush(dpy);
        };
        auto report = [&](const char* what, long long got, long long want) {
            char d[80];
            std::snprintf(d, sizeof d, "(计数器 %lld，应为 %lld)", got, want);
            check(got == want, what, d);
        };

        Atom* protocols = nullptr;
        int count = 0;
        XGetWMProtocols(dpy, st->win, &protocols, &count);
        bool advertised = false;
        for (int i = 0; i < count; ++i)
            advertised = advertised || protocols[i] == st->sync_request;
        if (protocols)
            XFree(protocols);
        check(advertised, "WM_PROTOCOLS 声明了 _NET_WM_SYNC_REQUEST");

        // 扩展协议：请求紧跟着配置事件。
        request(1000, true);
        resized_ok = resize_to(820, 580) && resized_ok;
        win.poll();
        check(st->sync_serial == 1000 && st->sync_extended && st->sync_configured,
              "扩展请求已登记并等到了配置事件");
        report("收到请求时扩展计数器为奇数（帧进行中）", counter(st->sync_counter_ext), 1001);
        win.render(frame);
        check(st->sync_serial == 0, "新尺寸的帧提交后请求已应答");
        report("扩展协议画完是偶数", counter(st->sync_counter_ext), 1002);
        report("扩展协议不动旧式计数器", counter(st->sync_counter), 0);

        // 旧式协议：不声明扩展位的窗口管理器。
        request(2000, false);
        resized_ok = resize_to(780, 540) && resized_ok;
        win.poll();
        win.render(frame);
        check(st->sync_serial == 0, "旧式请求已应答");
        report("旧式协议写入目标值", counter(st->sync_counter), 2000);
        report("旧式协议不动扩展计数器", counter(st->sync_counter_ext), 1002);

        // 请求之后没有配置事件：这一帧不是管理器要的尺寸，不能拿去应答；下一帧再应答，不能空转。
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        win.poll(); // 先把上一次缩放留下的事件排空
        win.impl->need_frame = false;
        request(3000, true);
        XSync(dpy, False);
        win.poll();
        win.render(frame);
        check(st->sync_serial == 3000 && win.impl->need_frame, "没等到配置事件：暂不应答并再排一帧");
        win.render(frame);
        check(st->sync_serial == 0 && !win.impl->need_frame, "下一帧照常应答，不再排帧");
        report("无配置事件时扩展协议仍落到偶数", counter(st->sync_counter_ext), 3002);
    }

    // ---- ④ 帧内到达的配置事件：主循环不能睡死 ----
    // libX11 的 XFlush 在发送之后会把 socket 里已有的事件读进内部队列，帧末的 XFlush 就在做
    // 这件事。之后这些事件不再出现在 fd 上，只等 fd 的主循环会一直睡到下一个无关事件到来。
    {
        std::atomic<bool> drew{false};
        int step = 0;
        std::thread watchdog([&] { // 修复缺失时 run() 不会自己返回，由看门狗收尾并判失败
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            win.close();
        });
        win.run([&](Ui& ui) {
            ui.clear(kPage);
            if (step++ == 0) {
                // 这一帧进行中到达一次尺寸变化。XSync 等回复时同样把事件读进 Xlib 队列；
                // 用合成事件不经过窗口管理器，有没有 WM 结果都一样。
                XEvent e{};
                e.xconfigure.type = ConfigureNotify;
                e.xconfigure.event = st->win;
                e.xconfigure.window = st->win;
                e.xconfigure.width = 612;
                e.xconfigure.height = 432;
                XSendEvent(dpy, st->win, False, StructureNotifyMask, &e);
                XSync(dpy, False);
            } else if (ui.width() == 612.0f) {
                drew = true;
                win.close();
            }
        });
        watchdog.join();
        check(drew, "帧内到达的配置事件在主循环里被处理，没有睡死");
    }

    check(resized_ok, "所有尺寸变化都被服务器应用");
    std::printf("[resize-flash] 通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
