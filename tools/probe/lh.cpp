#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
int main() {
    aino::detail::GdiTextRenderer tr;
    tr.init(1.0f, "", "");
    const float sizes[] = {11,11.5f,12,13,14,16,18,20,22,24};
    for (float s : sizes)
        std::printf("fs %.1f -> line_height %.2f  measure(\"解析配置\")=%.2f\n",
                    s, tr.line_height(s, 400), tr.measure("\xE8\xA7\xA3\xE6\x9E\x90\xE9\x85\x8D\xE7\xBD\xAE", s, 400));
    return 0;
}
