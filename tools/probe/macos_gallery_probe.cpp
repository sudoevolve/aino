// Exercise the real gallery demos: component-only probes cannot catch layout or
// application state being reset by a demo on every frame.
#define main aino_gallery_entry
#include "../../examples/gallery.cpp"
#undef main

#include <functional>
#include <type_traits>
#include <utility>

namespace {
template<class T, class = void> struct HasPublicRepaint : std::false_type {};
template<class T> struct HasPublicRepaint<T, std::void_t<decltype(std::declval<T&>().repaint())>>
    : std::true_type {};
static_assert(!HasPublicRepaint<Ui>::value, "repaint must not be part of the public API");

int passed = 0, failed = 0;
void expect(bool ok, const char *name) {
    if (ok)
        ++passed;
    else {
        ++failed;
        std::printf("[FAIL] %s\n", name);
    }
}
bool approximately(float a, float b) { return std::fabs(a - b) < 0.01f; }
bool equal(Color a, Color b) { return a.rgb24() == b.rgb24() && a.a == b.a; }

struct Fixture {
    float width = 1180, height = 760;
    Ui ui;
    aino::detail::GdiTextRenderer text;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr, previous = nullptr;
    aino::detail::Canvas canvas;
    Fixture() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 1400;
        info.bmiHeader.biHeight = -1000;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *pixels = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        previous = HBITMAP(SelectObject(dc, bitmap));
        canvas.px = static_cast<uint8_t *>(pixels);
        canvas.w = 1400;
        canvas.h = 1000;
        canvas.stride = 1400 * 4;
        canvas.platform_dc = dc;
        text.init(1.0f, {}, {});
        ui.impl->canvas = &canvas;
        ui.impl->text = &text;
        ui.set_theme(aino::Theme::light());
    }
    ~Fixture() {
        text.release();
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    void frame(const std::function<void()> &draw, float x = -100, float y = -100, bool press = false,
               bool release = false, float wheel = 0, float scale = 1) {
        ui.impl->input = aino::Input{};
        ui.impl->input.mouse = Vec2(x, y);
        ui.impl->input.down[0] = press;
        ui.impl->input.pressed[0] = press;
        ui.impl->input.released[0] = release;
        ui.impl->input.wheel = wheel;
        ui.begin_frame(width, height, 1.0f / 60.0f, scale);
        ui.clear(ui.theme().background);
        draw();
        ui.end_frame();
    }
    void click(float x, float y, const std::function<void()> &draw) {
        frame(draw, x, y, true);
        frame(draw, x, y, false, true);
    }
    void key(int key, const std::function<void()> &draw, bool ctrl = false) {
        ui.impl->input = aino::Input{};
        ui.impl->input.key_pressed[key] = true;
        ui.impl->input.key_ctrl[key] = ctrl;
        ui.impl->input.key_down[aino::KEY_CTRL] = ctrl;
        ui.begin_frame(width, height, 1.f / 60, 1);
        draw();
        ui.end_frame();
    }
    void type(const std::string &text, const std::function<void()> &draw) {
        ui.impl->input = aino::Input{};
        ui.impl->input.text_input = text;
        ui.begin_frame(width, height, 1.f / 60, 1);
        draw();
        ui.end_frame();
    }
    Color pixel(int x, int y) const {
        const auto *p = canvas.px + y * canvas.stride + x * 4;
        return Color(p[aino::detail::CH_R], p[aino::detail::CH_G], p[aino::detail::CH_B]);
    }
    Rect measure(App &app, void (*draw)(Ui &, App &, const Rect &), Rect box) {
        ui.begin_measure();
        draw(ui, app, box);
        return ui.end_measure();
    }
};
} // namespace

