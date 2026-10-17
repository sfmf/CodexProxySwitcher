#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <appmodel.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cwctype>
#include <shlobj.h>
#include "version.h"

using std::wstring;
static HINSTANCE instance;
static HWND window, portEdit, pathEdit, statusLabel;
static HFONT font;
static wstring directory, configPath;
static bool busy = false;
static constexpr UINT Finished = WM_APP + 1;
static constexpr int Port = 101, Path = 102, Browse = 103, Save = 104,
    Native = 105, Proxy = 106, OpenFolder = 107, Shortcut = 108;
static const wchar_t* keys[] = {L"HTTP_PROXY", L"HTTPS_PROXY", L"ALL_PROXY", L"NO_PROXY"};

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

static wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, (LPWSTR)&buffer, 0, nullptr);
    wstring text = buffer ? buffer : L"Windows API 调用失败";
    if (buffer) LocalFree(buffer);
    return text + L" (" + std::to_wstring(code) + L")";
}
struct Failure { wstring message; };
static void checkHr(HRESULT hr, const wchar_t* action) {
    if (FAILED(hr)) throw Failure{wstring(action) + L"：" + errorText((DWORD)hr)};
}
struct ComSession {
    ComSession() { checkHr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), L"初始化 COM"); }
    ~ComSession() { CoUninitialize(); }
};
template<class T> struct ComPtr {
    T* value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
};
static void check(bool ok, const wchar_t* action) {
    if (!ok) throw Failure{wstring(action) + L"：" + errorText(GetLastError())};
}
static wstring executable() {
    std::vector<wchar_t> buffer(32768);
    DWORD n = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    check(n && n < buffer.size(), L"读取执行文件路径");
    return wstring(buffer.data(), n);
}
static void createProxyShortcut(const wstring& destination) {
    ComPtr<IShellLinkW> link;
    checkHr(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, (void**)&link.value), L"创建快捷方式");
    wstring exe = executable();
    wstring folder = exe.substr(0, exe.find_last_of(L"\\/"));
    checkHr(link.value->SetPath(exe.c_str()), L"设置快捷方式目标");
    checkHr(link.value->SetArguments(L"--proxy"), L"设置代理启动参数");
    checkHr(link.value->SetWorkingDirectory(folder.c_str()), L"设置快捷方式目录");
    checkHr(link.value->SetDescription(L"按已保存配置直接以代理模式启动 Codex"), L"设置快捷方式说明");
    checkHr(link.value->SetIconLocation(exe.c_str(), 0), L"设置快捷方式图标");
    checkHr(link.value->SetShowCmd(SW_SHOWNORMAL), L"设置启动方式");
    ComPtr<IPersistFile> file;
    checkHr(link.value->QueryInterface(IID_IPersistFile, (void**)&file.value), L"保存快捷方式");
    checkHr(file.value->Save(destination.c_str(), TRUE), L"保存快捷方式");
}
static void chooseProxyShortcut() {
    PWSTR desktop = nullptr;
    wstring initial;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) {
        initial = desktop;
        CoTaskMemFree(desktop);
    }
    wchar_t path[32768] = L"Codex 代理启动.lnk";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrTitle = L"保存代理启动快捷方式";
    dialog.lpstrFilter = L"Windows 快捷方式 (*.lnk)\0*.lnk\0\0";
    dialog.lpstrDefExt = L"lnk";
    dialog.lpstrInitialDir = initial.empty() ? directory.c_str() : initial.c_str();
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) {
        createProxyShortcut(path);
        SetWindowTextW(statusLabel, L"快捷方式已生成；双击即可直接代理启动，无需打开此界面。");
    } else if (DWORD error = CommDlgExtendedError()) {
        throw Failure{L"打开快捷方式保存窗口失败：" + errorText(error)};
    }
}
static bool exists(const wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool validPort(const wstring& text) {
    if (text.empty() || text.size() > 5) return false;
    unsigned value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        value = value * 10 + c - L'0';
    }
    return value >= 1 && value <= 65535;
}
struct Settings { wstring port = L"7897", path; };
static Settings loadSettings(const wstring& file) {
    Settings s;
    wchar_t p[32], exe[32768];
    GetPrivateProfileStringW(L"Launcher", L"ProxyPort", L"7897", p, 32, file.c_str());
    GetPrivateProfileStringW(L"Launcher", L"CodexPath", L"", exe, 32768, file.c_str());
    s.port = p;
    s.path = exe;
    if (!validPort(s.port)) throw Failure{L"配置文件中的端口无效，请改为 1–65535：\n" + file};
    return s;
}
static void saveSettings(const wstring& file, const Settings& s) {
    // Atomic replacement prevents a failed write from destroying the previous settings.
    wstring temp = file + L".tmp";
    wstring data = L"\xFEFF[Launcher]\r\nProxyPort=" + s.port +
        L"\r\nCodexPath=" + s.path + L"\r\n";
    {
        Handle h(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        check(h.value != INVALID_HANDLE_VALUE, L"写入同目录配置（请将程序放在可写文件夹）");
        DWORD written = 0, bytes = (DWORD)(data.size() * sizeof(wchar_t));
        check(WriteFile(h.value, data.data(), bytes, &written, nullptr) && written == bytes,
            L"保存配置");
        check(FlushFileBuffers(h.value), L"刷新配置");
    }
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD code = GetLastError();
        DeleteFileW(temp.c_str());
        throw Failure{L"替换配置失败：" + errorText(code)};
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
}

struct Install { wstring path, appId; };
static Install findInstall(const wstring& configured) {
    if (!configured.empty()) {
        if (!exists(configured)) throw Failure{L"指定的程序不存在。"};
        auto pos = configured.find_last_of(L"\\/");
        wstring name = configured.substr(pos == wstring::npos ? 0 : pos + 1);
        if (_wcsicmp(name.c_str(), L"Codex.exe") && _wcsicmp(name.c_str(), L"ChatGPT.exe"))
            throw Failure{L"请选择 Codex.exe 或 ChatGPT.exe。"};
        // Resolve a configured Store executable to its AUMID too.
    }
    UINT32 count = 0, size = 0;
    const wchar_t* family = L"OpenAI.Codex_2p2nqsd0c76g0";
    if (GetPackagesByPackageFamily(family, &count, nullptr, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<PWSTR> names(count);
        std::vector<wchar_t> buffer(size);
        if (GetPackagesByPackageFamily(family, &count, names.data(), &size, buffer.data()) == ERROR_SUCCESS) {
            UINT64 bestVersion = 0;
            Install best;
            for (UINT32 i = 0; i < count; ++i) {
                UINT32 length = 0;
                if (GetPackagePathByFullName(names[i], &length, nullptr) != ERROR_INSUFFICIENT_BUFFER) continue;
                std::vector<wchar_t> path(length);
                if (GetPackagePathByFullName(names[i], &length, path.data()) != ERROR_SUCCESS) continue;
                wstring exe = wstring(path.data()) + L"\\app\\Codex.exe";
                if (!exists(exe)) exe = wstring(path.data()) + L"\\app\\ChatGPT.exe";
                if (!exists(exe)) continue;
                if (!configured.empty() && _wcsicmp(exe.c_str(), configured.c_str())) continue;
                PACKAGE_INFO_REFERENCE info{};
                if (OpenPackageInfoByFullName(names[i], 0, &info) != ERROR_SUCCESS) continue;
                UINT32 bytes = 0, apps = 0;
                wstring id;
                if (GetPackageApplicationIds(info, &bytes, nullptr, &apps) == ERROR_INSUFFICIENT_BUFFER) {
                    std::vector<BYTE> ids(bytes);
                    if (GetPackageApplicationIds(info, &bytes, ids.data(), &apps) == ERROR_SUCCESS && apps) {
                        auto values = reinterpret_cast<PWSTR*>(ids.data());
                        id = values[0];
                        for (UINT32 j = 0; j < apps; ++j)
                            if (wstring(values[j]) == wstring(family) + L"!App") id = values[j];
                    }
                }
                ClosePackageInfo(info);
                UINT64 version = 0;
                UINT32 infoBytes = 0;
                if (PackageIdFromFullName(names[i], 0, &infoBytes, nullptr) == ERROR_INSUFFICIENT_BUFFER) {
                    std::vector<BYTE> package(infoBytes);
                    if (PackageIdFromFullName(names[i], 0, &infoBytes, package.data()) == ERROR_SUCCESS)
                        version = reinterpret_cast<PACKAGE_ID*>(package.data())->version.Version;
                }
                if (!id.empty() && (best.appId.empty() || version > bestVersion)) {
                    best = {exe, id}; bestVersion = version;
                }
            }
            if (!best.appId.empty()) return best;
        }
    }
    if (!configured.empty()) return {configured, L""};
    throw Failure{L"未找到 Windows 商店版 Codex。可手动选择 Codex.exe 或 ChatGPT.exe，然后保存。"};
}

static void broadcast() {
    DWORD_PTR result;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
        SMTO_ABORTIFHUNG, 1000, &result);
}
struct SavedValue {
    bool processExists = false, registryExists = false;
    wstring process;
    DWORD type = REG_SZ;
    std::vector<BYTE> data;
};
class Environment {
    SavedValue saved[4];
    HKEY registry = nullptr;
    bool applied = false;
public:
    explicit Environment(bool user) {
        if (user) {
            LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0,
                KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &registry, nullptr);
            if (rc != ERROR_SUCCESS) throw Failure{L"打开用户环境变量失败：" + errorText(rc)};
        }
        for (int i = 0; i < 4; ++i) {
            SetLastError(ERROR_SUCCESS);
            DWORD n = GetEnvironmentVariableW(keys[i], nullptr, 0);
            saved[i].processExists = n || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
            if (n) {
                std::vector<wchar_t> buffer(n);
                GetEnvironmentVariableW(keys[i], buffer.data(), n);
                saved[i].process = buffer.data();
            }
            if (registry) {
                DWORD bytes = 0;
                LONG rc = RegQueryValueExW(registry, keys[i], nullptr, &saved[i].type, nullptr, &bytes);
                if (rc == ERROR_SUCCESS) {
                    saved[i].registryExists = true;
                    saved[i].data.resize(bytes);
                    rc = RegQueryValueExW(registry, keys[i], nullptr, &saved[i].type,
                        saved[i].data.data(), &bytes);
                }
                if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
                    RegCloseKey(registry); registry = nullptr;
                    throw Failure{L"备份用户环境变量失败：" + errorText(rc)};
                }
            }
        }
    }
    void apply(bool proxy, const wstring& port) {
        applied = true;
        wstring url = L"http://127.0.0.1:" + port;
        for (int i = 0; i < 4; ++i) {
            const wchar_t* value = proxy ? (i == 3 ? L"localhost,127.0.0.1,::1" : url.c_str()) : nullptr;
            check(SetEnvironmentVariableW(keys[i], value), L"设置启动环境");
            if (registry) {
                LONG rc = value ? RegSetValueExW(registry, keys[i], 0, REG_SZ,
                    (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t))) :
                    RegDeleteValueW(registry, keys[i]);
                if (rc != ERROR_SUCCESS && !(value == nullptr && rc == ERROR_FILE_NOT_FOUND))
                    throw Failure{L"设置临时用户环境失败：" + errorText(rc)};
            }
        }
        if (registry) broadcast();
    }
    bool restore() noexcept {
        if (!applied) return true;
        bool ok = true;
        for (int i = 0; i < 4; ++i) {
            if (!SetEnvironmentVariableW(keys[i], saved[i].processExists ? saved[i].process.c_str() : nullptr)) ok = false;
            if (registry) {
                LONG rc = saved[i].registryExists ? RegSetValueExW(registry, keys[i], 0,
                    saved[i].type, saved[i].data.data(), (DWORD)saved[i].data.size()) :
                    RegDeleteValueW(registry, keys[i]);
                if (rc != ERROR_SUCCESS && !(rc == ERROR_FILE_NOT_FOUND && !saved[i].registryExists)) ok = false;
            }
        }
        if (registry) broadcast();
        applied = !ok;
        return ok;
    }
    ~Environment() { restore(); if (registry) RegCloseKey(registry); }
};

