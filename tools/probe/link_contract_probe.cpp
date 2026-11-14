// 与 link_contract_impl.cpp 分别编译并链接，验证单实现单元接入。
#include "../../aino.hpp"
#include <type_traits>
#include <cstdio>

static_assert(!std::is_copy_constructible<aino::Ui>::value, "Ui owns its implementation");
static_assert(!std::is_copy_assignable<aino::Ui>::value, "Ui cannot share implementation ownership");
static_assert(!std::is_move_constructible<aino::Ui>::value, "Ui scopes borrow its address");
static_assert(!std::is_move_assignable<aino::Ui>::value, "Ui scopes borrow its address");
static_assert(!std::is_copy_constructible<aino::Window>::value, "Window owns native resources");
static_assert(!std::is_copy_assignable<aino::Window>::value, "Window owns native resources");
static_assert(!std::is_move_constructible<aino::Window>::value, "Window callbacks borrow its address");
static_assert(!std::is_move_assignable<aino::Window>::value, "Window callbacks borrow its address");

int main() {
    aino::Ui ui;
    ui.set_theme(aino::Theme::dark());
    ui.begin_frame(320, 240, .016f, 1);
    {
        auto owner = ui.state_scope(7);
        auto stack = ui.vbox({0, 0, 100, 100}, 4, 8);
        if (stack.next(20).h != 20 || stack.fill().empty()) return 1;
        auto scroll = ui.scroll({0, 0, 80, 80}, 160);
        scroll.to(10);
    }
    ui.end_frame();
    ui.release_state_scope(7);
    if (ui.width() != 320 || ui.height() != 240 || !ui.theme().is_dark()) return 2;
    // 取函数地址强制验证 Window 与工作进程 API 的外部符号，不创建窗口。
    auto title = &aino::Window::set_title;
    auto wake = &aino::Window::wake;
    auto worker = &aino::system_icon_worker_entry;
    if (!title || !wake || !worker || !aino::icons::check) return 3;
    std::puts("[link-contract] verdict: PASS");
    return 0;
}
