// SSH 示例的系统凭据适配，不进入 aino GUI 核心，也不引入第三方构建依赖。
#pragma once
#include <string>
#include <vector>
#include <random>
#include <cstdint>
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#else
#include <dlfcn.h>
#endif

namespace sshc {
inline std::string credential_hex(const unsigned char* data, size_t size) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) { out += hex[data[i] >> 4]; out += hex[data[i] & 15]; }
    return out;
}
inline bool credential_unhex(const std::string& value, std::vector<unsigned char>& bytes) {
    if (value.size() % 2 || value.size() > (1u << 20)) return false;
    auto digit = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    bytes.clear(); bytes.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int hi = digit(value[i]), lo = digit(value[i + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}

#if !defined(_WIN32) && !defined(__APPLE__)
// 用公开 libsecret 的不透明 schema 与 varargs 接口，避免依赖 GLib 头或结构布局。
struct SecretStore {
    void* library = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
    void* schema = nullptr;
    void* (*schema_new)(const char*, int, ...) = nullptr;
    void (*schema_unref)(void*) = nullptr;
    int (*store)(const void*, const char*, const char*, const char*, void*, void**, ...) = nullptr;
    int (*clear)(const void*, void*, void**, ...) = nullptr;
    char* (*lookup)(const void*, void*, void**, ...) = nullptr;
    void (*free_password)(char*) = nullptr;
    SecretStore() {
        if (!library) return;
        schema_new = reinterpret_cast<decltype(schema_new)>(dlsym(library, "secret_schema_new"));
        schema_unref = reinterpret_cast<decltype(schema_unref)>(dlsym(library, "secret_schema_unref"));
        store = reinterpret_cast<decltype(store)>(dlsym(library, "secret_password_store_sync"));
        clear = reinterpret_cast<decltype(clear)>(dlsym(library, "secret_password_clear_sync"));
        lookup = reinterpret_cast<decltype(lookup)>(dlsym(library, "secret_password_lookup_sync"));
        free_password = reinterpret_cast<decltype(free_password)>(dlsym(library, "secret_password_free"));
        if (schema_new && schema_unref && store && clear && lookup && free_password)
            schema = schema_new("org.aino.SSH", 0, "profile", 0, static_cast<const char*>(nullptr));
    }
    ~SecretStore() { if (schema) schema_unref(schema); if (library) dlclose(library); }
    SecretStore(const SecretStore&) = delete;
    SecretStore& operator=(const SecretStore&) = delete;
};
inline SecretStore& secret_store() { static SecretStore store; return store; }
#endif

inline bool protect_password(const std::string& identity, const std::string& password,
                             std::string* stored, std::string* error) {
    if (error) error->clear();
    if (password.empty()) { stored->clear(); return true; }
    if (password.size() > 65536) { if (error) *error = "密码过长，无法保存"; return false; }
    if (password.find('\0') != std::string::npos) {
        if (error) *error = "密码包含不支持的空字符，未保存密码";
        return false;
    }
#if defined(_WIN32)
    (void)identity;
    DATA_BLOB source{DWORD(password.size()), reinterpret_cast<BYTE*>(const_cast<char*>(password.data()))};
    DATA_BLOB result{};
    if (CryptProtectData(&source, L"aino SSH", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)) {
        try { *stored = "dpapi:" + credential_hex(result.pbData, result.cbData); }
        catch (...) { LocalFree(result.pbData); throw; }
        LocalFree(result.pbData);
        return true;
    }
#else
    // 每次保存创建新版本；配置原子替换失败时不能覆盖仍被旧配置引用的凭据。
    std::string unique = identity;
    try {
        std::random_device random;
        for (int i = 0; i < 4; ++i) {
            const uint32_t value = random();
            unique += credential_hex(reinterpret_cast<const unsigned char*>(&value), sizeof(value));
        }
    } catch (...) { if (error) *error = "系统随机源不可用，未保存密码"; return false; }
    const std::string key = credential_hex(reinterpret_cast<const unsigned char*>(unique.data()), unique.size());
#if defined(__APPLE__)
    const char service[] = "org.aino.SSH";
    SecKeychainItemRef item = nullptr;
    OSStatus rc = SecKeychainFindGenericPassword(nullptr, UInt32(sizeof(service) - 1), service,
        UInt32(key.size()), key.data(), nullptr, nullptr, &item);
    if (rc == errSecSuccess) {
        rc = SecKeychainItemModifyAttributesAndData(item, nullptr, UInt32(password.size()), password.data());
        CFRelease(item);
    } else if (rc == errSecItemNotFound) {
        rc = SecKeychainAddGenericPassword(nullptr, UInt32(sizeof(service) - 1), service,
            UInt32(key.size()), key.data(), UInt32(password.size()), password.data(), nullptr);
    }
    if (rc == errSecSuccess) { *stored = "vault:" + key; return true; }
#else
    auto& vault = secret_store();
    if (vault.schema && vault.store(vault.schema, "default", "aino SSH", password.c_str(), nullptr,
        nullptr, "profile", key.c_str(), static_cast<const char*>(nullptr))) {
        *stored = "vault:" + key; return true;
    }
#endif
#endif
    if (error) *error = "系统凭据存储不可用或访问被拒绝；未保存密码，请解锁系统凭据服务或取消记住密码";
    return false;
}

// 只清理本示例 schema 下的引用；DPAPI 的密文随配置文件替换而释放。
inline bool erase_password(const std::string& stored) {
    if (stored.rfind("vault:", 0) != 0) return true;
#if defined(_WIN32)
    return true;
#else
    const std::string key = stored.substr(6);
#if defined(__APPLE__)
    const char service[] = "org.aino.SSH";
    SecKeychainItemRef item = nullptr;
    OSStatus rc = SecKeychainFindGenericPassword(nullptr, UInt32(sizeof(service) - 1), service,
        UInt32(key.size()), key.data(), nullptr, nullptr, &item);
    if (rc == errSecItemNotFound) return true;
    if (rc != errSecSuccess) return false;
    rc = SecKeychainItemDelete(item); CFRelease(item); return rc == errSecSuccess;
#else
    auto& vault = secret_store();
    return vault.schema && vault.clear(vault.schema, nullptr, nullptr,
        "profile", key.c_str(), static_cast<const char*>(nullptr));
#endif
#endif
}

inline bool restore_password(const std::string& stored, std::string* password) {
    password->clear();
    if (stored.empty()) return true;
#if defined(_WIN32)
    if (stored.rfind("dpapi:", 0) != 0) return false;
    std::vector<unsigned char> bytes;
    if (!credential_unhex(stored.substr(6), bytes)) return false;
    DATA_BLOB source{DWORD(bytes.size()), bytes.data()}, result{};
    if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)) return false;
    try { password->assign(reinterpret_cast<const char*>(result.pbData), result.cbData); }
    catch (...) { SecureZeroMemory(result.pbData, result.cbData); LocalFree(result.pbData); throw; }
    SecureZeroMemory(result.pbData, result.cbData);
    LocalFree(result.pbData);
    return true;
#else
    if (stored.rfind("vault:", 0) != 0) return false;
    const std::string key = stored.substr(6);
#if defined(__APPLE__)
    const char service[] = "org.aino.SSH";
    UInt32 size = 0;
    void* data = nullptr;
    const OSStatus rc = SecKeychainFindGenericPassword(nullptr, UInt32(sizeof(service) - 1), service,
        UInt32(key.size()), key.data(), &size, &data, nullptr);
    if (rc != errSecSuccess) return false;
    try { password->assign(static_cast<const char*>(data), size); }
    catch (...) { SecKeychainItemFreeContent(nullptr, data); throw; }
    SecKeychainItemFreeContent(nullptr, data);
    return true;
#else
    auto& vault = secret_store();
    if (!vault.schema) return false;
    char* value = vault.lookup(vault.schema, nullptr, nullptr, "profile", key.c_str(), static_cast<const char*>(nullptr));
    if (!value) return false;
    try { *password = value; }
    catch (...) { vault.free_password(value); throw; }
    vault.free_password(value);
    return true;
#endif
#endif
}
} // namespace sshc