static bool codexRunning() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.value, &entry)) do {
        if (!_wcsicmp(entry.szExeFile, L"Codex.exe") ||
            !_wcsicmp(entry.szExeFile, L"ChatGPT.exe")) return true;
    } while (Process32NextW(snapshot.value, &entry));
    return false;
}
static void launch(const Settings& s, bool proxy) {
    Install app = findInstall(s.path);
    // Existing Electron instances can reuse the previous environment. Ask the user
    // to close them instead of terminating sessions or other users' processes.
    if (codexRunning()) throw Failure{L"Codex 正在运行。请先退出 Codex，再启动以使网络模式生效。"};
    Environment environment(!app.appId.empty());
    environment.apply(proxy, s.port);
    // Chromium networking does not use the backend's proxy environment variables.
    // Pass this to both Store activation and direct executable launches.
    wstring arguments = proxy ? L"--proxy-server=http://127.0.0.1:" + s.port : L"";
    if (!app.appId.empty()) {
        IApplicationActivationManager* manager = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
            IID_IApplicationActivationManager, (void**)&manager);
        if (SUCCEEDED(hr)) {
            DWORD pid = 0;
            hr = manager->ActivateApplication(app.appId.c_str(),
                arguments.empty() ? nullptr : arguments.c_str(), AO_NONE, &pid);
            manager->Release();
        }
        if (FAILED(hr)) throw Failure{L"激活 Codex 失败：" + errorText((DWORD)hr)};
    } else {
        wstring command = L"\"" + app.path + L"\"";
        if (!arguments.empty()) command += L" " + arguments;
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        wstring working = app.path.substr(0, app.path.find_last_of(L"\\/"));
        check(CreateProcessW(app.path.c_str(), &command[0], nullptr, nullptr, FALSE, 0,
            nullptr, working.c_str(), &si, &pi), L"启动 Codex");
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    if (!environment.restore()) throw Failure{L"Codex 已启动，但恢复环境变量失败。请检查当前用户的代理环境变量。"};
}

