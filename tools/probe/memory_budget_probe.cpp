// 小预算边界使用确定性的文字度量，不需要窗口或平台字形栅格。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

struct Metrics : aino::detail::TextRenderer {
    void init(float, const std::string&, const std::string&) override {}
    float measure(const std::string& text, float, int) override { return float(text.size()) * 8; }
    float ascent(float, int) override { return 10; }
    float line_height(float, int) override { return 14; }
    void draw(aino::detail::Canvas&, const std::string&, float, float, float, int,
              const aino::Color&, const aino::Rect&) override {}
    void release() override {}
};

int main() {
    aino::Ui ui;
    Metrics metrics;
    auto* im = ui.impl;
    im->text = &metrics;
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (ok) ++passed;
        else { ++failed; std::printf("FAIL: %s\n", what); }
    };
    const size_t wrap_budget = aino::detail::memory_budget_kib("AINO_WRAP_BUDGET", 6u << 20);
    const size_t undo_budget = aino::detail::memory_budget_kib("AINO_UNDO_BUDGET", 4u << 20);
    std::vector<std::string> docs;
    docs.reserve(100);
    for (int i = 0; i < 100; ++i) {
        docs.push_back(std::string(80, char('a' + i % 26)));
        const auto& lines = aino::detail::wrap_layout(im, docs.back(), 640, 14, 400, false);
        check(lines.size() == 1 && lines[0] == docs.back(), "cached layout is exact");
        check(im->wrap_bytes <= wrap_budget && im->wrap_cache.size() == im->wrap_lru.size(),
              "cache and recency index stay within budget");
    }
    check(im->wrap_evictions > 0, "small budget exercises eviction");
    const std::string& hot = docs.back();
    const auto& warm = aino::detail::wrap_layout(im, hot, 640, 14, 400, false);
    const auto* warm_address = &warm;
    const auto bytes = im->wrap_bytes, entries = im->wrap_cache.size();
    std::string huge(wrap_budget, 'x');
    const auto& transient = aino::detail::wrap_layout(im, huge, 640, 14, 400, false);
    check(transient.size() == 1 && transient[0] == huge, "oversized layout still returns complete text");
    check(im->wrap_bytes == bytes && im->wrap_cache.size() == entries, "oversized layout preserves hot cache");
    check(&aino::detail::wrap_layout(im, hot, 640, 14, 400, false) == warm_address && im->wrap_scratch.empty(),
          "next layout reuses hot entry and releases oversized scratch");
    docs.back()[0] = '!';
    const auto& changed = aino::detail::wrap_layout(im, hot, 640, 14, 400, false);
    check(changed[0] == hot && im->wrap_cache.size() == im->wrap_lru.size(), "in-place text changes invalidate cache");

    const uint64_t id = 71;
    for (int i = 0; i < 80; ++i) {
        aino::detail::edit_snapshot(im, id, std::string(300, char('a' + i % 26)), 1, -1,
                                    aino::detail::EDIT_OTHER);
        check(aino::detail::edit_history_bytes(im->edit_stack[id]) <= undo_budget, "undo obeys byte budget");
    }
    check(im->edit_stack[id].undo.size() < 80, "byte budget limits large snapshots before count limit");
    std::string text = "current";
    int caret = 1, anchor = -1;
    check(aino::detail::edit_undo(im, id, &text, &caret, &anchor), "bounded history remains usable");
    check(aino::detail::edit_redo(im, id, &text, &caret, &anchor) && text == "current", "bounded redo restores current text");
    aino::detail::edit_snapshot(im, id, std::string(undo_budget, 'z'), 1, -1, aino::detail::EDIT_OTHER);
    check(im->edit_stack[id].undo.empty() && im->edit_stack[id].redo.empty(),
          "oversized edit clears older history instead of skipping across an unrecorded edit");
    check(!aino::detail::edit_undo(im, id, &text, &caret, &anchor) && text == "current", "no history cannot alter text");
    std::printf("[memory-budget] %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
