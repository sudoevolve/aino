// 把 default_ui_font_family() 选中的字面名打出来 —— 别靠看字形猜。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
int main() {
    std::printf("picked = %s\n", aino::detail::default_ui_font_family().c_str());
    std::printf("Segoe UI Variable Text   installed=%d\n",
                int(aino::detail::font_face_installed(L"Segoe UI Variable Text")));
    std::printf("Segoe UI                 installed=%d\n",
                int(aino::detail::font_face_installed(L"Segoe UI")));
    std::printf("Microsoft YaHei UI       installed=%d\n",
                int(aino::detail::font_face_installed(L"Microsoft YaHei UI")));
    return 0;
}
