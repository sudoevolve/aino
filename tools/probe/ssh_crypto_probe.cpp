// 验证链接进程序的库，而非头文件声明；不访问网络、配置或凭据。
#include <libssh2.h>
#include <cstdio>
#include <cstring>

int main() {
    if (libssh2_init(0) != 0) return 1;
    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) { libssh2_exit(); return 1; }
    const char** methods = nullptr;
    const int count = libssh2_session_supported_algs(session, LIBSSH2_METHOD_KEX, &methods);
    bool supported = false;
    for (int i = 0; i < count; ++i) {
        const char* name = methods[i];
        if (std::strcmp(name, "curve25519-sha256") == 0 ||
            std::strcmp(name, "curve25519-sha256@libssh.org") == 0 ||
            std::strncmp(name, "ecdh-sha2-nistp", 15) == 0)
            supported = true;
    }
    if (methods) libssh2_free(session, const_cast<char**>(methods));
    libssh2_session_free(session);
    libssh2_exit();
    if (!supported) {
        std::puts("FAIL: linked libssh2 has no Curve25519/ECDH key exchange.");
        std::puts("For WinCNG, rebuild libssh2 with -DENABLE_ECDSA_WINCNG=ON (Windows 10+).");
        return 1;
    }
    std::puts("[ssh-crypto] linked libssh2 supports Curve25519/ECDH; PASS");
    return 0;
}
