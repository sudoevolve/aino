// Native offline workspace fixtures; no profiles, credentials or network are accessed.
#define WinMain aino_ssh_entry
#include "../../examples/ssh_client.cpp"
#undef WinMain
#include <fstream>

namespace {
int passed = 0, failed = 0;
void expect(bool condition, const char* description) {
    if (condition) ++passed;
    else { ++failed; std::printf("FAIL: %s\n", description); }
}

bool save_bitmap(const aino::detail::Canvas& canvas, const std::filesystem::path& path) {
    GdiFlush();
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = canvas.w;
    info.biHeight = -canvas.h;
    info.biPlanes = 1;
    info.biBitCount = 32;
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + canvas.stride * canvas.h;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(reinterpret_cast<const char*>(canvas.px), canvas.stride * canvas.h);
    return bool(output);
}

void populate(App& app) {
    for (int i = 0; i < 14; ++i) {
        Profile profile;
        profile.name = i == 0 ? "Production / application" : i == 1 ? "Ubuntu / development" : "Server " + std::to_string(i + 1);
        profile.host = i == 0 ? "app-01.example.com" : "dev-" + std::to_string(i) + ".example.com";
        profile.user = "demo";
        profile.src = i < 3 ? 0 : 1;
        app.profiles.push_back(profile);
    }
    app.download_dir = "C:\\Users\\demo\\Downloads";
    for (int i = 0; i < 3; ++i) {
        auto session = std::make_shared<Session>();
        session->ui_scope = uint64_t(i + 1);
        session->title = i == 0 ? "Production" : i == 1 ? "Ubuntu dev" : "Logs";
        session->host = app.profiles[size_t(i)].host;
        session->user = "demo";
        session->sftp_path = "/home/demo";
        for (int n = 0; n < 30; ++n) {
            Entry entry;
            entry.name = n == 0 ? "applications" : n == 1 ? "documents" : n == 2 ?
                "deployment-configuration-with-a-very-long-file-name.yaml" : "report-" + std::to_string(n) + ".log";
            entry.dir = n < 2;
            entry.size = uint64_t(n + 1) * 1024;
            entry.mtime = 1780000000;
            session->entries.push_back(entry);
        }
        const std::string output = "\x1b[36mOffline layout preview -- no SSH connection\x1b[0m\r\n\r\n"
            "\x1b[32mdemo@ubuntu\x1b[0m:~$ ls -lh\r\n"
            "total 24K\r\n"
            "drwxr-xr-x  4 demo demo 4.0K Nov 14 10:20 applications\r\n"
            "drwxr-xr-x  2 demo demo 4.0K Nov 14 10:20 documents\r\n"
            "-rw-r--r--  1 demo demo  826 Nov 14 10:20 deploy.yaml\r\n\r\n"
            "\x1b[32mdemo@ubuntu\x1b[0m:~$ ";
        session->term.feed(output.data(), output.size());
        app.tabs.push_back(session);
    }
    app.active = 1;
}

bool text_pixels_fit(const aino::detail::Canvas& canvas, float scale, const Rect& bounds, Color background) {
    GdiFlush();
    int ink = 0;
    for (int y = 0; y < canvas.h; ++y) for (int x = 0; x < canvas.w; ++x) {
        const auto* pixel = canvas.px + y * canvas.stride + x * 4;
        if (pixel[0] == background.b && pixel[1] == background.g && pixel[2] == background.r) continue;
        ++ink;
        if (x < int(std::floor(bounds.x * scale)) - 1 || x > int(std::ceil(bounds.right() * scale)) + 1 ||
            y < int(std::floor(bounds.y * scale)) - 1 || y > int(std::ceil(bounds.bottom() * scale)) + 1) return false;
    }
    return ink > 40;
}
}