static wstring controlText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::vector<wchar_t> buffer(n + 1);
    GetWindowTextW(h, buffer.data(), n + 1);
    return buffer.data();
}
static Settings readControls() {
    Settings s{controlText(portEdit), controlText(pathEdit)};
    if (!validPort(s.port)) throw Failure{L"请输入 1–65535 之间的端口号。"};
    while (!s.path.empty() && iswspace(s.path.front())) s.path.erase(0, 1);
    while (!s.path.empty() && iswspace(s.path.back())) s.path.pop_back();
    if (s.path.size() >= 2 && s.path.front() == L'"' && s.path.back() == L'"')
        s.path = s.path.substr(1, s.path.size() - 2);
    if (s.path.find_first_of(L"\r\n") != wstring::npos) throw Failure{L"文件路径不能包含换行。"};
    if (!s.path.empty()) {
        if (s.path.size() < 3 || !(s.path[1] == L':' || s.path.substr(0, 2) == L"\\\\"))
            throw Failure{L"请填写完整的程序路径，或留空自动查找。"};
        findInstall(s.path);
    }
    return s;
}
struct Work { Settings settings; bool proxy; wstring error; };
static DWORD WINAPI worker(void* data) {
    auto task = (Work*)data;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        if (FAILED(hr)) throw Failure{L"初始化 COM 失败：" + errorText((DWORD)hr)};
        launch(task->settings, task->proxy);
    } catch (const Failure& e) { task->error = e.message; }
      catch (...) { task->error = L"启动时发生未知错误。"; }
    if (SUCCEEDED(hr)) CoUninitialize();
    PostMessageW(window, Finished, 0, (LPARAM)task);
    return 0;
}
static void showError(const wstring& message) {
    MessageBoxW(window, message.c_str(), L"Codex Proxy Switcher", MB_OK | MB_ICONERROR);
}
static void setBusy(bool value) {
    busy = value;
    for (int id : {Port, Path, Browse, Save, Native, Proxy, OpenFolder, Shortcut})
        EnableWindow(GetDlgItem(window, id), !value);
    EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | (value ? MF_GRAYED : MF_ENABLED));
}
static HWND add(const wchar_t* cls, const wchar_t* text, DWORD style,
    int x, int y, int w, int h, int id = 0) {
    HWND child = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0,
        cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        window, (HMENU)(INT_PTR)id, instance, nullptr);
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    return child;
}
static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        window = hwnd;
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        add(L"STATIC", L"Codex Proxy Switcher", 0, 24, 20, 510, 28);
        add(L"STATIC", L"本地 HTTP 代理端口", 0, 24, 64, 180, 24);
        portEdit = add(L"EDIT", L"7897", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 210, 60, 120, 30, Port);
        SendMessageW(portEdit, EM_SETLIMITTEXT, 5, 0);
        add(L"STATIC", L"程序路径（Codex.exe / ChatGPT.exe，留空自动查找）", 0, 24, 108, 510, 24);
        pathEdit = add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 24, 138, 442, 30, Path);
        SendMessageW(pathEdit, EM_SETLIMITTEXT, 32000, 0);
        add(L"BUTTON", L"浏览…", WS_TABSTOP, 478, 137, 78, 32, Browse);
        add(L"BUTTON", L"原生启动", WS_TABSTOP, 24, 194, 254, 48, Native);
        add(L"BUTTON", L"代理启动", WS_TABSTOP | BS_DEFPUSHBUTTON, 302, 194, 254, 48, Proxy);
        add(L"BUTTON", L"保存配置", WS_TABSTOP, 24, 264, 120, 34, Save);
        add(L"BUTTON", L"打开程序目录", WS_TABSTOP, 158, 264, 150, 34, OpenFolder);
        add(L"BUTTON", L"创建代理启动快捷方式", WS_TABSTOP, 322, 264, 234, 34, Shortcut);
        statusLabel = add(L"STATIC", L"配置保存在 EXE 同目录；启动前请先退出 Codex。", 0, 24, 316, 420, 42);
        add(L"STATIC", L"v" APP_VERSION_W, SS_RIGHT, 456, 316, 100, 42);
        try {
            Settings s = loadSettings(configPath);
            SetWindowTextW(portEdit, s.port.c_str());
            SetWindowTextW(pathEdit, s.path.c_str());
            if (!exists(configPath)) saveSettings(configPath, s);
        } catch (const Failure& e) { showError(e.message); }
        return 0;
    }
    case WM_COMMAND:
        if (busy || HIWORD(wp) != BN_CLICKED) break;
        try {
            switch (LOWORD(wp)) {
            case Browse: {
                wchar_t file[32768]{};
                OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = hwnd;
                dialog.lpstrFilter = L"Codex / ChatGPT executable\0Codex.exe;ChatGPT.exe\0\0";
                dialog.lpstrFile = file; dialog.nMaxFile = 32768;
                dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&dialog)) SetWindowTextW(pathEdit, file);
                break;
            }
            case Save:
                saveSettings(configPath, readControls());
                SetWindowTextW(statusLabel, L"已保存到 EXE 同目录的 CodexProxySwitcher.ini。");
                break;
            case Shortcut:
                saveSettings(configPath, readControls());
                chooseProxyShortcut();
                break;
            case OpenFolder:
                if ((INT_PTR)ShellExecuteW(hwnd, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
                    throw Failure{L"无法打开程序目录。"};
                break;
            case Native: case Proxy: {
                Settings s = readControls();
                saveSettings(configPath, s);
                auto task = new Work{s, LOWORD(wp) == Proxy, L""};
                setBusy(true);
                SetWindowTextW(statusLabel, L"正在查找并启动 Codex，请稍候…");
                HANDLE thread = CreateThread(nullptr, 0, worker, task, 0, nullptr);
                if (!thread) { delete task; setBusy(false); check(false, L"创建启动线程"); }
                CloseHandle(thread);
                break;
            }
            }
        } catch (const Failure& e) { showError(e.message); }
        return 0;
    case Finished: {
        auto task = (Work*)lp;
        setBusy(false);
        if (task->error.empty()) { delete task; DestroyWindow(hwnd); }
        else {
            SetWindowTextW(statusLabel, L"启动未完成，请检查提示后重试。");
            showError(task->error); delete task;
        }
        return 0;
    }
    case WM_CLOSE: if (!busy) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: DeleteObject(font); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int selfTest() {
    // No Codex launch or user-registry mutation in this test.
    if (!validPort(L"1") || !validPort(L"65535") || validPort(L"0") ||
        validPort(L"65536") || validPort(L"12x") || validPort(L"")) return 1;
    wchar_t root[32768], temp[32768];
    if (!GetTempPathW(32768, root) || !GetTempFileNameW(root, L"cps", 0, temp)) return 2;
    int result = 0;
    try {
        Settings s{L"12345", L"C:\\测试 文件夹\\Codex.exe"};
        saveSettings(temp, s);
        Settings read = loadSettings(temp);
        if (read.port != s.port || read.path != s.path) result = 3;
        s = {L"7897", L""}; saveSettings(temp, s);
        read = loadSettings(temp);
        if (read.port != s.port || !read.path.empty()) result = 4;
        wchar_t before[32768]{};
        DWORD n = GetEnvironmentVariableW(L"HTTP_PROXY", before, 32768);
        {
            Environment env(false); env.apply(true, L"12345");
            wchar_t value[128]{}; GetEnvironmentVariableW(L"HTTP_PROXY", value, 128);
            if (wstring(value) != L"http://127.0.0.1:12345") result = 5;
            if (!env.restore()) result = 6;
        }
        wchar_t after[32768]{};
        DWORD m = GetEnvironmentVariableW(L"HTTP_PROXY", after, 32768);
        if (n != m || wstring(before) != after) result = 7;
        // Verify the actual persisted .lnk, including paths and proxy argument.
        ComSession com;
        wstring shortcut = wstring(temp) + L".lnk";
        try {
            createProxyShortcut(shortcut);
            ComPtr<IShellLinkW> link;
            checkHr(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                IID_IShellLinkW, (void**)&link.value), L"读取测试快捷方式");
            ComPtr<IPersistFile> file;
            checkHr(link.value->QueryInterface(IID_IPersistFile, (void**)&file.value), L"读取测试快捷方式");
            checkHr(file.value->Load(shortcut.c_str(), STGM_READ), L"加载测试快捷方式");
            wchar_t target[32768]{}, args[128]{}, icon[32768]{}, working[32768]{};
            int index = -1;
            checkHr(link.value->GetPath(target, 32768, nullptr, SLGP_RAWPATH), L"读取目标");
            checkHr(link.value->GetArguments(args, 128), L"读取参数");
            checkHr(link.value->GetIconLocation(icon, 32768, &index), L"读取图标");
            checkHr(link.value->GetWorkingDirectory(working, 32768), L"读取目录");
            wstring exe = executable();
            if (_wcsicmp(target, exe.c_str()) || wstring(args) != L"--proxy" ||
                _wcsicmp(icon, exe.c_str()) || index != 0 ||
                _wcsicmp(working, exe.substr(0, exe.find_last_of(L"\\/")).c_str())) result = 9;
            if (!FindResourceW(instance, MAKEINTRESOURCEW(1), RT_GROUP_ICON)) result = 10;
        } catch (...) { DeleteFileW(shortcut.c_str()); throw; }
        DeleteFileW(shortcut.c_str());
    } catch (...) { result = 8; }
    DeleteFileW(temp);
    return result;
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR command, int show) {
    instance = h;
    if (wstring(command) == L"--self-test") return selfTest();
    if (wstring(command) == L"--diagnose") {
        wstring report;
        int code = 0;
        try {
            Install app = findInstall(L"");
            report = L"CodexPath=" + app.path + L"\r\nAppUserModelID=" + app.appId + L"\r\n";
        } catch (const Failure& e) { report = e.message + L"\r\n"; code = 1; }
        int size = WideCharToMultiByte(CP_UTF8, 0, report.data(), (int)report.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, report.data(), (int)report.size(), &utf8[0], size, nullptr, nullptr);
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        return code;
    }
    SetProcessDPIAware();
    try {
        wstring exe = executable();
        directory = exe.substr(0, exe.find_last_of(L"\\/"));
        configPath = directory + L"\\CodexProxySwitcher.ini";
        ComSession com;
        Handle mutex(CreateMutexW(nullptr, FALSE, L"Local\\CodexProxySwitcherWin32"));
        check(mutex.value != nullptr, L"创建单实例锁");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxW(nullptr, L"启动器已经运行，请使用已打开的窗口。", L"Codex Proxy Switcher", MB_OK);
            return 0;
        }
        // This branch runs before window registration/creation: no launcher flash.
        if (wstring(command) == L"--proxy") {
            if (!exists(configPath))
                throw Failure{L"未找到配置文件。请先双击 EXE 保存配置，再使用代理启动快捷方式。"};
            launch(loadSettings(configPath), true);
            return 0;
        }
        WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = h;
        wc.lpszClassName = L"CodexProxySwitcher.Native";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(h, MAKEINTRESOURCEW(1));
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        check(RegisterClassW(&wc) != 0, L"注册窗口");
        RECT rect{0, 0, 580, 374};
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&rect, style, FALSE);
        int w = rect.right - rect.left, height = rect.bottom - rect.top;
        HWND hwnd = CreateWindowW(wc.lpszClassName, L"Codex Proxy Switcher · Win32", style,
            (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
            w, height, nullptr, nullptr, h, nullptr);
        check(hwnd != nullptr, L"创建窗口");
        ShowWindow(hwnd, show);
        MSG msg{};
        BOOL rc;
        while ((rc = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        return rc == -1 ? 1 : (int)msg.wParam;
    } catch (const Failure& e) { showError(e.message); return 1; }
      catch (...) { showError(L"程序发生未知错误。"); return 1; }
}
