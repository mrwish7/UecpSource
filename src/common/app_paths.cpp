#include "app_paths.h"

#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace ucs {

std::string GetExecutableDirectory() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return "";

    std::wstring wpath(buf, len);
    size_t slash = wpath.find_last_of(L"\\/");
    std::wstring wdir = (slash != std::wstring::npos) ? wpath.substr(0, slash + 1) : L"";

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), static_cast<int>(wdir.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return "";
    std::string dir(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), static_cast<int>(wdir.size()), &dir[0], size_needed,
                        nullptr, nullptr);
    return dir;
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = 0;
    std::string path(buf);
    size_t slash = path.find_last_of('/');
    return (slash != std::string::npos) ? path.substr(0, slash + 1) : "";
#endif
}

std::string ResolveConfigPath(int argc, char** argv, const std::string& default_filename) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            return argv[i + 1];
        }
    }
    return GetExecutableDirectory() + default_filename;
}

} // namespace ucs
