// 空闲 CPU 归因探针：逐个开组件，看谁在"一直要帧"。
// 用法：cpu_probe.exe <mode>   mode: 0=什么都不画 1=spinner 2=skeleton 3=纯静态页
//     4=不停 set_theme 5=textarea 获焦（光标闪烁）
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const int mode = argc > 1 ? std::atoi(argv[1]) : 0;

    aino::Window win(400, 300, "cpu probe");
    if (!win.ok()) return 1;

    std::string txt = "hi";
    float t = 0.0f;

    win.run([&](aino::Ui& ui) {
        if (mode == 4) ui.set_theme(t < 0.5f ? aino::Theme::light() : aino::Theme::dark());
        ui.clear(ui.theme().background);
        switch (mode) {
            case 0: ui.label("idle", { 10, 10, 100, 20 }); break;
            case 1: ui.spinner({ 20, 20, 40, 40 }); break;
            case 2: ui.skeleton({ 20, 20, 200, 20 }); break;
            case 3: ui.panel({ 20, 20, 300, 200 }); ui.label("static", { 30, 30, 100, 20 }); break;
            case 4: ui.label("theme", { 10, 10, 100, 20 }); break;
            case 5: ui.textarea(&txt, { 20, 20, 300, 120 }); break;
            default: break;
        }
    });
    return 0;
}