int main() {
    struct IsolatedHome {
        std::string old;
        std::filesystem::path root;
        bool had_old = false;
        IsolatedHome() {
            if (const char* value = std::getenv("AINO_SSH_HOME")) { old = value; had_old = true; }
            root = std::filesystem::temp_directory_path() / ("aino-layout-" + std::to_string(GetCurrentProcessId()));
            _putenv_s("AINO_SSH_HOME", root.u8string().c_str());
        }
        ~IsolatedHome() { _putenv_s("AINO_SSH_HOME", had_old ? old.c_str() : ""); }
    } isolated;
    const char* directory = std::getenv("AINO_SSH_LAYOUT_DIR");
    if (directory) std::filesystem::create_directories(std::filesystem::u8path(directory));
    for (int dpi : {96, 144}) for (bool small : {false, true}) for (bool dark : {false, true}) {
        const int width = small ? 980 : 1320, height = small ? 600 : 820;
        aino::WindowConfig config;
        config.width = width;
        config.height = height;
        config.min_width = config.min_height = 1;
        config.title = "aino SSH offline layout check";
        aino::Window window(config);
        if (!window.ok()) return 2;
        window.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
        auto* native = static_cast<aino::detail::Win32State*>(window.impl->native);
        RECT outer{}, client{};
        GetWindowRect(native->hwnd, &outer);
        GetClientRect(native->hwnd, &client);
        outer.right = outer.left + width * dpi / 96 + (outer.right - outer.left) - client.right;
        outer.bottom = outer.top + height * dpi / 96 + (outer.bottom - outer.top) - client.bottom;
        SendMessageW(native->hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&outer));
        if (!window.poll()) return 3;
        App app;
        populate(app);
        for (int mode = 0; mode < 4; ++mode) {
            app.show_start = mode == 0;
            app.sidebar_page = mode == 1 ? 1 : 0;
            app.sidebar_open = mode != 3;
            const int frames = directory ? 16 : 2;
            for (int frame_index = 0; frame_index < frames; ++frame_index) {
                if (directory && frame_index) std::this_thread::sleep_for(std::chrono::milliseconds(16));
                window.render([&](aino::Ui& ui) {
                ui.clear(ui.theme().background);
                draw_workspace(ui, app, ui.theme());
                const auto layout = workspace_layout(ui, app);
                expect(layout.content.right() <= ui.width() + .1f && layout.content.bottom() <= ui.height() + .1f,
                    "workspace stays inside client bounds");
                expect(layout.content.w >= 460, "terminal retains usable width");
                if (mode == 1) expect(app.tabs[1]->r_list.bottom() <= ui.height() - 180,
                    "file list leaves space for fixed actions");
                const Rect row(0, 0, app.sidebar_open ? layout.sidebar.w - 24 : 276, 0);
                const auto measured = connection_row(ui, app.profiles[0], 0, row.w, true);
                for (const auto& line : measured.title)
                    expect(ui.text_width(line, 12.5f, 600) <= row.w - 100 + .1f, "sidebar label fits its text column");
                });
            }
            if (directory) {
                const std::string name = std::string(dark ? "dark" : "light") + (small ? "-min-" : "-default-") +
                    std::to_string(dpi) + "-" + std::to_string(mode) + ".bmp";
                expect(save_bitmap(window.impl->canvas, std::filesystem::u8path(directory) / name), "native screenshot saved");
            }
        }
        Profile long_name;
        long_name.name = "Long server name / " + std::string(80, 'w') + " 中文连接名称";
        long_name.host = std::string(110, 'w') + ".example.com";
        long_name.user = "demo";
        Rect ink_bounds;
        const Color background = Color::hex(0x092347);
        window.render([&](aino::Ui& ui) {
            ui.clear(background);
            const auto measured = connection_row(ui, long_name, 0, 276, true);
            const Rect row(20, 20, 276, measured.height);
            ink_bounds = {row.x + 43, row.y + 11, row.w - 98, row.h - 24};
            draw_connection_text(ui, measured, row, ui.theme(), true);
        });
        expect(text_pixels_fit(window.impl->canvas, window.impl->scale, ink_bounds, background),
            "actual long-label pixels remain within padded text bounds");
        std::printf("layout %s %dx%d %d DPI checked\n", dark ? "dark" : "light", width, height, dpi);
        app.shutdown_all();
    }
    std::printf("[ssh-layout] %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