int main() {
    Fixture f;
    App app;
    f.frame([] {});
    {
        int requests = 0;
        f.ui.impl->request_repaint = [&] { ++requests; };
        expect(f.ui.request_next_frame() && requests == 1,
               "one-shot frame request works outside the frame callback");
        expect(!f.ui.request_animation_frame() && requests == 1,
               "animation frame request requires an active frame");
        f.ui.begin_frame(f.width, f.height, 1.f / 60.f, 1.f);
        expect(!f.ui.request_next_frame() && requests == 1,
               "one-shot frame request is rejected during a frame callback");
        expect(f.ui.request_animation_frame() && requests == 2 && !f.ui.animating(),
               "explicit animation request schedules one frame without enabling continuous mode");
        f.ui.begin_measure();
        expect(!f.ui.request_animation_frame() && !f.ui.request_next_frame() && requests == 2,
               "measurement cannot request frames through public APIs");
        f.ui.set_animating(true);
        expect(!f.ui.animating() && requests == 2,
               "measurement cannot enable continuous animation");
        f.ui.end_measure();
        f.ui.deferred([&] {
            expect(!f.ui.request_next_frame(), "deferred callbacks cannot request general repaint");
        });
        f.ui.end_frame();
        f.frame([] {});
        expect(requests == 2 && !f.ui.animating(), "one-shot animation does not persist across frames");
        f.ui.impl->request_repaint = {};
    }
    {
        Fixture idle;
        int requests = 0;
        idle.ui.impl->request_repaint = [&] { ++requests; };
        for (const auto &card : kCards) {
            App state;
            auto draw = [&] { draw_body(idle.ui, state, card.id, {20, 20, 360, 400}); };
            for (int i = 0; i < 64; ++i) idle.frame(draw);
            requests = 0;
            idle.frame(draw);
            if (requests || idle.ui.animating()) std::printf("  non-idle card: %s\n", card.name);
            expect(requests == 0 && !idle.ui.animating(), "settled controls never request idle frames");
        }
        int selected = 0;
        const std::vector<std::string> items{"one", "two"};
        auto select = [&] { idle.ui.select(items, &selected, {20, 20, 160, 24}); };
        idle.click(60, 32, select);
        for (int i = 0; i < 64; ++i) idle.frame(select);
        requests = 0;
        idle.frame(select);
        expect(requests == 0, "open select remains idle until an input event");
        idle.click(60, 88, select);
        expect(selected == 1, "idle open select still accepts item selection");
    }
    {
        bool on = true, off = false, checked = true;
        int choice = 0, segment = 1;
        float offset = 0;
        std::array<uint64_t, 3> ids{}, initial_ids{};
        auto scrolling = [&] {
            auto scroll = f.ui.scroll({20, 20, 600, 450}, 1400);
            scroll.to(offset);
            Rect a{40, 140 - offset, 54, 24}, b{40, 184 - offset, 54, 24};
            Rect check{140, 140 - offset, 200, 24};
            ids = {f.ui.impl->widget_id("toggle", a), f.ui.impl->widget_id("toggle", b),
                   f.ui.impl->widget_id("checkbox", check, aino::detail::hash_string("checked"))};
            f.ui.toggle(&on, a);
            f.ui.toggle(&off, b);
            f.ui.checkbox(&checked, "checked", check);
            f.ui.radio_group({"one", "two"}, &choice, {40, 240 - offset, 200, 72});
            f.ui.segmented({"one", "two"}, &segment, {270, 240 - offset, 220, 24});
        };
        f.frame(scrolling);
        initial_ids = ids;
        expect(f.ui.impl->anim[ids[0] * 41u + 1u] == 1,
               "initial checked switch is rendered on without an entrance animation");
        const size_t animation_count = f.ui.impl->anim.size();
        for (float position : {44.f, 88.f, 22.5f, 100.25f, .173f, .865f, 21.317f, 100.983f, 0.f}) {
            offset = position;
            f.frame(scrolling);
            expect(ids == initial_ids, "scroll preserves widget identity when "
                                       "opposite states cross screen positions");
            expect(f.ui.impl->anim[ids[0] * 41u + 1u] == 1 && f.ui.impl->anim[ids[1] * 41u + 1u] == 0 &&
                       f.ui.impl->anim[ids[2] * 37u + 1u] == 1,
                   "scroll never replays or swaps switch and checkbox state animations");
            expect(on && !off && checked && choice == 0 && segment == 1,
                   "scroll leaves all bound values unchanged");
        }
        expect(f.ui.impl->anim.size() == animation_count,
               "scroll does not allocate new widget animation states on each wheel "
               "step");
        auto measuring = [&] {
            const size_t count = f.ui.impl->anim.size();
            f.ui.begin_measure();
            bool selected = true;
            f.ui.toggle(&selected, {25, 25, 54, 24});
            f.ui.checkbox(&selected, "measure only", {110, 25, 160, 24});
            f.ui.end_measure();
            expect(f.ui.impl->anim.size() == count, "measurement never creates phantom animation states");
        };
        f.frame(measuring);
        uint64_t inner_id = 0, nested_id = 0;
        float outer_offset = 0, inner_offset = 0;
        auto nested = [&] {
            auto outer = f.ui.scroll({20, 20, 600, 450}, 1400);
            outer.to(outer_offset);
            auto inner = f.ui.scroll({70, 160 - outer_offset, 320, 230}, 700);
            inner.to(inner_offset);
            nested_id = inner.id;
            Rect r{90, 190 - outer_offset - inner_offset, 54, 24};
            inner_id = f.ui.impl->widget_id("toggle", r);
            f.ui.toggle(&on, r);
        };
        f.frame(nested);
        const uint64_t first_inner = inner_id, first_nested = nested_id;
        outer_offset = 44;
        inner_offset = 22;
        f.frame(nested);
        expect(first_inner == inner_id && first_nested == nested_id,
               "nested scroll areas and their controls keep stable identities");
    }
    f.frame([] {});
    f.frame([] {});
    {
        App motion;
        auto progress = [&] { draw_progress(f.ui, motion, Progress, {20, 20, 360, 160}); };
        int repaints = 0;
        f.ui.impl->request_repaint = [&] { ++repaints; };
        for (int i = 0; i < 30; ++i)
            f.frame(progress);
        expect(motion.progress_phase == 0 && repaints == 0,
               "indeterminate progress is completely idle without hover");
        for (int i = 0; i < 12; ++i)
            f.frame(progress, 180, 115);
        expect(motion.progress_phase > 0 && repaints > 0,
               "hover advances indeterminate progress and requests the next frame");
        float frozen = motion.progress_phase;
        repaints = 0;
        for (int i = 0; i < 30; ++i)
            f.frame(progress);
        expect(motion.progress_phase == frozen && repaints == 0,
               "progress stays at its last phase after pointer exit even when "
               "other frames occur");
        motion.loading_left = 1.6f;
        auto loading = [&] { draw_buttons(f.ui, motion, ButtonLoading, {20, 20, 360, 160}); };
        for (int i = 0; i < 12; ++i)
            f.frame(loading);
        expect(approximately(motion.loading_left, 1.6f),
               "loading button does not run its timer without hover");
        for (int i = 0; i < 12; ++i)
            f.frame(loading, 100, 66);
        expect(motion.loading_left < 1.6f && motion.loading_left > 0,
               "hovering the loading button advances the preview");
        frozen = motion.loading_left;
        for (int i = 0; i < 12; ++i)
            f.frame(loading);
        expect(motion.loading_left == frozen, "loading button pauses immediately after pointer exit");
        f.ui.impl->request_repaint = {};
    }
    f.frame([] {});
    f.frame([] {});
    {
        for (int i = 0; i < int(aino::SystemIcon::Count); ++i) {
            auto mask = aino::detail::load_system_icon(static_cast<aino::SystemIcon>(i), 24);
            expect(!mask.alpha.empty() && std::any_of(mask.alpha.begin(), mask.alpha.end(),
                                                      [](uint8_t alpha) { return alpha > 0; }),
                   "each icon resolves to an installed system glyph");
        }
        f.frame([&] {
            f.ui.push_clip({30, 30, 10, 10});
            f.ui.system_icon(aino::SystemIcon::Star, {20, 20, 40, 40}, Color(0, 0, 0));
            f.ui.pop_clip();
        });
        bool outside = false;
        for (int y = 18; y < 62; ++y)
            for (int x = 18; x < 62; ++x)
                if (!(x >= 30 && x < 40 && y >= 30 && y < 40) && f.pixel(x, y).r < 240)
                    outside = true;
        expect(!outside, "system glyph drawing honors the current clip");
        bool open = true, confirmed = false;
        auto dialog = [&] {
            confirmed = f.ui.alert_dialog("Confirm", "Body", "Save", "Cancel", &open, {20, 20, 400, 210}) ||
                        confirmed;
        };
        f.frame(dialog);
        Color corner = f.pixel(41, 143), center = f.pixel(65, 156);
        expect(center.b > 220 && center.r < 40 && std::abs(int(corner.r) - int(corner.b)) < 5,
               "dialog confirm uses a filled rounded primary button");
        for (int i = 0; i < 20; ++i)
            f.frame(dialog, 220, 156);
        Color hover_corner = f.pixel(41, 143);
        expect(equal(corner, hover_corner), "dialog hover never fills square button corners");
        f.click(220, 156, dialog);
        expect(confirmed && !open, "rounded dialog confirmation retains its action behavior");
    }
    f.frame([] {});
    f.frame([] {});
    expect(CardCount == 43 && kPageCount == 15, "reference inventory contains 15 sections and 43 cards");
    int total = 0;
    for (const auto &page : kPages) {
        expect(page.first == total, "sections own a continuous, nonduplicated card range");
        total += page.count;
    }
    expect(total == CardCount, "every card is reachable from the catalog");
    for (int i = 0; i < CardCount; ++i)
        expect(int(kCards[i].id) == i, "card id matches its manifest entry");
    for (bool dark : {false, true})
        for (int i = 0; i < 8; ++i) {
            app.dark = dark;
            app.well = i;
            auto native = dark ? aino::Theme::dark() : aino::Theme::light();
            native.set_tint(accent_color(i, dark));
            expect(gallery_theme(app).same_as(native), "gallery preserves the existing native visual theme");
            const auto th = gallery_theme(app);
            expect(equal(th.tint, th.primary) && equal(th.table_sel, th.tint) &&
                       th.ring.rgb24() == th.tint.rgb24(),
                   "accent changes synchronize all derived theme tokens");
        }
    expect(equal(aino::Theme::dark().title_bg, aino::Theme::dark().window_bg),
           "dark toolbar uses the window background without a lighter gray band");
    const auto original = aino::Theme::light();
    for (int token = 0; token < 6; ++token) {
        auto changed = original;
        float *arrays[] = {changed.size_h,    changed.size_radius, changed.size_font,
                           changed.size_line, changed.size_padx,   changed.size_pady};
        arrays[token][2] += 1;
        expect(!original.same_as(changed), "geometry token change invalidates the theme");
    }
    expect(original.same_as(original), "unchanged theme remains idle");
    {
        Flow row({20, 20, 300, 200});
        Rect first = row.next(24, 24), second = row.next(24, 24);
        expect(first.w == first.h && second.w == second.h, "icon buttons have square bounds");
        expect(second.x == first.right() + 10, "icon button gap follows its full width");
    }
    {
        const Rect area{20, 20, 520, 80};
        Flow row(area);
        const float width = f.ui.text_width("继续", 13) + 30;
        row.next(width);
        Rect secondary = row.next(f.ui.text_width("次要", 13) + 30);
        Rect standard = row.next(f.ui.text_width("标准", 13) + 30);
        auto surface = [&](Rect r) { return f.pixel(int(r.x + 8), int(r.cy())); };
        for (bool dark : {false, true}) {
            const aino::Theme th = dark ? aino::Theme::dark() : aino::Theme::light();
            f.ui.set_theme(th);
            const auto skin = aino::detail::make_skin(th, Style().standard(), Style::Default, th.radius);
            const auto primary = aino::detail::make_skin(th, Style(), Style::Default, th.radius);
            expect(skin.base.a == 255 && equal(skin.fg, th.label) && skin.border_w == 1,
                   "standard is an opaque neutral bordered button");
            expect(equal(primary.base, th.tint) && equal(primary.fg, th.tint_foreground),
                   "default button keeps its accent semantics");
            for (Color backdrop : {th.window_bg, th.card}) {
                App variants;
                f.ui.set_focus(nullptr);
                auto draw = [&] {
                    f.ui.fill_rect(area, backdrop);
                    draw_buttons(f.ui, variants, ButtonVariants, area);
                };
                for (int i = 0; i < 24; ++i) f.frame(draw);
                const Color standard_idle = surface(standard), secondary_idle = surface(secondary);
                expect(std::fabs(standard_idle.luminance() - secondary_idle.luminance()) * 255 >= 16,
                       "standard and secondary have visible surface contrast on window and card backgrounds");
                expect(equal(standard_idle, skin.base), "standard surface does not depend on its backdrop");
                expect(equal(secondary_idle, th.fill2.over(backdrop)),
                       "secondary retains its translucent theme fill");
                const Color standard_edge = f.pixel(int(standard.cx()), int(standard.bottom() - 1));
                const Color secondary_edge = f.pixel(int(secondary.cx()), int(secondary.bottom() - 1));
                expect(std::fabs(standard_edge.luminance() - standard_idle.luminance()) * 255 >= 4,
                       "standard has a visible lower edge");
                expect(equal(secondary_edge, secondary_idle), "secondary retains its flat edge");
                for (int i = 0; i < 24; ++i) f.frame(draw, standard.x + 8, standard.cy());
                expect(equal(surface(standard), standard_idle) && variants.hits == 0,
                       "hover preserves the standard neutral surface without activating it");
                for (int i = 0; i < 24; ++i) f.frame(draw, standard.x + 8, standard.cy(), true);
                expect(std::fabs(surface(standard).luminance() - standard_idle.luminance()) * 255 >= 16,
                       "standard press gives visible feedback");
                f.frame(draw, standard.x + 8, standard.cy(), false, true);
                expect(variants.hits == 1 && variants.notices.back().message == "已点击 标准",
                       "standard click activates exactly its own action");
                for (int i = 0; i < 24; ++i) f.frame(draw);
                expect(equal(surface(standard), standard_idle), "standard returns to idle after release");
                f.click(secondary.x + 8, secondary.cy(), draw);
                expect(variants.hits == 2 && variants.notices.back().message == "已点击 次要",
                       "secondary keeps its independent action");
            }
            bool clicked = false;
            auto disabled = [&] {
                clicked |= f.ui.button("disabled standard", standard, Style().standard().off());
            };
            f.click(standard.cx(), standard.cy(), disabled);
            expect(!clicked, "disabled standard cannot activate");
            const Style custom = Style().standard().background(th.purple).color(th.tint_foreground)
                                        .stroke(0, Color::none()).corner(3);
            const auto overridden = aino::detail::make_skin(th, custom, Style::Default, th.radius);
            expect(equal(overridden.base, th.purple) && equal(overridden.fg, th.tint_foreground) &&
                       overridden.border_w == 0 && overridden.radius == 3,
                   "standard respects explicit style overrides");
        }
        f.ui.set_focus(nullptr);
        f.ui.set_theme(aino::Theme::light());
    }
    app = App{};
    app.date_open = true;
    app.color_open = true;
    int gates = f.ui.impl->gate_next_allow_n;
    auto measured_popovers = layout_gallery(f.ui, app, 876, false);
    expect(f.ui.impl->gate_next_allow_n == gates,
           "layout measurement does not register input gates for open popovers");
    expect(app.date_open && app.color_open, "layout measurement preserves real popover state");
    app.date_open = false;
    app.color_open = false;
    // Measure and rasterize the real Typography card: draw_mono_text alone
    // does not report bounds, so measurement-only layout tests miss its ink.
    for (bool dark : {false, true}) {
        f.ui.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
        for (float scale : {1.f, 1.25f, 1.5f}) {
            for (float width : {268.f, 340.f, 412.f, 600.f}) {
                App typography;
                Rect card;
                const Color backdrop = dark ? f.ui.theme().window_bg : Color::hex(0xF5F5F7);
                f.frame([&] {
                    f.ui.clear(backdrop);
                    card = {40, 40, width + 32, body_height(f.ui, typography, Typography, width) + 66};
                    for (const auto& entry : kCards)
                        if (entry.id == Typography) draw_card(f.ui, typography, entry, card);
                }, -100, -100, false, false, 0, scale);
                GdiFlush();
                bool overflow = false, number_visible = false;
                const int bottom = int(std::ceil(card.bottom() * scale));
                for (int y = bottom; y < bottom + int(32 * scale); ++y)
                    for (int x = int((card.x + 16) * scale); x < int((card.right() - 16) * scale); ++x)
                        overflow |= !equal(f.pixel(x, y), backdrop);
                const Color surface = dark ? f.ui.theme().card : f.ui.theme().control_bg;
                for (int y = int((card.y + 48 + 142) * scale); y < bottom - int(10 * scale); ++y)
                    for (int x = int((card.x + 16) * scale); x < int((card.x + 160) * scale); ++x)
                        number_visible |= !equal(f.pixel(x, y), surface);
                expect(!overflow, "Typography ink stays inside its measured card at every tested DPI and width");
                expect(number_visible, "Typography number remains visible inside its card");
            }
        }
    }
    f.ui.set_theme(aino::Theme::light());
    for (float width : {268.f, 340.f, 412.f, 600.f}) {
        for (const auto &card : kCards) {
            f.ui.begin_measure();
            draw_body(f.ui, app, card.id, {0, 0, width, 400});
            Rect bounds = f.ui.end_measure();
            const bool fits = bounds.x >= -4 && bounds.right() <= width + 4;
            if (!fits)
                std::printf("  overflow %s width %.1f: x=%.1f right=%.1f\n", card.name, width, bounds.x,
                            bounds.right());
            expect(fits, "component body fits its actual column width");
        }
    }
    for (float width : {612.f, 780.f, 880.f, 1100.f}) {
        auto layout = layout_gallery(f.ui, app, width, width <= 780);
        expect(layout.cards.size() == CardCount, "responsive layout preserves all cards");
        for (const auto &card : layout.cards) {
            expect(card.rect.x >= 0 && card.rect.right() <= width + 1,
                   "responsive card remains inside content bounds");
        }
        bool overlaps = false;
        for (size_t i = 0; i < layout.cards.size(); ++i)
            for (size_t j = i + 1; j < layout.cards.size(); ++j) {
                Rect a = layout.cards[i].rect, b = layout.cards[j].rect;
                if (a.x < b.right() - .1f && a.right() > b.x + .1f && a.y < b.bottom() - .1f &&
                    a.bottom() > b.y + .1f)
                    overlaps = true;
            }
        expect(!overlaps, "responsive cards never overlap");
    }
    f.width = 1600;
    f.frame([] {});
    auto wide_layout = layout_gallery(f.ui, app, 1296, false);
    for (const auto &placed : wide_layout.cards)
        if (kCards[placed.index].id == Carousel)
            expect(approximately(placed.rect.w, (1296 - 3 * kGridGap) / 4 * 2 + kGridGap),
                   "wide carousel spans two columns instead of the entire grid");
    f.width = 1060;
    f.frame([] {});
    auto medium_layout = layout_gallery(f.ui, app, 756, false);
    for (const auto &placed : medium_layout.cards)
        if (kCards[placed.index].id == Carousel)
            expect(approximately(placed.rect.w, (756 - kGridGap) / 2),
                   "carousel stays in one column below the 1080px breakpoint");
    f.width = 1180;
    f.frame([] {});
    app.query = "PaSsWoRd";
    auto filtered = layout_gallery(f.ui, app, 876, false);
    expect(filtered.cards.size() == 1 && kCards[filtered.cards[0].index].id == InputSearch,
           "search matches component keywords case insensitively");
    app.query = "日期";
    filtered = layout_gallery(f.ui, app, 876, false);
    expect(filtered.cards.size() == 2 && filtered.counts[6] == 2,
           "Chinese section search includes its component cards");
    app.query = "no-such-component";
    filtered = layout_gallery(f.ui, app, 876, false);
    expect(filtered.cards.empty(), "empty search has no stale component cards");
    app = App{};
    auto full = [&] { build_ui(f.ui, app); };
    for (int i = 0; i < kPageCount; ++i) {
        app.pending_anchor = i;
        f.frame(full);
        f.frame(full);
        if (app.page != i)
            std::printf("  anchor %d resolved as %d offset %.1f\n", i, app.page, app.content_offset);
        expect(app.page == i, "sidebar anchor and scroll spy agree for every section");
        expect(app.visible_cards > 0 && app.visible_cards < CardCount,
               "long gallery renders only visible cards");
    }
    app.query = "password";
    f.frame(full);
    f.frame(full);
    expect(app.content_offset == 0 && app.visible_cards == 1,
           "search resets the scroll position and visible cards");
    f.click(80, nav_item_y(6) - app.nav_offset, full);
    f.frame(full);
    expect(app.query.empty() && app.page == 6,
           "navigation clears a filter and reaches the requested section");
    app.pending_anchor = 7;
    f.frame(full);
    f.frame(full);
    Rect accents = app.card_rects[Accents];
    f.click(accents.x + 16 + 4 + 34 * 4 + 12, accents.y + 48 + 4 + 12, full);
    expect(app.well == 4 && equal(f.ui.theme().tint, accent_color(4, app.dark)),
           "system swatch updates the live global accent");
    app.pending_anchor = 0;
    f.frame(full);
    f.frame(full);
    expect(equal(f.ui.theme().primary, accent_color(4, app.dark)), "chosen accent survives navigation");
    Rect button = app.card_rects[ButtonVariants];
    int hits = app.hits;
    f.click(button.x + 38, button.y + 60, full);
    expect(app.hits == hits + 1 && !app.notices.empty(), "main button produces actual notification feedback");
    {
        Fixture quiet;
        App notice;
        notice.notices.push_back({"保存完成", 0, false});
        quiet.ui.set_theme(gallery_theme(notice));
        quiet.ui.begin_frame(quiet.width, quiet.height, 4.0f, 1.0f);
        quiet.ui.clear(quiet.ui.theme().background);
        build_ui(quiet.ui, notice);
        quiet.ui.end_frame();
        expect(notice.notices.size() == 1,
               "static gallery notifications persist until explicitly dismissed");
    }
    app.pending_anchor = 7;
    f.frame(full);
    f.frame(full);
    Rect palette = app.card_rects[Palette];
    f.click(palette.x + 60, palette.y + 60, full);
    expect(app.color_open, "color trigger opens a real popover");
    // Popover placement is window-clamped; calculate the same anchor geometry.
    float px = aino::clampf(palette.x + 16, 6.f, 1180 - 272.f - 6), py = palette.y + 48 + 24 + 6;
    if (py + 144 > 754)
        py = std::max(6.f, palette.y + 48 - 6 - 144);
    f.click(px + 20 + 56 * 2 + 13, py + 20 + 34 + 13, full);
    f.frame(full);
    expect(app.well == 2 && !app.color_open,
           "choosing a palette cell changes the accent and closes the popover");
    app.pending_anchor = 6;
    f.frame(full);
    f.frame(full);
    Rect date = app.card_rects[Date];
    f.click(date.x + 60, date.y + 60, full);
    expect(app.date_open, "date trigger opens the calendar");
    float dx = aino::clampf(date.x + 16, 6.f, 1180 - 320.f - 6), dy = date.y + 48 + 24 + 6;
    if (dy + 332 > 754)
        dy = std::max(6.f, date.y + 48 - 6 - 332);
    int target = 12, slot = aino::detail::weekday_of(app.cy, app.cm, 1) + target - 1;
    f.click(dx + 20 + (slot % 7 + .5f) * 40, dy + 20 + 68 + (slot / 7 + .5f) * (224.f / 6), full);
    f.frame(full);
    expect(app.cd == target && !app.date_open, "calendar selection writes the date and closes the popover");
    Rect time = app.card_rects[Time];
    int hour = app.hour;
    float offset = app.content_offset;
    f.frame(full, time.x + 40, time.y + 104, false, false, -1);
    expect(app.hour == (hour + 1) % 24, "hour picker responds to the mouse wheel");
    expect(approximately(app.content_offset, offset), "wheel picker does not also scroll the outer gallery");
    app.pending_anchor = 9;
    f.frame(full);
    f.frame(full);
    Rect table = app.card_rects[Table];
    f.click(table.x + 16 + (table.w - 32) * .5f, table.y + 60, full);
    expect(app.sort_column == 1 && !app.sort_desc, "table header selects numeric ascending sort");
    auto rows = sorted_build_rows(1, false);
    expect(rows[0] == 4 && rows[1] == 2 && rows[4] == 1,
           "table sizes sort numerically instead of lexicographically");
    f.click(table.x + 16 + (table.w - 32) * .5f, table.y + 60, full);
    expect(app.sort_desc, "clicking the same table header reverses sort direction");
    app.pending_anchor = 12;
    f.frame(full);
    f.frame(full);
    // The carousel may be below the viewport; target its card locally too.
    f.frame([] {});
    f.frame([] {});
    auto carousel_body = [&] { draw_cards(f.ui, app, Carousel, {20, 20, 500, 200}); };
    f.click(20 + 500 - 32, 20 + 156 - 28, carousel_body);
    expect(app.slide == 1, "carousel advances through the actual slide state");
    f.click(20 + 32, 20 + 156 - 28, carousel_body);
    expect(app.slide == 0, "carousel supports reverse navigation");
    app = App{};
    auto accordion = [&] { draw_cards(f.ui, app, Accordions, {20, 20, 360, 300}); };
    f.click(110, 36, accordion);
    expect(app.expanded[0] && app.pending_accordion == 0,
           "accordion defers a size change until layout can remeasure");
    f.frame(full);
    expect(!app.expanded[0] && app.pending_accordion == -1,
           "next frame reflows the collapsed accordion before rendering");
    app = App{};
    auto sliders = [&] { draw_values(f.ui, app, Sliders, {20, 20, 360, 160}); };
    f.click(300, 60, sliders);
    float selected = app.vol;
    f.frame(sliders);
    expect(selected > 70 && approximately(app.vol, selected), "sliders preserve edited values across frames");
    app = App{};
    auto password = [&] { f.ui.input(&app.password, {20, 20, 360, 24}, Style().secret()); };
    std::string clipboard = "unchanged";
    f.ui.impl->set_clipboard = [&](const std::string &text) { clipboard = text; };
    f.click(80, 32, password);
    f.key('A', password, true);
    f.key('C', password, true);
    expect(clipboard == "unchanged", "masked password does not leak through copy");
    f.key('X', password, true);
    expect(clipboard == "unchanged" && app.password == "aino-demo",
           "masked password cannot leak through cut");
    f.key(aino::KEY_END, password);
    for (int i = 0; i < 12; ++i)
        f.frame(password);
    f.type("你好", password);
    f.frame(password);
    if (app.password != "aino-demo你好")
        std::printf("password actual: %s veto=%.4f focus=%p\n", app.password.c_str(), f.ui.impl->text_veto,
                    f.ui.focused_id());
    expect(app.password == "aino-demo你好", "masked field retains real Unicode text while editing");
    f.key('Z', password, true);
    expect(app.password == "aino-demo", "masked input preserves undo history");
    f.frame([] {});
    f.frame([] {});
    app = App{};
    f.width = 780;
    f.height = 560;
    f.frame(full);
    f.click(30, 30, full);
    f.frame(full);
    expect(app.nav_open && app.nav_w == 0,
           "narrow layout opens an overlay sidebar without shrinking the content");
    f.click(90, nav_item_y(1) - app.nav_offset, full);
    f.frame(full);
    if (app.page != 1 || app.nav_open)
        std::printf("drawer page=%d open=%d offset=%.1f pending=%d\n", app.page, app.nav_open, app.nav_offset,
                    app.pending_anchor);
    expect(app.page == 1 && !app.nav_open, "drawer navigation remains clickable and closes after selection");
    f.click(30, 30, full);
    f.frame(full);
    f.click(500, 100, full);
    f.frame(full);
    expect(!app.nav_open, "clicking outside the sidebar dismisses the drawer");
    f.click(30, 30, full);
    f.frame(full);
    f.key(aino::KEY_ESC, full);
    f.frame(full);
    expect(!app.nav_open, "Escape dismisses the narrow sidebar drawer");
    f.width = 1180;
    f.height = 760;
    app = App{};
    app.pending_anchor = 14;
    f.frame(full);
    f.frame(full);
    expect(app.virtual_drawn > 0 && app.virtual_drawn <= 8, "10,000 item list draws only the visible rows");
    f.frame([] {});
    f.frame([] {});
    std::string query = "native search";
    auto search_clear = [&] { f.ui.search_field(&query, {20, 20, 240, 24}); };
    f.click(248, 32, search_clear);
    expect(query.empty(), "clear button owns the click instead of the enclosing search input");
    query = "cannot clear";
    auto disabled_clear = [&] { f.ui.search_field(&query, {20, 20, 240, 24}, Style().off()); };
    f.click(248, 32, disabled_clear);
    expect(query == "cannot clear", "disabled search clear does not modify the value");
    f.frame([] {});
    f.frame([] {});
    f.ui.set_theme(aino::Theme::light());
    for (float value : {-1.f, 0.f, .30f, .75f, 1.f, 2.f}) {
        auto progress = [&] { f.ui.progress_ring(value, {20, 20, 80, 80}); };
        f.frame(progress);
        expect(f.pixel(18, 60).r > 240 && f.pixel(102, 60).r > 240,
               "progress ring stays inside its declared bounds");
    }
    // A translucent track must keep the same opacity around its entire
    // centerline.
    f.frame([&] {
        f.ui.progress_ring(0, {20, 20, 80, 80},
                           Style().background(Color(0, 0, 0, 128)).stroke(6, Color::none()));
    });
    int min_track = 255, max_track = 0;
    for (int i = 0; i < 360; ++i) {
        const float angle = i * 6.2831853f / 360;
        const Color pixel = f.pixel(int(60 + 37 * std::cos(angle)), int(60 + 37 * std::sin(angle)));
        min_track = std::min(min_track, int(pixel.r));
        max_track = std::max(max_track, int(pixel.r));
    }
    expect(max_track - min_track <= 1 && min_track > 100 && max_track < 150,
           "translucent progress ring has uniform opacity without dark segment "
           "joins");
    f.frame([&] { f.ui.progress_ring(.75f, {20, 20, 80, 80}); });
    expect(f.pixel(34, 34).r > 200 && f.pixel(86, 34).r < 50,
           "75 percent ring leaves its upper left quarter unfilled");
    f.frame([&] { f.ui.progress_ring(.30f, {20, 20, 80, 80}); });
    expect(f.pixel(60, 23).r < 50 && f.pixel(60, 97).r > 200,
           "30 percent ring starts at twelve oclock and ends before the bottom");
    f.frame([&] {
        f.ui.button("Member", {20, 20, 300, 33}, Style().ghost().left().padded(34, 0));
    });
    bool padding_ink = false, text_ink = false;
    for (int y = 22; y < 51; ++y)
        for (int x = 20; x < 150; ++x)
            if (f.pixel(x, y).r < 100) {
                if (x < 54) padding_ink = true;
                else text_ink = true;
            }
    expect(!padding_ink && text_ink, "button text honors explicit padding and leaves its icon column clear");
    // Existing low-level fixes remain covered after the gallery catalog changed.
    f.frame([] {});
    f.frame([] {});
    f.ui.set_theme(aino::Theme::light());
    bool on = true;
    for (Rect box : {Rect(20, 20, 36, 16), Rect(20, 20, 44, 20), Rect(20, 20, 54, 24), Rect(20, 20, 64, 28),
                     Rect(20, 20, 80, 36), Rect(20, 20, 44, 26)}) {
        auto toggle = [&] { f.ui.toggle(&on, box); };
        for (int i = 0; i < 30; ++i)
            f.frame(toggle);
        expect(f.pixel(18, int(box.cy())).r > 240, "switch track stays within its hit rectangle");
    }
    float value = .5f;
    auto slider = [&] { f.ui.slider(&value, 0, 1, {20, 20, 200, 24}); };
    f.click(30, 32, slider);
    expect(approximately(value, 0), "slider reaches its visible left endpoint");
    f.click(210, 32, slider);
    expect(approximately(value, 1), "slider reaches its visible right endpoint");
    int radio = 0;
    auto choice = [&] { f.ui.radio_group({"one"}, &radio, {20, 20, 200, 24}); };
    for (int i = 0; i < 30; ++i)
        f.frame(choice);
    Color center = f.pixel(28, 32), surround = f.pixel(33, 32);
    expect(std::abs(int(center.r) - int(surround.r)) + std::abs(int(center.g) - int(surround.g)) +
                   std::abs(int(center.b) - int(surround.b)) >
               100,
           "selected radio dot contrasts with its tint");
    App material;
    material.query = "空状态";
    auto empty_card = [&] { build_ui(f.ui, material); };
    f.frame(empty_card);
    const Rect empty = material.card_rects[Empty];
    f.click(empty.cx(), empty.y + 48 + 146, empty_card);
    expect(material.open_material && material.material_name.empty() && material.notices.empty(),
           "create material requests a window without claiming creation");
    material.material_editor->created_name = "测试素材";
    material.material_editor->created_description = "子窗口提交的说明";
    collect_material(material);
    expect(material.material_name == "测试素材" && material.material_description == "子窗口提交的说明",
           "child submission updates material content");
    expect(material.material_editor->created_name.empty(), "child submission is consumed once");
    collect_material(material);
    expect(material.material_name == "测试素材", "idle collection preserves submitted material");
    App cached;
    auto matches_fresh_layout = [&](float width) {
        auto actual = layout_gallery(f.ui, cached, width, width <= 780);
        App fresh = cached;
        fresh.body_measurements = BodyMeasurements{};
        auto expected = layout_gallery(f.ui, fresh, width, width <= 780);
        bool equal = approximately(actual.height, expected.height) && actual.cards.size() == expected.cards.size();
        for (size_t i = 0; equal && i < actual.cards.size(); ++i) {
            const Rect a = actual.cards[i].rect, b = expected.cards[i].rect;
            equal = actual.cards[i].index == expected.cards[i].index &&
                    approximately(a.x, b.x) && approximately(a.y, b.y) &&
                    approximately(a.w, b.w) && approximately(a.h, b.h);
        }
        expect(equal, "cached card geometry matches a fresh measurement");
    };
    matches_fresh_layout(876);
    cached.vol = 87;
    cached.note = "edited text";
    matches_fresh_layout(876);
    cached.expanded[0] = false;
    cached.expanded[1] = true;
    matches_fresh_layout(876);
    cached.chip_removed = true;
    matches_fresh_layout(876);
    matches_fresh_layout(620);
    cached.query = "input";
    matches_fresh_layout(620);
    cached.query.clear();
    matches_fresh_layout(876);
    f.ui.set_theme(aino::Theme::dark());
    matches_fresh_layout(876);
    auto larger_font = f.ui.theme();
    larger_font.font_size = 17;
    f.ui.set_theme(larger_font);
    matches_fresh_layout(876);
    f.ui.impl->scale = 1.25f;
    matches_fresh_layout(876);
    std::printf("[macos-gallery] %d passed, %d failed; verdict: %s\n", passed, failed,
                failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
