#define main aino_gallery_entry
#include "../../examples/gallery.cpp"
#undef main

int main(int argc, char **argv) {
    aino::WindowConfig cfg;
    cfg.width = 1180;
    cfg.height = 760;
    cfg.title = "aino layout check";
    aino::Window win(cfg);
    if (!win.ok()) return 2;
    int passed = 0, failed = 0;
    auto expect = [&](bool ok, const char *name) {
        if (ok) ++passed;
        else { ++failed; std::printf("FAIL: %s\n", name); }
    };
    win.run([&](Ui &ui) {
        App app;
        auto report = [&](const GalleryLayout &layout, float width, std::string &text) {
            std::FILE *f = std::tmpfile();
            if (!f) { expect(false, "temporary report opens"); return -1; }
            const int over = write_layout_report(ui, app, layout, width, f);
            std::rewind(f);
            text.clear();
            char buffer[1024];
            while (size_t size = std::fread(buffer, 1, sizeof(buffer), f)) text.append(buffer, size);
            std::fclose(f);
            return over;
        };
        std::string text;
        for (bool dark : {false, true}) {
            ui.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
            for (float width : {268.f, 612.f, 876.f, 1296.f}) {
                const auto layout = layout_gallery(ui, app, width, width <= 612);
                const int over = report(layout, width, text);
                if (over > 0) std::printf("%s", text.c_str());
                expect(over == 0, "all card bodies fit at responsive widths in both themes");
                expect(text.find("# over=0 total=43\n") != std::string::npos,
                       "summary reports every card and is accepted by CI");
            }
        }
        ui.set_theme(aino::Theme::light());
        const auto layout = layout_gallery(ui, app, 876, false);
        auto broken = layout;
        broken.cards.front().rect.h = 70;
        expect(report(broken, 876, text) > 0, "report rejects clipped card content");
        expect(text.find("OVERFLOW") != std::string::npos && text.find("# over=0 ") == std::string::npos,
               "invalid report contains diagnostics and cannot satisfy the CI gate");
        broken = layout;
        broken.cards.front().rect.x = 900;
        expect(report(broken, 876, text) > 0, "report rejects cards outside the column");
        broken = layout;
        broken.cards[1].rect = broken.cards[0].rect;
        expect(report(broken, 876, text) > 0, "report rejects overlapping cards");
        broken = layout;
        broken.height = 1;
        expect(report(broken, 876, text) > 0, "report rejects cards outside the scroll extent");
        app.date_open = app.color_open = true;
        expect(report(layout, 876, text) == 0, "popovers do not affect body measurements");
        expect(app.date_open && app.color_open && app.hits == 0,
               "report preserves application state");
        if (argc > 1) {
            std::FILE *f = std::fopen(argv[1], "w");
            expect(f != nullptr, "output report opens");
            if (f) {
                expect(write_layout_report(ui, app, layout, 876, f) == 0, "output report has no overflow");
                expect(std::fclose(f) == 0, "output report is written");
            }
        }
        win.close();
    });
    std::printf("[gallery-layout] %d passed, %d failed; verdict: %s\n", passed, failed,
                failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
