#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <string>
static void save() {}
int main() {
    aino::Window win(880, 560, "aino");
    std::string email; bool notify = true;
    win.run([&](aino::Ui& ui) {
        ui.panel({24, 24, 400, 240});
        aino::Stack col = ui.vbox({24, 24, 400, 240}, 10.0f, 18.0f);
        ui.heading("设置", col.next(28), aino::Style().left());
        ui.input(&email, col.next(36), aino::Style().hint("you@example.com"));
        ui.checkbox(&notify, "邮件通知", col.next(22));
        if (ui.button("保存", col.next(36), aino::Style().primary())) save();
    });
    return 0;
}
