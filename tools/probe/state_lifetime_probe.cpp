// 控件状态拥有者回归：隐藏保留、显式销毁、跨拥有者隔离及回调引用安全。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <memory>

static int passed = 0, failed = 0;
static void check(bool ok, const char* message) {
    if (ok) ++passed;
    else { ++failed; std::printf("[FAIL] %s\n", message); }
}
static void frame(aino::Ui& ui) { ui.begin_frame(320, 240, .016f, 1); }
static void empty_frames(aino::Ui& ui, int count) {
    for (int i = 0; i < count; ++i) { frame(ui); ui.end_frame(); }
}
static void seed(aino::Ui& ui, uint64_t id) {
    auto* im = ui.impl;
    im->anim[id] = .75f;
    im->phase[id] = 1.5f;
    im->flags[id] = 42;
    im->strings[id] = std::string(256, 'q');
    aino::detail::edit_snapshot(im, id, std::string(256, 'x'), 3, -1, aino::detail::EDIT_OTHER);
    ui.tween(id, 2);
    ui.tween_color(id, aino::Color::white());
}
static void check_count(aino::Ui& ui, size_t count) {
    auto* im = ui.impl;
    check(im->anim.size() == count, "hover state count matches remaining owners");
    check(im->phase.size() == count, "animation phase count matches remaining owners");
    check(im->flags.size() == count, "flag count matches remaining owners");
    check(im->strings.size() == count, "search string count matches remaining owners");
    check(im->edit_stack.size() == count, "undo and redo count matches remaining owners");
    check(im->tweens.size() == count, "numeric tween count matches remaining owners");
    check(im->color_tweens.size() == count, "color tween count matches remaining owners");
}
static void hidden_is_not_destroyed() {
    aino::Ui ui;
    frame(ui);
    seed(ui, 17);
    {
        auto page = ui.state_scope(101);
        seed(ui, 17);
        ui.impl->flags[17] = 84;
    }
    ui.end_frame();
    empty_frames(ui, 2400);
    check_count(ui, 2);
    check(ui.impl->flags[17] == 42, "legacy unscoped state survives long periods without submission");
    {
        auto page = ui.state_scope(101);
        check(ui.impl->anim[17] == .75f && ui.impl->phase[17] == 1.5f &&
              ui.impl->flags[17] == 84 && ui.impl->strings[17] == std::string(256, 'q') &&
              ui.impl->edit_stack[17].undo.size() == 1 && ui.tween(17, 2) == 2,
              "hidden pages keep functional state, history and animation values indefinitely");
    }
    ui.release_state_scope(0);
    check_count(ui, 2);
    ui.release_state_scope(101);
    check_count(ui, 1);
    check(ui.impl->flags[17] == 42, "releasing a named owner never clears legacy state with the same key");
}
static void changing_owners() {
    aino::Ui ui;
    for (uint64_t f = 1; f <= 1800; ++f) {
        frame(ui);
        {
            auto page = ui.state_scope(f);
            for (uint64_t i = 0; i < 8; ++i) seed(ui, i);
        }
        ui.release_state_scope(f);
        ui.end_frame();
    }
    check_count(ui, 0);
    check(ui.impl->released_state_scopes.empty(), "destruction requests do not accumulate across frames");
}
static void compact_live_nodes() {
    aino::Ui ui;
    {
        auto page = ui.state_scope(501);
        for (uint64_t id = 1; id <= 4096; ++id) seed(ui, id);
    }
    float* value;
    aino::Ui::Impl::EditStack* history;
    {
        auto other_page = ui.state_scope(502);
        seed(ui, 3);
        value = &ui.impl->flags[3];
        history = &ui.impl->edit_stack[3];
    }
    const size_t buckets = ui.impl->flags.bucket_count();
    ui.release_state_scope(501);
    check_count(ui, 1);
    check(ui.impl->flags.bucket_count() < buckets && ui.impl->flags.bucket_count() <= 64 &&
          ui.impl->edit_stack.bucket_count() <= 64, "destroying a large owner releases bucket high-water storage");
    {
        auto page = ui.state_scope(502);
        check(value == ui.impl->flags.get_if(3) && history == ui.impl->edit_stack.get_if(3) &&
              *value == 42 && history->undo.size() == 1 && history->undo[0].text.size() == 256,
              "compaction preserves another owner's node addresses and history contents");
    }
    ui.release_state_scope(502);
    check_count(ui, 0);
}
static void editor_and_scroll_state() {
    aino::Ui ui;
    const aino::Rect input{10, 10, 120, 24}, area{10, 50, 120, 70}, scroll{160, 10, 100, 100};
    std::string single = "before", multi = "before", untouched;
    {
        auto page = ui.state_scope(701);
        frame(ui);
        const uint64_t ids[] = {ui.impl->widget_id("input", input), ui.impl->widget_id("textarea", area)};
        for (uint64_t id : ids) {
            aino::detail::edit_snapshot(ui.impl, id, "before", 3, -1, aino::detail::EDIT_OTHER);
            int caret = 5, anchor = -1;
            std::string text = "after";
            check(aino::detail::edit_undo(ui.impl, id, &text, &caret, &anchor) && text == "before",
                  "prepare a redo history for an editor");
        }
        ui.scroll_to_index(scroll, 20, 20, 100);
        ui.end_frame();
        for (int i = 0; i < 1200; ++i) {
            frame(ui);
            ui.input(&single, input);
            ui.textarea(&multi, area);
            ui.input(&untouched, {10, 150, 120, 24});
            ui.end_frame();
        }
        empty_frames(ui, 2400);
        check(ui.scroll_offset(scroll) == 400, "a long-hidden scroll region keeps its exact offset");
        check(ui.impl->edit_stack.size() == 2, "rendering an untouched editor does not allocate an undo stack");
        for (uint64_t id : ids) {
            int caret = 3, anchor = -1;
            std::string text = "before";
            check(aino::detail::edit_redo(ui.impl, id, &text, &caret, &anchor) && text == "after" && caret == 5,
                  "hidden editors preserve redo history and caret snapshots");
            check(aino::detail::edit_undo(ui.impl, id, &text, &caret, &anchor) && text == "before" && caret == 3,
                  "hidden editors preserve undo history and caret snapshots");
        }
    }
    ui.release_state_scope(701);
    check_count(ui, 0);
    check(single == "before" && multi == "before", "releasing UI state never edits application-owned text");
}
static void measurement_and_deferred() {
    aino::Ui ui;
    frame(ui);
    { auto page = ui.state_scope(101); seed(ui, 17); }
    ui.begin_measure();
    {
        auto measured = ui.state_scope(202);
        seed(ui, 17);
        ui.release_state_scope(101);
        check(ui.impl->tweens.size() == 1 && ui.impl->color_tweens.size() == 1,
              "measurement cannot create persistent tween states or destroy another owner");
    }
    ui.end_measure();
    ui.end_frame();
    check_count(ui, 1);
    ui.release_state_scope(101);
    check_count(ui, 0);

    frame(ui);
    bool first_callback = false, second_callback = false;
    {
        auto page = ui.state_scope(303);
        seed(ui, 24);
        float& borrowed = ui.impl->flags[24];
        ui.deferred([&ui, &first_callback, &borrowed] {
            first_callback = ui.impl->state_scope_id == 303 && borrowed == 42;
            seed(ui, 25);
        });
    }
    {
        auto other_page = ui.state_scope(404);
        seed(ui, 24);
        ui.impl->flags[24] = 84;
        ui.deferred([&] { second_callback = ui.impl->state_scope_id == 404 && ui.impl->flags[24] == 84; });
    }
    ui.release_state_scope(303);
    ui.release_state_scope(303);
    check(ui.impl->flags.size() == 2 && ui.impl->released_state_scopes.size() == 1,
          "in-frame destruction is deduplicated and leaves borrowed nodes alive until deferred completes");
    ui.end_frame();
    check(first_callback && second_callback && ui.impl->state_scope_id == 0,
          "each deferred callback restores its owner after the original scope guard is gone");
    check_count(ui, 1);
    ui.release_state_scope(404);
    check_count(ui, 0);
}
static void interaction_ownership() {
    aino::Ui ui;
    const aino::Rect box{0, 0, 100, 24};
    frame(ui);
    ui.impl->input.mouse = {10, 10};
    ui.impl->input.pressed[0] = ui.impl->input.down[0] = true;
    uint64_t first_id, other_id;
    {
        auto page = ui.state_scope(801);
        first_id = ui.impl->widget_id("button", box);
        aino::detail::interact(ui.impl, first_id, box, true);
        aino::detail::grab_focus(ui.impl, first_id);
        ui.impl->gate_claim(box, 1);
    }
    {
        auto other_page = ui.state_scope(802);
        other_id = ui.impl->widget_id("button", box);
        aino::detail::grab_focus(ui.impl, other_id);
        ui.impl->ime_owner = true;
        ui.impl->ime_comp = "composition";
        ui.impl->gate_claim({0, 50, 100, 24}, 2);
    }
    check(first_id != other_id, "identical controls on different pages have isolated interaction identities");
    ui.release_state_scope(801);
    ui.end_frame();
    check(ui.impl->active == 0 && ui.impl->focus_id == other_id,
          "destroying the dragging owner releases its capture state without stealing another owner's focus");
    check(ui.impl->ime_owner && ui.impl->ime_comp == "composition",
          "releasing another owner preserves the focused editor's IME composition");
    check(ui.impl->gate_next_allow_n == 1 && ui.impl->gate_next && ui.impl->gate_next_allow_scope[0] == 802,
          "destroying a popup owner removes only its own input gates");
    ui.release_state_scope(802);
    check(ui.impl->focus == nullptr && ui.impl->focus_id == 0 && !ui.impl->gate_next,
          "destroying the focused owner clears its remaining focus and popup gate");
    check(!ui.impl->ime_owner && ui.impl->ime_comp.empty(), "destroying the focused owner releases IME ownership");
    {
        auto outer = ui.state_scope(901);
        { auto inner = ui.state_scope(902); auto moved = std::move(inner); }
        check(ui.impl->state_scope_id == 901, "moving a nested scope guard restores the parent exactly once");
    }
    check(ui.impl->state_scope_id == 0, "leaving all scope guards restores legacy behavior");
}
static void nested_deferred_lifetime() {
    aino::Ui ui;
    int nested = 0;
    bool capture_saw_live_state = false, parent_restored = false;
    frame(ui);
    {
        auto page = ui.state_scope(1001);
        seed(ui, 1);
        float& borrowed = ui.impl->flags[1];
        auto token = std::shared_ptr<int>(new int(0), [&](int* value) {
            auto state = ui.state_scope(1001);
            const auto* flag = ui.impl->flags.peek(1);
            capture_saw_live_state = flag && *flag == 42;
            delete value;
        });
        ui.deferred([&ui, &nested, &borrowed, &parent_restored, token = std::move(token),
                     payload = std::string(2048, 'p')] {
            {
                auto child = ui.state_scope(1002);
                for (int i = 0; i < 128; ++i) ui.deferred([&ui, &nested, &borrowed, &payload, i] {
                    if (ui.impl->state_scope_id == 1002 && borrowed == 42 &&
                        payload == std::string(2048, 'p')) ++nested;
                    ui.impl->flags[uint64_t(i)] = float(i);
                });
                ui.release_state_scope(1002);
            }
            parent_restored = ui.impl->state_scope_id == 1001 && payload == std::string(2048, 'p');
        });
    }
    ui.deferred({});
    ui.release_state_scope(1001);
    ui.end_frame();
    check(nested == 128 && parent_restored, "queue growth preserves parent captures until all nested callbacks finish");
    check(capture_saw_live_state, "callback captures are destroyed before their borrowed owner is released");
    check(ui.impl->state_scope_id == 0, "nested callbacks restore the caller's scope");
    check_count(ui, 0);
}
static void nested_scroll_ownership() {
    aino::Ui ui;
    const aino::Rect box{0, 0, 100, 100};
    frame(ui);
    {
        auto page = ui.state_scope(1101);
        auto scroll = ui.scroll(box, 1000);
        {
            auto child = ui.state_scope(1102);
            scroll.to(300);
            scroll.content(1200);
            ui.scroll_to(400);
            scroll.close();
            check(ui.impl->state_scope_id == 1102, "scroll methods restore the nested caller's owner");
            check(ui.impl->flags.peek(scroll.id) == nullptr,
                  "child controls do not acquire their parent scroll's offset");
            ui.impl->flags[7] = 99;
        }
        check(ui.scroll_offset(box) == 400, "scroll handles and the current-scroll API retain the creating owner");
    }
    ui.end_frame();
    ui.release_state_scope(1101);
    check(ui.impl->flags.size() == 1, "releasing a parent owner does not cascade into independent nested owners");
    {
        auto child = ui.state_scope(1102);
        check(ui.impl->flags[7] == 99, "independent nested owner retains its functional state");
    }
    ui.release_state_scope(1102);
    check_count(ui, 0);
}
int main() {
    hidden_is_not_destroyed();
    changing_owners();
    compact_live_nodes();
    editor_and_scroll_state();
    measurement_and_deferred();
    interaction_ownership();
    nested_deferred_lifetime();
    nested_scroll_ownership();
    std::printf("[state-lifetime] %d passed, %d failed; verdict: %s\n",
                passed, failed, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
