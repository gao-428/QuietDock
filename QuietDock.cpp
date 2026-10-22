#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <commoncontrols.h>
#include <cstdint>
#include <cmath>
#include <dwmapi.h>
#include <fstream>
#include <map>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sstream>
#include <string>
#include <vector>
#include <windowsx.h>
#include <tlhelp32.h>
#include <exdisp.h>
#include <shldisp.h>

using std::wstring;
constexpr wchar_t CLASS_NAME[] = L"QuietDock.Native.Window.v1";
constexpr UINT WM_SHELLCHECK = WM_APP + 1, WM_FOREGROUND = WM_APP + 2, WM_RESTORE = WM_APP + 3,
               WM_TOGGLE = WM_APP + 4;
struct SavedWindow {
    HWND hwnd;
    DWORD pid;
    wstring cls;
};
struct Entry {
    wstring label, path;
    HICON icon = nullptr;
    bool fixed = false;
    Gdiplus::Bitmap *image = nullptr;
    bool temporary = false;
    // Auxiliary UI belongs to this window, never to every process of its host.
    HWND window = nullptr;
    DWORD windowPid = 0;
    bool closeRequested = false;
};
HWND dock = nullptr, tip = nullptr, shadow = nullptr;
bool inspectUi = false;
bool glassActive = false;
wstring backdropName = L"solid";
HINSTANCE instance;
std::vector<Entry> entries;
std::vector<SavedWindow> saved;
wstring dataDir, configPath, sessionPath, exePath;
bool clean = false, shutting = false, watchdogReady = false, canClean = false, hiddenForFullscreen = true;
HANDLE restoreEvent = nullptr;
int hover = -1, focusIndex = 0;
int shownTip = -1;
int dragIndex = -1;
bool dragging = false;
POINT dragStart{};
bool verticalDrag = false, dragReturning = false;
bool menuOpen = false;
HWND dragGhost = nullptr;
float ghostX = 0, ghostY = 0, ghostTargetX = 0, ghostTargetY = 0;
float ghostExpansion = 1.f;
POINT ghostHome{};
int ghostEntry = -1;
int upwardDistance = 0;
std::map<wstring, wstring> applicationTargets;
std::map<wstring, std::vector<DWORD>> runningProcesses;
std::map<wstring, std::vector<DWORD>> visibleApplicationProcesses;
struct TrackedProcess { DWORD pid; HANDLE handle; };
std::map<wstring, std::vector<TrackedProcess>> trackedProcesses;
void trackProcess(const wstring &key, DWORD pid) {
    auto &items = trackedProcesses[key];
    for (const auto &item : items) if (item.pid == pid && WaitForSingleObject(item.handle, 0) == WAIT_TIMEOUT) return;
    HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle) items.push_back({pid, handle});
}
bool processStillAlive(const wstring &key) {
    auto found = trackedProcesses.find(key);
    if (found == trackedProcesses.end()) return false;
    auto &items = found->second;
    items.erase(std::remove_if(items.begin(), items.end(), [](const TrackedProcess &item) {
        if (WaitForSingleObject(item.handle, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(item.handle); return true;
    }), items.end());
    return !items.empty();
}
std::map<wstring, std::vector<HWND>> systemWindows;
std::vector<Entry> auxiliaryWindows;
struct AuxiliaryCloseRequest {
    Entry entry;
    int attempts = 0;
    ULONGLONG settleUntil = 0;
    bool ownerWasMinimized = false;
    wstring windowClass;
};
std::vector<AuxiliaryCloseRequest> auxiliaryCloseRequests;
void refreshRunning();
void cancelWindowActivation();
bool windowActivationPending(HWND window);
bool isRunning(int index);
void updateGhost(float dt);
void finishVerticalDrag(bool commit);
void hideName();
void showName();
float pointerX = -10000.f;
std::vector<float> magnification;
std::vector<float> iconVelocity;
bool animationActive = false;
ULONGLONG animationTick = 0;
UINT dpi = 96, taskbarMessage = 0;
HFONT font = nullptr;
HANDLE singleton = nullptr;
HWINEVENTHOOK foregroundHook = nullptr, objectHook = nullptr;
ULONG_PTR gdiplusToken = 0;
int visibleEntryLimit = 100;
int dockZoom = 125;
int wheelRemainder = 0;
int scale(int n) { return MulDiv(n, dpi * dockZoom, 9600); }
int visibleCount() { return std::min((int)entries.size(), visibleEntryLimit); }
bool hasOverflow() { return visibleCount() < (int)entries.size(); }
bool showRecent = true;
std::vector<wstring> recentPaths;
std::map<wstring, wstring> previousApplications;
bool recentScanStarted = false;
void syncRecentEntries();
void startupLog(const wstring &message);
void loadRecent() {
    wstring file = dataDir + L"\\recent.ini";
    showRecent = GetPrivateProfileIntW(L"Recent", L"Enabled", 1, file.c_str()) != 0;
    recentPaths.clear();
    for (int i = 0; i < 4096; ++i) {
        wchar_t path[32768]{};
        GetPrivateProfileStringW(L"Recent", std::to_wstring(i).c_str(), L"", path, 32768, file.c_str());
        if (!*path) break;
        recentPaths.push_back(path);
    }
}
void saveRecent() {
    wstring file = dataDir + L"\\recent.ini";
    HANDLE created = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr);
    if (created != INVALID_HANDLE_VALUE) {
        WORD bom = 0xfeff; DWORD written = 0;
        WriteFile(created, &bom, sizeof(bom), &written, nullptr);
        CloseHandle(created);
    }
    wstring section = L"Enabled=" + std::to_wstring(showRecent);
    section.push_back(0);
    for (size_t i = 0; i < recentPaths.size(); ++i) {
        section += std::to_wstring(i) + L"=" + recentPaths[i];
        section.push_back(0);
    }
    section.push_back(0);
    if (!WritePrivateProfileSectionW(L"Recent", section.c_str(), file.c_str()))
        startupLog(L"recent save failed error=" + std::to_wstring(GetLastError()));
}
void rememberRecent(const wstring &path) {
    auto it = std::find(recentPaths.begin(), recentPaths.end(), path);
    if (it == recentPaths.begin() && it != recentPaths.end()) return;
    if (it != recentPaths.end()) recentPaths.erase(it);
    recentPaths.insert(recentPaths.begin(), path);
    saveRecent();
}
int recentStart() {
    for (int i = 0; i < visibleCount(); ++i) if (entries[i].temporary) return i;
    return -1;
}
int recentGap() { return recentStart() > 0 ? scale(12) : 0; }
int itemCount() { return visibleCount() + (hasOverflow() ? 1 : 0); }
int slotWidth() { return scale(48); }
int dockHeight() { return scale(56); }
void positionDock();
RECT itemRect(int i) {
    int gap = recentStart() >= 0 && i >= recentStart() ? recentGap() : 0;
    return {scale(10) + i * slotWidth() + gap, scale(3), scale(10) + (i + 1) * slotWidth() + gap, scale(53)};
}
void startIconAnimation() {
    // Resetting a Win32 timer on every mouse event postpones frames during fast movement.
    if (!animationActive) {
        animationTick = GetTickCount64();
        animationActive = SetTimer(dock, 2, 16, nullptr) != 0;
    }
}
// Monotonic easing to a fixed hovered-item target. No pointer-distance wobble or overshoot.
void animateIcons() {
    magnification.resize(itemCount(), 0.f);
    iconVelocity.resize(itemCount(), 0.f);
    ULONGLONG now = GetTickCount64();
    float dt = std::clamp((now - animationTick) / 1000.f, .001f, .05f);
    animationTick = now;
    constexpr float speed = 22.f;
    float decay = std::exp(-speed * dt);
    bool moving = false;
    for (int i = 0; i < itemCount(); ++i) {
        float goal = i == hover && i < visibleCount() ? 1.f : 0.f;
        magnification[i] = goal + (magnification[i] - goal) * decay;
        if (std::abs(magnification[i] - goal) < .005f) {
            magnification[i] = goal;
            iconVelocity[i] = 0.f;
        } else
            moving = true;
    }
    if (verticalDrag || dragReturning) {
        updateGhost(dt);
        moving = true;
    }
    InvalidateRect(dock, nullptr, FALSE);
    if (!moving) {
        KillTimer(dock, 2);
        animationActive = false;
        showName();
    }
}
wstring classOf(HWND w) {
    wchar_t b[256]{};
    GetClassNameW(w, b, 256);
    return b;
}
void startupLog(const wstring &message) {
    HANDLE f = CreateFileW((dataDir + L"\\startup.log").c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t prefix[100];
    swprintf(prefix, 100, L"[%04u-%02u-%02u %02u:%02u:%02u pid=%lu] ", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    wstring line = prefix + message + L"\r\n";
    DWORD n = 0;
    // UTF-8 keeps the diagnostic log readable from common command-line tools.
    int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string bytes(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, bytes.data(), size, nullptr, nullptr);
    WriteFile(f, bytes.data(), size - 1, &n, nullptr);
    CloseHandle(f);
}
void initPaths(const wstring &overrideDir = L" ") {
    PWSTR p = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p);
    dataDir = p ? p : L".";
    CoTaskMemFree(p);
    dataDir += L"\\QuietDock";
    if (overrideDir != L" ")
        dataDir = overrideDir;
    wchar_t absolute[32768]{};
    DWORD length = GetFullPathNameW(dataDir.c_str(), 32768, absolute, nullptr);
    if (length && length < 32768)
        dataDir = absolute;
    while (dataDir.size() > 3 && (dataDir.back() == L'\\' || dataDir.back() == L'/'))
        dataDir.pop_back();
    CreateDirectoryW(dataDir.c_str(), nullptr);
    configPath = dataDir + L"\\config.ini";
    wchar_t b[32768];
    GetModuleFileNameW(nullptr, b, 32768);
    exePath = b;
    sessionPath = dataDir + L"\\session-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
}
bool explorerShellTarget(HWND w) {
    wstring cls = classOf(w);
    if (cls != L"Shell_TrayWnd" && cls != L"Shell_SecondaryTrayWnd" && cls != L"SysListView32")
        return false;
    if (cls == L"SysListView32" && classOf(GetParent(w)) != L"SHELLDLL_DefView")
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[32768]{};
    DWORD n = 32768;
    bool ok = QueryFullProcessImageNameW(process, 0, path, &n) &&
              _wcsicmp(PathFindFileNameW(path), L"explorer.exe") == 0;
    CloseHandle(process);
    return ok;
}
bool matches(const SavedWindow &s) {
    DWORD p = 0;
    GetWindowThreadProcessId(s.hwnd, &p);
    return IsWindow(s.hwnd) && p == s.pid && classOf(s.hwnd) == s.cls &&
           explorerShellTarget(s.hwnd);
}
bool writeSession() {
    wstring tmp = sessionPath + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    wstring text;
    for (auto &s : saved)
        text += std::to_wstring((uintptr_t)s.hwnd) + L"\t" + std::to_wstring(s.pid) + L"\t" +
                s.cls + L"\r\n";
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), (DWORD)(text.size() * 2), &written, nullptr) &&
              written == text.size() * 2;
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    return ok && MoveFileExW(tmp.c_str(), sessionPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
std::vector<SavedWindow> readSession(const wstring &path) {
    std::vector<SavedWindow> out;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return out;
    DWORD size = GetFileSize(f, nullptr), read = 0;
    if (size > 1024 * 1024 || (size % 2) != 0) {
        CloseHandle(f);
        return out;
    }
    wstring text(size / 2, L'\0');
    bool ok = ReadFile(f, text.data(), size, &read, nullptr) && read == size;
    CloseHandle(f);
    if (!ok)
        return out;
    std::wistringstream input(text);
    wstring line;
    while (std::getline(input, line)) {
        std::wistringstream row(line);
        uintptr_t h;
        DWORD pid;
        wstring cls;
        if (row >> h >> pid >> cls)
            out.push_back({(HWND)h, pid, cls});
    }
    return out;
}
void restoreFile(const wstring &path) {
    for (auto &s : readSession(path))
        if (matches(s))
            ShowWindow(s.hwnd, SW_SHOWNOACTIVATE);
    DeleteFileW(path.c_str());
}
void restoreAllFiles() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dataDir + L"\\session-*.ini").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            restoreFile(dataDir + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
void restoreShell() {
    clean = false;
    if (dock)
        SetPropW(dock, L"QuietDock.Clean", nullptr);
    for (auto &s : saved)
        if (matches(s))
            ShowWindow(s.hwnd, SW_SHOWNOACTIVATE);
    saved.clear();
    DeleteFileW(sessionPath.c_str());
    if (dock && !shutting) {
        positionDock();
        InvalidateRect(dock, nullptr, FALSE);
    }
}
void hideOne(HWND w) {
    if (!w || !IsWindowVisible(w) || !explorerShellTarget(w))
        return;
    if (restoreEvent && WaitForSingleObject(restoreEvent, 0) == WAIT_OBJECT_0) {
        restoreShell();
        return;
    }
    auto it = std::find_if(saved.begin(), saved.end(),
                           [&](const SavedWindow &s) { return s.hwnd == w && matches(s); });
    if (it == saved.end()) {
        DWORD pid;
        GetWindowThreadProcessId(w, &pid);
        saved.push_back({w, pid, classOf(w)});
        if (!writeSession()) {
            saved.pop_back();
            restoreShell();
            MessageBoxW(dock, L"无法写入恢复记录，已恢复桌面。请检查本地用户目录是否可写。",
                        L"QuietDock", MB_ICONWARNING);
            return;
        }
    }
    ShowWindow(w, SW_HIDE);
    if (restoreEvent && WaitForSingleObject(restoreEvent, 0) == WAIT_OBJECT_0)
        restoreShell();
}
BOOL CALLBACK findShell(HWND w, LPARAM) {
    if (!clean)
        return FALSE;
    wstring cls = classOf(w);
    if (cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd")
        hideOne(w);
    if (cls == L"Progman" || cls == L"WorkerW") {
        HWND def = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr);
        if (def)
            hideOne(FindWindowExW(def, nullptr, L"SysListView32", nullptr));
    }
    return TRUE;
}
void refreshShell() {
    if (restoreEvent && WaitForSingleObject(restoreEvent, 0) == WAIT_OBJECT_0) {
        restoreShell();
        return;
    }
    if (clean)
        EnumWindows(findShell, 0);
}
void toggleClean() {
    if (clean)
        restoreShell();
    else if (canClean) {
        ResetEvent(restoreEvent);
        clean = true;
        SetPropW(dock, L"QuietDock.Clean", (HANDLE)1);
        refreshShell();
        positionDock();
        InvalidateRect(dock, nullptr, FALSE);
    }
}
bool startWatchdog() {
    HANDLE ready = CreateEventW(
        nullptr, TRUE, FALSE,
        (L"Local\\QuietDock.WatchdogReady." + std::to_wstring(GetCurrentProcessId())).c_str());
    if (!ready)
        return false;
    wstring command = L"\"" + exePath + L"\" --watchdog " + std::to_wstring(GetCurrentProcessId()) +
                      L" --data-dir \"" + dataDir + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exePath.c_str(), command.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        HANDLE waits[] = {ready, pi.hProcess};
        ok = WaitForMultipleObjects(2, waits, FALSE, 5000) == WAIT_OBJECT_0;
        CloseHandle(pi.hProcess);
    }
    CloseHandle(ready);
    return ok;
}
int watchdog(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process)
        return 2;
    sessionPath = dataDir + L"\\session-" + std::to_wstring(pid) + L".ini";
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE,
                              (L"Local\\QuietDock.WatchdogReady." + std::to_wstring(pid)).c_str());
    if (!ready) {
        CloseHandle(process);
        return 3;
    }
    SetEvent(ready);
    CloseHandle(ready);
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    restoreFile(sessionPath);
    return 0;
}
Gdiplus::Bitmap *iconImage(HICON icon) {
    // GDI+ FromHICON can flatten Shell icons onto black. Recover premultiplied alpha
    // from native DrawIconEx on black and white once, then cache the resulting image.
    ICONINFO ii{};
    int w = 48, h = 48;
    if (GetIconInfo(icon, &ii)) {
        BITMAP bm{};
        if (GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm)) {
            w = bm.bmWidth;
            h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
        }
        if (ii.hbmColor)
            DeleteObject(ii.hbmColor);
        if (ii.hbmMask)
            DeleteObject(ii.hbmMask);
    }
    if (w <= 0 || h <= 0 || w > 512 || h > 512)
        return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void *dark = nullptr;
    void *light = nullptr;
    HBITMAP black = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &dark, nullptr, 0);
    HBITMAP white = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &light, nullptr, 0);
    if (!black || !white) {
        if (black)
            DeleteObject(black);
        if (white)
            DeleteObject(white);
        DeleteDC(dc);
        return nullptr;
    }
    std::fill_n((DWORD *)dark, w * h, 0xff000000u);
    std::fill_n((DWORD *)light, w * h, 0xffffffffu);
    auto old = SelectObject(dc, black);
    DrawIconEx(dc, 0, 0, icon, w, h, 0, nullptr, DI_NORMAL);
    SelectObject(dc, white);
    DrawIconEx(dc, 0, 0, icon, w, h, 0, nullptr, DI_NORMAL);
    GdiFlush();
    auto image = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
    Gdiplus::BitmapData data{};
    Gdiplus::Rect area(0, 0, w, h);
    if (image->LockBits(&area, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) ==
        Gdiplus::Ok) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                DWORD b = ((DWORD *)dark)[y * w + x], a = ((DWORD *)light)[y * w + x];
                int delta = 0;
                for (int shift = 0; shift < 24; shift += 8)
                    delta = std::max(delta, (int)((a >> shift) & 255) - (int)((b >> shift) & 255));
                DWORD alpha = 255 - std::clamp(delta, 0, 255), pixel = alpha << 24;
                for (int shift = 0; shift < 24; shift += 8)
                    pixel |= std::min(alpha, (b >> shift) & 255) << shift;
                ((DWORD *)((BYTE *)data.Scan0 + y * data.Stride))[x] = pixel;
            }
        image->UnlockBits(&data);
    } else {
        delete image;
        image = nullptr;
    }
    SelectObject(dc, old);
    DeleteObject(black);
    DeleteObject(white);
    DeleteDC(dc);
    return image;
}
void loadIcon(Entry &e) {
    delete e.image;
    e.image = nullptr;
    if (e.icon) {
        DestroyIcon(e.icon);
        e.icon = nullptr;
    }
    wstring target = e.path;
    if (target == L"https://www.bing.com") {
        wchar_t browser[32768]{};
        DWORD n = 32768;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"https", L"open",
                                        browser, &n)))
            target = browser;
    } else if (target == L"ms-settings:") {
        wchar_t windows[32768]{};
        GetWindowsDirectoryW(windows, 32768);
        target = wstring(windows) + L"\\ImmersiveControlPanel\\SystemSettings.exe";
    }
    SHFILEINFOW info{};
    DWORD flags = SHGFI_SYSICONINDEX | SHGFI_LARGEICON;
    PIDLIST_ABSOLUTE pidl = nullptr;
    bool got = false;
    if (target == L"shell:RecycleBinFolder" &&
        SUCCEEDED(SHParseDisplayName(target.c_str(), nullptr, &pidl, 0, nullptr)))
        got = SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), flags | SHGFI_PIDL) != 0;
    else
        got = SHGetFileInfoW(target.c_str(), 0, &info, sizeof(info), flags) != 0;
    if (pidl)
        CoTaskMemFree(pidl);
    if (got) {
        IImageList *list = nullptr;
        if (SUCCEEDED(SHGetImageList(SHIL_EXTRALARGE, __uuidof(IImageList), (void **)&list))) {
            list->GetIcon(info.iIcon, ILD_TRANSPARENT, &e.icon);
            list->Release();
        }
    }
    if (!e.icon) {
        SHSTOCKICONINFO stock{sizeof(stock)};
        SHSTOCKICONID id = target == L"shell:RecycleBinFolder"                   ? SIID_RECYCLER
                           : target.find(L"SystemSettings.exe") != wstring::npos ? SIID_SETTINGS
                           : e.fixed                                             ? SIID_FOLDER
                                                                                 : SIID_APPLICATION;
        if (SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_LARGEICON, &stock)))
            e.icon = stock.hIcon;
    }
    if (!e.icon)
        e.icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    e.image = iconImage(e.icon);
}
wstring settingsKey() {
    unsigned long long hash = 1469598103934665603ULL;
    for (wchar_t c : dataDir) { hash ^= towlower(c); hash *= 1099511628211ULL; }
    return L"Software\\QuietDock\\Profiles\\" + std::to_wstring(hash);
}
bool readSettings(wstring &text) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKey().c_str(), L"PinnedConfig", RRF_RT_REG_SZ,
                     nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes > 4 * 1024 * 1024) return false;
    text.resize(bytes / sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKey().c_str(), L"PinnedConfig", RRF_RT_REG_SZ,
                     nullptr, text.data(), &bytes) != ERROR_SUCCESS) return false;
    while (!text.empty() && text.back() == 0) text.pop_back();
    return true;
}
bool writeSettings(const wstring &text) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settingsKey().c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool ok = RegSetValueExW(key, L"PinnedConfig", 0, REG_SZ, (const BYTE *)text.c_str(),
                            (DWORD)(text.size() + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
    if (ok) ok = RegFlushKey(key) == ERROR_SUCCESS;
    RegCloseKey(key);
    wstring verify;
    return ok && readSettings(verify) && verify == text;
}
bool parseDockConfig(const wstring &content, std::vector<wstring> &paths, int &zoom,
                     std::vector<wstring> *order = nullptr) {
    std::wistringstream input(content);
    wstring line, section;
    std::map<int, wstring> items;
    std::map<int, wstring> ordered;
    bool hasItems = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = line;
            hasItems |= section == L"[Items]";
            continue;
        }
        size_t equal = line.find(L'=');
        if (equal == wstring::npos)
            continue;
        wstring key = line.substr(0, equal), value = line.substr(equal + 1);
        if (section == L"[Items]" && !key.empty() && key.size() <= 3 &&
            key.find_first_not_of(L"0123456789") == wstring::npos && !value.empty()) {
            int index = _wtoi(key.c_str());
            if (index < 100)
                items[index] = value;
        }
        if (section == L"[Appearance]" && key == L"Scale")
            zoom = std::clamp(_wtoi(value.c_str()), 75, 200);
        if (section == L"[Order]" && !key.empty() && key.size() <= 3 &&
            key.find_first_not_of(L"0123456789") == wstring::npos && !value.empty())
            ordered[_wtoi(key.c_str())] = value;
    }
    if (!hasItems)
        return false;
    paths.clear();
    for (const auto &item : items)
        paths.push_back(item.second);
    if (order) {
        order->clear();
        for (const auto &item : ordered)
            order->push_back(item.second);
    }
    return true;
}
// Read the file itself, not the Win32 INI cache: saves replace the file atomically.
bool readDockConfig(std::vector<wstring> &paths, int &zoom, std::vector<wstring> *order = nullptr) {
    HANDLE f = CreateFileW(configPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(f, nullptr), read = 0;
    if (size == INVALID_FILE_SIZE || size > 4 * 1024 * 1024) {
        CloseHandle(f);
        return false;
    }
    std::vector<BYTE> bytes(size);
    bool ok = ReadFile(f, bytes.data(), size, &read, nullptr) && read == size;
    CloseHandle(f);
    if (!ok || size < 2)
        return false;
    wstring content;
    if (bytes[0] == 0xff && bytes[1] == 0xfe && size % 2 == 0) {
        for (DWORD i = 2; i < size; i += 2)
            content.push_back((wchar_t)(bytes[i] | (bytes[i + 1] << 8)));
    } else {
        int offset = size >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        const char *data = (const char *)bytes.data() + offset;
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, size - offset, nullptr, 0);
        if (!count)
            return false;
        content.resize(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, size - offset, content.data(), count);
    }
    return parseDockConfig(content, paths, zoom, order);
}
bool reorderEntry(int from, int to) {
    if (from < 0 || to < 0 || from >= (int)entries.size() || to >= (int)entries.size() ||
        from == to || entries[from].temporary != entries[to].temporary) return false;
    if (to > from)
        std::rotate(entries.begin() + from, entries.begin() + from + 1, entries.begin() + to + 1);
    else
        std::rotate(entries.begin() + to, entries.begin() + from, entries.begin() + from + 1);
    return true;
}
void saveRecentOrder() {
    if (!showRecent) return;
    std::vector<wstring> order;
    for (const auto &entry : entries) if (entry.temporary && !entry.window) order.push_back(entry.path);
    if (!order.empty() && order != recentPaths) {
        recentPaths = std::move(order);
        saveRecent();
    }
}
void saveConfig() {
    saveRecentOrder();
    wstring text = L"[Items]\r\n";
    int count = 0;
    for (auto &e : entries)
        if (!e.fixed && !e.temporary)
            text += std::to_wstring(count++) + L"=" + e.path + L"\r\n";
    text += L"Count=" + std::to_wstring(count) + L"\r\n";
    text += L"\r\n[Appearance]\r\nScale=" + std::to_wstring(dockZoom) + L"\r\n";
    text += L"\r\n[Order]\r\n";
    for (size_t i = 0; i < entries.size(); ++i)
        if (!entries[i].temporary)
            text += std::to_wstring(i) + L"=" + entries[i].path + L"\r\n";
    bool settingsSaved = writeSettings(text);
    wstring tmp = configPath + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = f != INVALID_HANDLE_VALUE;
    if (ok) {
        WORD bom = 0xfeff;
        DWORD n = 0;
        ok = WriteFile(f, &bom, 2, &n, nullptr) && n == 2;
        if (ok)
            ok = WriteFile(f, text.data(), (DWORD)text.size() * 2, &n, nullptr) &&
                 n == text.size() * 2;
        ok = FlushFileBuffers(f) && ok;
        CloseHandle(f);
        if (ok) {
            CopyFileW(configPath.c_str(), (configPath + L".bak").c_str(), FALSE);
            ok = MoveFileExW(tmp.c_str(), configPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
    if (ok) {
        std::vector<wstring> persisted;
        int zoom = 125;
        ok = readDockConfig(persisted, zoom) && persisted.size() == (size_t)count && zoom == dockZoom;
        size_t index = 0;
        for (const auto &entry : entries)
            if (!entry.fixed && !entry.temporary) {
                ok = ok && index < persisted.size() && persisted[index] == entry.path;
                ++index;
            }
        startupLog(L"config saved verified=" + std::to_wstring(ok) + L" items=" + std::to_wstring(count));
    }
    startupLog(L"settings verified=" + std::to_wstring(settingsSaved) + L" items=" + std::to_wstring(count));
    if (!ok && !settingsSaved) {
        DeleteFileW(tmp.c_str());
        MessageBoxW(dock, L"入口已在本次运行中更新，但配置保存失败。请检查数据目录权限或磁盘空间。",
                    L"QuietDock", MB_ICONWARNING);
    }
}
// Independent append-only pin history survives stale snapshot replacement.
bool recordPin(const wstring &path, bool pin) {
    HANDLE file = CreateFileW((dataDir + L"\\pins.log").c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) SetFilePointer(file, 0, nullptr, FILE_END);
    wstring record = (pin ? L"+" : L"-") + path + L"\n";
    DWORD written = 0;
    bool ok = file != INVALID_HANDLE_VALUE &&
        WriteFile(file, record.data(), (DWORD)record.size() * 2, &written, nullptr) &&
        written == record.size() * 2 && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ok) MessageBoxW(dock, L"未能保存固定入口，本次更改未生效。", L"QuietDock", MB_ICONERROR);
    return ok;
}
void replayPins(std::vector<wstring> &paths) {
    HANDLE file = CreateFileW((dataDir + L"\\pins.log").c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    if (size > 16 * 1024 * 1024 || size == INVALID_FILE_SIZE) { CloseHandle(file); return; }
    wstring text(size / 2, L'\0');
    bool ok = ReadFile(file, text.data(), (DWORD)text.size() * 2, &read, nullptr);
    CloseHandle(file);
    if (!ok) return;
    text.resize(read / 2);
    size_t start = 0, end;
    while ((end = text.find(L'\n', start)) != wstring::npos) {
        wstring line = text.substr(start, end - start);
        start = end + 1;
        if (line.size() < 2 || (line[0] != L'+' && line[0] != L'-')) continue;
        wstring path = line.substr(1);
        auto found = std::find_if(paths.begin(), paths.end(),
            [&](const wstring &p) { return _wcsicmp(p.c_str(), path.c_str()) == 0; });
        if (line[0] == L'+' && found == paths.end()) paths.push_back(path);
        if (line[0] == L'-' && found != paths.end()) paths.erase(found);
    }
}
wstring labelFor(const wstring &p) {
    const wchar_t *filename = PathFindFileNameW(p.c_str());
    wstring text = filename && *filename ? filename : p;
    size_t dot = text.find_last_of(L'.');
    if (dot != wstring::npos && dot > 0)
        text.resize(dot);
    return text;
}
void loadEntries() {
    loadRecent();
    std::vector<wstring> stored, order;
    bool loaded = readDockConfig(stored, dockZoom, &order);
    if (!loaded) {
        wstring primary = configPath;
        configPath += L".bak";
        loaded = readDockConfig(stored, dockZoom, &order);
        configPath = primary;
    }
    if (GetFileAttributesW((dataDir + L"\\pins.log").c_str()) == INVALID_FILE_ATTRIBUTES)
        for (const auto &path : stored) recordPin(path, true);
    wstring persistedSettings;
    if (readSettings(persistedSettings))
        parseDockConfig(persistedSettings, stored, dockZoom, &order);
    replayPins(stored);
    startupLog(L"config loaded=" + std::to_wstring(loaded) + L" items=" +
               std::to_wstring(stored.size()) + L" path=" + configPath);
    wchar_t personal[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, personal);
    entries = {{L"文件", L"explorer.exe", nullptr, true},
               {L"文档", personal, nullptr, true},
               {L"浏览器", L"https://www.bing.com", nullptr, true},
               {L"设置", L"ms-settings:", nullptr, true},
               {L"回收站", L"shell:RecycleBinFolder", nullptr, true}};
    for (const auto &p : stored)
        entries.push_back({labelFor(p), p});
    size_t destination = 0;
    for (const auto &path : order) {
        auto found = std::find_if(entries.begin() + destination, entries.end(),
                                 [&](const Entry &entry) { return entry.path == path; });
        if (found != entries.end()) {
            std::rotate(entries.begin() + destination, found, found + 1);
            ++destination;
        }
    }
    for (auto &e : entries)
        loadIcon(e);
}
void hideName() {
    if (tip)
        ShowWindow(tip, SW_HIDE);
    shownTip = -1;
}
void showName() {
    if (!tip || hover < 0 || hover >= itemCount() || hiddenForFullscreen || dragging)
        return;
    hideName();
    wstring label = hover < visibleCount() ? entries[hover].label :
                    L"更多入口";
    if (hover < visibleCount() && entries[hover].temporary)
        label += entries[hover].window ? L"（附属窗口）" : L"（最近打开）";
    HDC dc = GetDC(tip);
    auto old = SelectObject(dc, font);
    SIZE text{};
    GetTextExtentPoint32W(dc, label.c_str(), (int)label.size(), &text);
    SelectObject(dc, old);
    ReleaseDC(tip, dc);
    int width = text.cx + scale(20), height = text.cy + scale(12);
    RECT r = itemRect(hover);
    POINT position{(r.left + r.right) / 2 - width / 2, -height - scale(8)};
    ClientToScreen(dock, &position);
    SetWindowTextW(tip, label.c_str());
    SetWindowRgn(tip, CreateRoundRectRgn(0, 0, width + 1, height + 1, scale(10), scale(10)), FALSE);
    SetWindowPos(tip, HWND_TOPMOST, position.x, position.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(tip, nullptr, FALSE);
    shownTip = hover;
}
void updateTooltips() { hideName(); }
void roundedPath(Gdiplus::GraphicsPath &path, float x, float y, float w, float h, float radius) {
    float d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}
void applyAppearance() {
    BOOL composition = FALSE;
    DwmIsCompositionEnabled(&composition);
    HIGHCONTRASTW hc{sizeof(hc)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    glassActive = false;
    backdropName = L"solid";
    BOOL dark = FALSE;
    DwmSetWindowAttribute(dock, 20, &dark, sizeof(dark));
    DWORD noBorder = 0xfffffffe;
    DwmSetWindowAttribute(dock, 34, &noBorder, sizeof(noBorder));
    DWORD cornerPreference = 2; // DWMWCP_ROUND: clip the compositor backdrop as well as paint.
    DwmSetWindowAttribute(dock, 33, &cornerPreference, sizeof(cornerPreference));
    if (composition && !(hc.dwFlags & HCF_HIGHCONTRASTON)) {
        // Low-tint acrylic lets the actual desktop color show through. The standard
        // DWM transient material has a deliberately opaque tint that cannot be tuned.
        int backdrop = 1; // Clear any previous system backdrop before applying acrylic.
        DwmSetWindowAttribute(dock, 38, &backdrop, sizeof(backdrop));
        struct Accent { int state, flags; DWORD color; int animation; };
        struct CompositionData { int attribute; void *data; SIZE_T size; };
        using SetComposition = BOOL(WINAPI *)(HWND, CompositionData *);
        auto set = (SetComposition)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                  "SetWindowCompositionAttribute");
        Accent accent{4, 0, 0x01FFFFFF, 0};
        CompositionData data{19, &accent, sizeof(accent)};
        if (set && set(dock, &data)) {
            glassActive = true;
            backdropName = L"transparent acrylic";
        } else {
            backdrop = 3;
            if (SUCCEEDED(DwmSetWindowAttribute(dock, 38, &backdrop, sizeof(backdrop)))) {
                glassActive = true;
                backdropName = L"DWM Desktop Acrylic fallback";
            }
        }
    }
    MARGINS margins = glassActive ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(dock, &margins);
    SetPropW(dock, L"QuietDock.Glass", (HANDLE)(uintptr_t)glassActive);
    InvalidateRect(dock, nullptr, FALSE);
}
LRESULT CALLBACK shadowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}
void updateShadow(int x, int y, int width, int height) {
    if (!shadow)
        return;
    int margin = scale(12), sw = width + 2 * margin, sh = height + 2 * margin;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sw;
    bi.bmiHeader.biHeight = -sh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return;
    }
    auto old = SelectObject(dc, bitmap);
    ZeroMemory(pixels, sw * sh * 4);
    {
        using namespace Gdiplus;
        Bitmap surface(sw, sh, sw * 4, PixelFormat32bppPARGB, (BYTE *)pixels);
        Graphics g(&surface);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        for (int spread = margin; spread >= scale(2); spread -= std::max(1, scale(1))) {
            GraphicsPath path;
            roundedPath(path, (float)(margin - spread), (float)(margin - spread + scale(3)),
                        (float)(width + spread * 2), (float)(height + spread * 2),
                        float(MulDiv(8, dpi, 96) + spread));
            SolidBrush brush(Color(2, 0, 0, 0));
            g.FillPath(&brush, &path);
        }
        g.SetCompositingMode(CompositingModeSourceCopy);
        GraphicsPath cut;
        roundedPath(cut, (float)margin, (float)margin, (float)width, (float)height,
                    (float)MulDiv(8, dpi, 96));
        SolidBrush clear(Color(0, 0, 0, 0));
        g.FillPath(&clear, &cut);
    }
    POINT dest{x - margin, y - margin}, source{};
    SIZE size{sw, sh};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(shadow, screen, &dest, &size, dc, &source, 0, &blend, ULW_ALPHA);
    SetWindowPos(shadow, HWND_TOPMOST, x - margin, y - margin, sw, sh, SWP_NOACTIVATE);
    ShowWindow(shadow, hiddenForFullscreen ? SW_HIDE : SW_SHOWNOACTIVATE);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
void positionDock() {
    SetPropW(dock, L"QuietDock.Items", (HANDLE)(uintptr_t)entries.size());
    SetPropW(dock, L"QuietDock.Pinned", (HANDLE)(uintptr_t)std::count_if(entries.begin(), entries.end(),
        [](const Entry &e) { return !e.temporary; }));
    SetPropW(dock, L"QuietDock.RecentEnabled", (HANDLE)(uintptr_t)showRecent);
    SetPropW(dock, L"QuietDock.RecentCount", (HANDLE)(uintptr_t)std::count_if(entries.begin(), entries.end(), [](const Entry &e) { return e.temporary; }));
    SetPropW(dock, L"QuietDock.AuxiliaryCount", (HANDLE)(uintptr_t)std::count_if(entries.begin(), entries.end(), [](const Entry &e) { return e.window != nullptr; }));
    SetPropW(dock, L"QuietDock.Scale", (HANDLE)(uintptr_t)dockZoom);
    HMONITOR monitor = MonitorFromWindow(dock, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    int maxWidth = (mi.rcMonitor.right - mi.rcMonitor.left) - scale(32);
    int capacity = std::max(3, (maxWidth - scale(20) - scale(12)) / slotWidth());
    visibleEntryLimit = (int)entries.size() <= capacity ? capacity : capacity - 1;
    focusIndex = std::min(focusIndex, itemCount() - 1);
    int width = std::min(scale(20) + itemCount() * slotWidth() + recentGap(), maxWidth);
    RECT frame{0, 0, width, dockHeight()};
    if (inspectUi)
        AdjustWindowRectExForDpi(&frame, (DWORD)GetWindowLongPtrW(dock, GWL_STYLE), FALSE,
                                 (DWORD)GetWindowLongPtrW(dock, GWL_EXSTYLE), dpi);
    int outerWidth = frame.right - frame.left, outerHeight = frame.bottom - frame.top;
    int dockX = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - outerWidth) / 2;
    int dockY = (clean ? mi.rcMonitor.bottom : mi.rcWork.bottom) - outerHeight - scale(10);
    updateShadow(dockX, dockY, outerWidth, outerHeight);
    SetWindowPos(dock, HWND_TOPMOST, dockX, dockY, outerWidth, outerHeight, SWP_NOACTIVATE);
    HRGN region = inspectUi
                      ? nullptr
                      : CreateRoundRectRgn(0, 0, width + 1, dockHeight() + 1, MulDiv(16, dpi, 96), MulDiv(16, dpi, 96));
    SetWindowRgn(dock, region, TRUE);
    updateTooltips();
}
void addPath(const wstring &input) {
    wstring p = input;
    if (p.empty())
        return;
    startupLog(L"add requested path=" + p);
    for (auto &e : entries)
        if (_wcsicmp(e.path.c_str(), p.c_str()) == 0) {
            if (e.temporary && recordPin(e.path, true)) {
                e.temporary = false;
                syncRecentEntries();
                saveConfig();
                positionDock();
                InvalidateRect(dock, nullptr, FALSE);
            }
            else if (!e.temporary) saveConfig();
            return;
        }
    if (std::count_if(entries.begin(), entries.end(), [](const Entry &e) { return !e.temporary; }) >= 100) {
        MessageBoxW(dock, L"最多可保存 100 个入口。", L"QuietDock", MB_OK);
        return;
    }
    if (!recordPin(p, true)) return;
    entries.push_back({labelFor(p), p});
    loadIcon(entries.back());
    syncRecentEntries();
    saveConfig();
    positionDock();
    InvalidateRect(dock, nullptr, FALSE);
}
void chooseFile() {
    wchar_t path[32768]{};
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = dock;
    of.lpstrFilter = L"应用、快捷方式和文件\0*.exe;*.lnk;*.url;*.*\0所有文件\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = 32768;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT;
    of.lpstrTitle = L"添加到 QuietDock";
    if (GetOpenFileNameW(&of))
        addPath(path);
}
void openEntry(int i);
void showOverflow() {
    HMENU menu = CreatePopupMenu(), remove = CreatePopupMenu();
    for (int i = visibleCount(); i < (int)entries.size(); i++) {
        AppendMenuW(menu, MF_STRING, 10000 + i, entries[i].label.c_str());
        if (!entries[i].fixed && !entries[i].temporary)
            AppendMenuW(remove, MF_STRING, 20000 + i, entries[i].label.c_str());
    }
    if (GetMenuItemCount(remove))
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)remove, L"移除入口");
    else
        DestroyMenu(remove);
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(dock);
    menuOpen = true;
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, dock, nullptr);
    menuOpen = false;
    DestroyMenu(menu);
    if (cmd >= 20000 && cmd < 20000 + (int)entries.size()) {
        int i = cmd - 20000;
        if (i < (int)entries.size() && !entries[i].fixed) {
            if (!entries[i].temporary && !recordPin(entries[i].path, false)) return;
            DestroyIcon(entries[i].icon);
            delete entries[i].image;
            entries.erase(entries.begin() + i);
            saveConfig();
            positionDock();
            InvalidateRect(dock, nullptr, FALSE);
        }
    } else if (cmd >= 10000 && cmd < 10000 + (int)entries.size()) {
        int i = cmd - 10000;
        if (i < (int)entries.size())
            openEntry(i);
    }
}
wstring applicationHost(wstring target) {
    wstring lower = target;
    if (!lower.empty()) CharLowerBuffW(lower.data(), (DWORD)lower.size());
    // HoYo's stable launcher hands off to HYP.exe inside a version directory.
    // Resolve by installation identity, never by a shared process name alone.
    wstring name = PathFindFileNameW(lower.c_str());
    if (name == L"hyp.exe" || name == L"launcher.exe") {
        size_t file = target.find_last_of(L"\\/");
        if (file != wstring::npos) {
            wstring version = target.substr(0, file);
            size_t parent = version.find_last_of(L"\\/");
            if (parent != wstring::npos) {
                wstring root = version.substr(0, parent);
                wchar_t game[128]{};
                GetPrivateProfileStringW(L"hyp", L"primary_game", L"", game, 128,
                                          (root + L"\\config.ini").c_str());
                if (wcsncmp(game, L"hyp_", 4) == 0 &&
                    GetFileAttributesW((version + L"\\HYP.exe").c_str()) != INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW((root + L"\\launcher.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
                    return root + L"\\launcher.exe";
            }
        }
    }
    if (_wcsicmp(PathFindFileNameW(target.c_str()), L"steamwebhelper.exe") == 0) {
        size_t bin = lower.find(L"\\bin\\cef\\");
        if (bin != wstring::npos) {
            wstring client = target.substr(0, bin) + L"\\steam.exe";
            if (GetFileAttributesW(client.c_str()) != INVALID_FILE_ATTRIBUTES) return client;
        }
    }
    return target;
}
wstring applicationExecutable(const Entry &entry) {
    if (entry.path == L"ms-settings:") {
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        return wstring(windows) + L"\\ImmersiveControlPanel\\SystemSettings.exe";
    }
    if (entry.path == L"https://www.bing.com") {
        wchar_t executable[32768]{};
        DWORD length = 32768;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"https", L"open",
                                        executable, &length)))
            return executable;
        return L"";
    }
    wstring target = entry.path;
    if (_wcsicmp(PathFindExtensionW(target.c_str()), L".lnk") == 0) {
        IShellLinkW *link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&link)))) {
            IPersistFile *file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
                if (SUCCEEDED(file->Load(target.c_str(), STGM_READ))) {
                    wchar_t path[32768]{};
                    if (SUCCEEDED(link->GetPath(path, 32768, nullptr, SLGP_RAWPATH)) && *path)
                        target = path;
                }
                file->Release();
            }
            link->Release();
        }
    }
    if (_wcsicmp(PathFindExtensionW(target.c_str()), L".exe") != 0)
        return L"";
    wchar_t full[32768]{};
    if (PathIsRelativeW(target.c_str()) &&
        SearchPathW(nullptr, target.c_str(), nullptr, 32768, full, nullptr))
        target = full;
    return applicationHost(target);
}
wstring browserExecutable(const Entry &entry) {
    wstring target = applicationExecutable(entry);
    const wchar_t *name = PathFindFileNameW(target.c_str());
    for (auto browser : {L"msedge.exe", L"chrome.exe", L"firefox.exe", L"brave.exe", L"opera.exe",
                         L"vivaldi.exe", L"zen.exe", L"QQBrowser.exe", L"360chrome.exe"})
        if (_wcsicmp(name, browser) == 0)
            return target;
    return L"";
}
wstring canonicalExecutable(wstring value) {
    if (!value.empty()) CharLowerBuffW(value.data(), (DWORD)value.size());
    return value;
}
bool minimizedWindowOwner(HWND window) {
    int depth = 0;
    for (HWND owner = GetWindow(window, GW_OWNER); owner && depth++ < 16; owner = GetWindow(owner, GW_OWNER))
        if (IsIconic(owner)) return true;
    return false;
}
bool auxiliaryWindowAvailable(const Entry &entry) {
    return IsWindowVisible(entry.window) || (!entry.closeRequested && minimizedWindowOwner(entry.window));
}
bool validAuxiliaryWindow(const Entry &entry, bool requireAvailable = true) {
    DWORD pid = 0;
    if (!entry.window || !IsWindow(entry.window) || (requireAvailable && !auxiliaryWindowAvailable(entry))) return false;
    GetWindowThreadProcessId(entry.window, &pid);
    if (pid != entry.windowPid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{}; DWORD length = 32768;
    bool match = QueryFullProcessImageNameW(process, 0, path, &length) &&
                 canonicalExecutable(path) == canonicalExecutable(entry.path);
    CloseHandle(process);
    return match;
}
void reconcileAuxiliaryCloseRequests() {
    for (auto request = auxiliaryCloseRequests.begin(); request != auxiliaryCloseRequests.end();) {
        wchar_t title[512]{}; GetWindowTextW(request->entry.window, title, 512);
        if (!validAuxiliaryWindow(request->entry, false) ||
            classOf(request->entry.window) != request->windowClass || request->entry.label != title) {
            request = auxiliaryCloseRequests.erase(request); continue;
        }
        if (minimizedWindowOwner(request->entry.window)) {
            request->ownerWasMinimized = true; ++request; continue;
        }
        if (request->ownerWasMinimized) {
            request->ownerWasMinimized = false;
            request->settleUntil = GetTickCount64() + 600;
        }
        // The first close was already posted. Give older asynchronous restore
        // messages time to finish before spending the bounded cleanup retries.
        if (GetTickCount64() < request->settleUntil) {
            if (dock) SetTimer(dock, 5, 100, nullptr);
            ++request; continue;
        }
        if (!IsWindowVisible(request->entry.window)) {
            request = auxiliaryCloseRequests.erase(request); continue;
        }
        if (request->attempts >= 3) { request = auxiliaryCloseRequests.erase(request); continue; }
        // An earlier async restore or an owner restore can show the popup
        // after WM_CLOSE. Finish only this close request after those settle.
        PostMessageW(request->entry.window, WM_CLOSE, 0, 0);
        ++request->attempts;
        if (dock) SetTimer(dock, 5, 100, nullptr);
        ++request;
    }
}
wstring entryExecutable(int index) {
    if (index < 0 || index >= (int)entries.size()) return L"";
    auto &path = entries[index].path;
    auto it = applicationTargets.find(path);
    if (it == applicationTargets.end())
        it = applicationTargets.emplace(path, canonicalExecutable(applicationExecutable(entries[index]))).first;
    return it->second;
}
wstring systemWindowKey(const Entry &entry) {
    if (_wcsicmp(entry.path.c_str(), L"explorer.exe") == 0) return L"explorer";
    if (entry.path == L"ms-settings:") return entry.path;
    if (entry.path == L"shell:RecycleBinFolder") return L"::{645ff040-5081-101b-9f08-00aa002f954e}";
    if (PathIsDirectoryW(entry.path.c_str())) return canonicalExecutable(entry.path);
    return L"";
}
bool isRunning(int index) {
    if (index < 0 || index >= (int)entries.size()) return false;
    if (entries[index].window) {
        DWORD pid = 0;
        GetWindowThreadProcessId(entries[index].window, &pid);
        return pid == entries[index].windowPid && auxiliaryWindowAvailable(entries[index]);
    }
    wstring key = systemWindowKey(entries[index]);
    if (!key.empty()) {
        auto found = systemWindows.find(key);
        return found != systemWindows.end() && !found->second.empty();
    }
    auto it = runningProcesses.find(entryExecutable(index));
    return it != runningProcesses.end() && !it->second.empty();
}
bool settingsProcess(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{}; DWORD length = 32768;
    bool match = QueryFullProcessImageNameW(process, 0, path, &length) &&
                 _wcsicmp(PathFindFileNameW(path), L"SystemSettings.exe") == 0;
    CloseHandle(process);
    return match;
}
BOOL CALLBACK settingsChild(HWND window, LPARAM context) {
    if (settingsProcess(window)) { *(bool *)context = true; return FALSE; }
    return TRUE;
}
void refreshSystemWindows() {
    std::map<wstring, std::vector<HWND>> fresh;
    IShellWindows *windows = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(&windows)))) {
        long count = 0; windows->get_Count(&count);
        for (long i = 0; i < count; ++i) {
            VARIANT index; VariantInit(&index); index.vt = VT_I4; index.lVal = i;
            IDispatch *dispatch = nullptr;
            if (FAILED(windows->Item(index, &dispatch)) || !dispatch) continue;
            IWebBrowser2 *browser = nullptr;
            if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&browser)))) {
                SHANDLE_PTR handle = 0; browser->get_HWND(&handle);
                HWND window = (HWND)handle;
                wstring cls = classOf(window);
                if (IsWindowVisible(window) && (cls == L"CabinetWClass" || cls == L"ExploreWClass")) {
                    fresh[L"explorer"].push_back(window);
                    IDispatch *document = nullptr;
                    if (SUCCEEDED(browser->get_Document(&document)) && document) {
                        IShellFolderViewDual *view = nullptr;
                        if (SUCCEEDED(document->QueryInterface(IID_PPV_ARGS(&view)))) {
                            Folder *folder = nullptr;
                            if (SUCCEEDED(view->get_Folder(&folder)) && folder) {
                                Folder2 *folder2 = nullptr;
                                if (SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&folder2)))) {
                                    FolderItem *self = nullptr;
                                    if (SUCCEEDED(folder2->get_Self(&self)) && self) {
                                        BSTR path = nullptr;
                                        if (SUCCEEDED(self->get_Path(&path)) && path) {
                                            fresh[canonicalExecutable(path)].push_back(window);
                                            SysFreeString(path);
                                        }
                                        self->Release();
                                    }
                                    folder2->Release();
                                }
                                folder->Release();
                            }
                            view->Release();
                        }
                        document->Release();
                    }
                }
                browser->Release();
            }
            dispatch->Release();
        }
        windows->Release();
    }
    EnumWindows([](HWND w, LPARAM context)->BOOL {
        if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        bool found = settingsProcess(w);
        if (!found && classOf(w) == L"ApplicationFrameWindow")
            EnumChildWindows(w, settingsChild, (LPARAM)&found);
        if (found) (*(std::map<wstring, std::vector<HWND>> *)context)[L"ms-settings:"].push_back(w);
        return TRUE;
    }, (LPARAM)&fresh);
    if (fresh != systemWindows) {
        systemWindows = std::move(fresh);
        if (dock) InvalidateRect(dock, nullptr, FALSE);
    }
}
// Shell surfaces are not launchable applications. Restrict name exclusions to
// Windows-owned paths so an unrelated user application is never hidden by name.
bool systemSurface(const wstring &path) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    wstring lower = canonicalExecutable(path);
    wstring root = canonicalExecutable(windows) + L"\\";
    if (lower.rfind(root, 0) != 0) return false;
    wstring name = PathFindFileNameW(lower.c_str());
    for (const auto *host : {L"applicationframehost.exe", L"startmenuexperiencehost.exe",
         L"shellexperiencehost.exe", L"searchhost.exe", L"searchapp.exe", L"searchui.exe",
         L"textinputhost.exe", L"lockapp.exe", L"runtimebroker.exe", L"dllhost.exe",
         L"backgroundtaskhost.exe", L"taskhostw.exe"})
        if (name == host) return true;
    return false;
}
bool auxiliarySurface(const wstring &path) {
    wstring lower = canonicalExecutable(path);
    // QQ Music embeds the same WMPF runtime as WeChat. Scope by installation
    // path, and keep each window independent instead of killing a shared host.
    return wstring(PathFindFileNameW(lower.c_str())) == L"wechatappex.exe" &&
           lower.find(L"\\tencent\\") != wstring::npos &&
           (lower.find(L"\\wmpf\\") != wstring::npos ||
            lower.find(L"\\xplugin\\plugins\\radiumwmpf\\") != wstring::npos);
}
BOOL CALLBACK collectApplications(HWND window, LPARAM context) {
    bool visible = IsWindowVisible(window) != FALSE;
    if ((!visible && !minimizedWindowOwner(window)) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    wstring cls = classOf(window);
    if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" ||
        cls == L"Shell_SecondaryTrayWnd") return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;
    if (std::any_of(auxiliaryCloseRequests.begin(), auxiliaryCloseRequests.end(), [&](const AuxiliaryCloseRequest &request) {
        return request.entry.window == window && request.entry.windowPid == pid;
    })) return TRUE;
    // Windows hides owned popups when their owner is minimized. Retain only
    // previously discovered pages, not all hidden helper/message windows.
    if (!visible && std::none_of(entries.begin(), entries.end(), [&](const Entry &entry) {
        return entry.window == window && entry.windowPid == pid && !entry.closeRequested;
    })) return TRUE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    wchar_t path[32768]{};
    DWORD length = 32768;
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        if (!systemSurface(path)) {
            if (auxiliarySurface(path) || GetWindow(window, GW_OWNER)) {
                // Ignore message/utility popups; keep real, titled auxiliary UI.
                wchar_t title[512]{};
                GetWindowTextW(window, title, 512);
                LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
                LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
                if (*title && !(extended & WS_EX_NOACTIVATE) &&
                    ((style & WS_CAPTION) || (extended & WS_EX_APPWINDOW)))
                {
                    Entry candidate{title, path, nullptr, false, nullptr, true, window, pid};
                    auto existing = std::find_if(entries.begin(), entries.end(), [&](const Entry &entry) {
                        return entry.window == window && entry.windowPid == pid &&
                               canonicalExecutable(entry.path) == canonicalExecutable(path);
                    });
                    if (existing != entries.end()) candidate.closeRequested = existing->closeRequested;
                    auxiliaryWindows.push_back(candidate);
                }
                CloseHandle(process);
                return TRUE;
            }
            wstring host = applicationHost(path), key = canonicalExecutable(host);
            (*(std::map<wstring, wstring> *)context)[key] = host;
            trackProcess(key, pid);
            auto &pids = visibleApplicationProcesses[key];
            if (std::find(pids.begin(), pids.end(), pid) == pids.end()) pids.push_back(pid);
        }
    }
    CloseHandle(process);
    return TRUE;
}
void syncRecentEntries() {
    std::vector<wstring> retained;
    for (const auto &path : recentPaths) {
        wstring key = canonicalExecutable(applicationHost(path));
        bool pinned = false;
        for (int i = 0; i < (int)entries.size(); ++i)
            if (!entries[i].temporary && entryExecutable(i) == key) { pinned = true; break; }
        // Older versions persisted web runtimes as ordinary recent programs.
        // A closed auxiliary window must not consume a retained top-three slot.
        if (pinned || systemSurface(path) || auxiliarySurface(path)) continue;
        auto running = runningProcesses.find(key);
        bool alive = processStillAlive(key) || (running != runningProcesses.end() && !running->second.empty());
        if (alive || (retained.size() < 3 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES))
            retained.push_back(path);
        else startupLog(L"recent removed: confirmed no live process; path=" + path);
    }
    if (retained != recentPaths) {
        recentPaths = retained;
        saveRecent();
    }
    std::vector<Entry> desired;
    if (showRecent) {
        for (const auto &path : retained)
            desired.push_back({labelFor(path), path, nullptr, false, nullptr, true});
        // Keep manual positions of surviving window entries without writing
        // transient HWNDs or helper executables into recent.ini.
        auto pending = auxiliaryWindows;
        int slot = 0;
        for (const auto &entry : entries) if (entry.temporary) {
            if (entry.window) {
                auto found = std::find_if(pending.begin(), pending.end(), [&](const Entry &candidate) {
                    return candidate.window == entry.window && candidate.windowPid == entry.windowPid &&
                           candidate.path == entry.path;
                });
                if (found != pending.end()) {
                    desired.insert(desired.begin() + std::min(slot, (int)desired.size()), *found);
                    pending.erase(found);
                }
            }
            ++slot;
        }
        desired.insert(desired.begin(), pending.begin(), pending.end());
    }
    std::stable_partition(entries.begin(), entries.end(), [](const Entry &e) { return !e.temporary; });
    std::vector<Entry> current;
    for (const auto &e : entries) if (e.temporary) current.push_back(e);
    if (current.size() == desired.size() && std::equal(current.begin(), current.end(), desired.begin(),
        [](const Entry &a, const Entry &b) {
            return a.path == b.path && a.window == b.window && a.windowPid == b.windowPid &&
                   a.label == b.label && a.closeRequested == b.closeRequested;
        })) return;
    hideName();
    for (auto &e : entries) if (e.temporary) { DestroyIcon(e.icon); delete e.image; }
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const Entry &e) { return e.temporary; }), entries.end());
    for (const auto &entry : desired) {
        entries.push_back(entry);
        loadIcon(entries.back());
    }
    hover = -1;
    magnification.clear();
}
void refreshRunning() {
    reconcileAuxiliaryCloseRequests();
    refreshSystemWindows();
    bool changed = false;
    bool stable = dragIndex < 0 && !dragReturning && !menuOpen;
    if (stable) {
        std::map<wstring, wstring> applications;
        visibleApplicationProcesses.clear();
        auxiliaryWindows.clear();
        EnumWindows(collectApplications, (LPARAM)&applications);
        for (const auto &app : applications) {
            bool pinned = false;
            for (int i = 0; i < (int)entries.size(); ++i)
                if (!entries[i].temporary && entryExecutable(i) == app.first) { pinned = true; break; }
            if (pinned) continue;
            bool known = std::find(recentPaths.begin(), recentPaths.end(), app.second) != recentPaths.end();
            if (previousApplications.count(app.first) && known) continue;
            if (recentScanStarted && !previousApplications.count(app.first)) {
                rememberRecent(app.second); // Newly opened apps enter at the left.
            } else if (std::find(recentPaths.begin(), recentPaths.end(), app.second) == recentPaths.end()) {
                // Startup must not reorder the user's persisted recent section.
                recentPaths.push_back(app.second);
                saveRecent();
            }
        }
        recentScanStarted = true;
        previousApplications = applications;
    }
    std::map<wstring, std::vector<DWORD>> fresh;
    std::vector<wstring> names;
    for (int i = 0; i < (int)entries.size(); ++i) {
        wstring target = entryExecutable(i);
        if (!target.empty()) {
            fresh[target];
            names.push_back(PathFindFileNameW(target.c_str()));
        }
    }
    for (const auto &path : recentPaths) {
        wstring target = canonicalExecutable(applicationHost(path));
        fresh[target];
        names.push_back(PathFindFileNameW(target.c_str()));
    }
    // A live top-level window is direct running evidence. Process snapshots may
    // temporarily omit a process; never evict its Dock entry while its window exists.
    if (stable) for (const auto &app : visibleApplicationProcesses) {
        auto it = fresh.find(app.first);
        if (it != fresh.end()) it->second = app.second;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W process{sizeof(process)};
    if (Process32FirstW(snapshot, &process)) do {
        wstring name = canonicalExecutable(process.szExeFile);
        if (name != L"hyp.exe" && std::find(names.begin(), names.end(), name) == names.end()) continue;
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
        if (!handle) continue;
        wchar_t path[32768]{};
        DWORD length = 32768;
        if (QueryFullProcessImageNameW(handle, 0, path, &length)) {
            auto it = fresh.find(canonicalExecutable(applicationHost(path)));
            if (it != fresh.end()) {
                trackProcess(it->first, process.th32ProcessID);
                if (std::find(it->second.begin(), it->second.end(), process.th32ProcessID) == it->second.end())
                    it->second.push_back(process.th32ProcessID);
            }
        }
        CloseHandle(handle);
    } while (Process32NextW(snapshot, &process));
    CloseHandle(snapshot);
    for (auto &app : trackedProcesses) {
        if (!processStillAlive(app.first)) continue;
        auto it = fresh.find(app.first);
        if (it == fresh.end()) continue;
        for (const auto &process : app.second)
            if (std::find(it->second.begin(), it->second.end(), process.pid) == it->second.end())
                it->second.push_back(process.pid);
    }
    for (auto it = trackedProcesses.begin(); it != trackedProcesses.end(); ) {
        if (it->second.empty()) it = trackedProcesses.erase(it);
        else ++it;
    }
    if (fresh != runningProcesses) {
        runningProcesses = std::move(fresh);
        InvalidateRect(dock, nullptr, FALSE);
    }
    if (stable) {
        std::vector<std::pair<wstring, HWND>> before;
        for (const auto &e : entries) before.push_back({e.path, e.window});
        syncRecentEntries();
        std::vector<std::pair<wstring, HWND>> after;
        for (const auto &e : entries) after.push_back({e.path, e.window});
        changed = before != after;
    }
    if (changed && dock) {
        hover = -1;
        focusIndex = std::min(focusIndex, itemCount() - 1);
        magnification.clear();
        positionDock();
        InvalidateRect(dock, nullptr, FALSE);
    }
}
struct CloseApplication { wstring executable; int count = 0; };
BOOL CALLBACK closeApplicationWindow(HWND window, LPARAM context) {
    auto &request = *(CloseApplication *)context;
    if (!IsWindowVisible(window) || window == dock || window == shadow || window == tip ||
        GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        return TRUE;
    // Explorer owns the desktop and taskbar as well as file windows.
    wstring cls = classOf(window);
    if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" ||
        cls == L"Shell_SecondaryTrayWnd") return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) return TRUE;
    wchar_t path[32768]{};
    DWORD length = 32768;
    bool match = QueryFullProcessImageNameW(handle, 0, path, &length) &&
                 canonicalExecutable(path) == request.executable;
    CloseHandle(handle);
    if (match && PostMessageW(window, WM_CLOSE, 0, 0)) ++request.count;
    return TRUE;
}
struct BrowserWindowSearch {
    wstring executable;
    HWND window = nullptr;
};
void updateGhost(float dt) {
    if (ghostEntry < 0 || ghostEntry >= (int)entries.size()) return;
    float blend = 1.f - std::exp(-28.f * dt);
    ghostX += (ghostTargetX - ghostX) * blend;
    ghostY += (ghostTargetY - ghostY) * blend;
    if (dragReturning) ghostExpansion *= 1.f - blend;
    if (dragReturning && std::abs(ghostX - ghostTargetX) < .6f &&
        std::abs(ghostY - ghostTargetY) < .6f) {
        dragReturning = false;
        ghostEntry = -1;
        ShowWindow(dragGhost, SW_HIDE);
        return;
    }
    if (!dragGhost)
        dragGhost = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                    WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                    L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                                    dock, nullptr, instance, nullptr);
    int width = scale(220), height = scale(96);
    int icon = scale(34) + (int)std::lround(scale(16) * ghostExpansion);
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) { DeleteDC(dc); ReleaseDC(nullptr, screen); return; }
    auto old = SelectObject(dc, bitmap);
    ZeroMemory(bits, width * height * 4);
    {
        using namespace Gdiplus;
        Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, (BYTE *)bits);
        Graphics g(&surface);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        if (entries[ghostEntry].image)
            g.DrawImage(entries[ghostEntry].image, (width - icon) / 2, height - icon, icon, icon);
        if (!dragReturning) {
            bool ready = upwardDistance >= scale(64) && isRunning(ghostEntry);
            GraphicsPath pill;
            roundedPath(pill, 1.f, 1.f, (float)width - 2, (float)scale(29), (float)scale(10));
            SolidBrush background(ready ? Color(240, 164, 48, 55) : Color(230, 40, 42, 48));
            g.FillPath(&background, &pill);
            Font labelFont(L"Microsoft YaHei UI", (REAL)scale(11), FontStyleRegular, UnitPixel);
            SolidBrush foreground(Color(255, 255, 255, 255));
            StringFormat format;
            format.SetAlignment(StringAlignmentCenter);
            format.SetLineAlignment(StringAlignmentCenter);
            const wchar_t *text = !isRunning(ghostEntry) ? L"应用未运行 · 松手返回" :
                                  ready ? L"松手关闭 · 下移取消" : L"继续上滑关闭";
            g.DrawString(text, -1, &labelFont, RectF(0, 0, (REAL)width, (REAL)scale(30)),
                         &format, &foreground);
        }
    }
    POINT destination{(LONG)std::lround(ghostX) - width / 2,
                      (LONG)std::lround(ghostY) - height + icon / 2};
    POINT origin{};
    SIZE size{width, height};
    BLENDFUNCTION blendFunction{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(dragGhost, screen, &destination, &size, dc, &origin, 0,
                        &blendFunction, ULW_ALPHA);
    ShowWindow(dragGhost, SW_SHOWNOACTIVATE);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
void finishVerticalDrag(bool commit) {
    if (!verticalDrag) return;
    verticalDrag = false;
    if (commit && ghostEntry >= 0 && upwardDistance >= scale(64) && isRunning(ghostEntry)) {
        bool restorePending = windowActivationPending(entries[ghostEntry].window);
        cancelWindowActivation();
        wstring target = entryExecutable(ghostEntry);
        refreshRunning();
        wstring windowKey = systemWindowKey(entries[ghostEntry]);
        if (entries[ghostEntry].window) {
            if (validAuxiliaryWindow(entries[ghostEntry])) {
                entries[ghostEntry].closeRequested = PostMessageW(entries[ghostEntry].window, WM_CLOSE, 0, 0) != FALSE;
                if (entries[ghostEntry].closeRequested &&
                    (restorePending || minimizedWindowOwner(entries[ghostEntry].window))) {
                    Entry closed = entries[ghostEntry]; closed.icon = nullptr; closed.image = nullptr;
                    wchar_t title[512]{}; GetWindowTextW(closed.window, title, 512); closed.label = title;
                    auxiliaryCloseRequests.push_back({closed, 0, GetTickCount64() + 600,
                                                     minimizedWindowOwner(closed.window), classOf(closed.window)});
                }
            }
        } else if (!windowKey.empty()) {
            for (HWND window : systemWindows[windowKey])
                if (IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
        } else if (canonicalExecutable(PathFindFileNameW(target.c_str())) == L"steam.exe") {
            // Exit the client, not its replaceable CEF interface process.
            ShellExecuteW(dock, L"open", target.c_str(), L"-shutdown", nullptr, SW_HIDE);
        } else if (canonicalExecutable(PathFindFileNameW(target.c_str())) == L"explorer.exe") {
            CloseApplication request{target};
            EnumWindows(closeApplicationWindow, (LPARAM)&request);
        } else {
            bool failed = false;
            auto pids = runningProcesses[target];
            for (DWORD pid : pids) {
                if (pid == GetCurrentProcessId()) continue;
                HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                              FALSE, pid);
                if (!process) { failed = true; continue; }
                wchar_t path[32768]{};
                DWORD length = 32768;
                if (QueryFullProcessImageNameW(process, 0, path, &length) &&
                    canonicalExecutable(applicationHost(path)) == target) {
                    if (TerminateProcess(process, 0)) WaitForSingleObject(process, 100);
                    else if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) failed = true;
                }
                CloseHandle(process);
            }
            if (failed) startupLog(L"close: process exited during enumeration or access unavailable; refreshing actual state");
        }
        refreshRunning();
        SetTimer(dock, 5, 100, nullptr);
    }
    dragReturning = true;
    ghostTargetX = (float)ghostHome.x;
    ghostTargetY = (float)ghostHome.y;
    hover = -1;
    startIconAnimation();
}
BOOL CALLBACK findBrowserWindow(HWND window, LPARAM param) {
    auto &search = *(BrowserWindowSearch *)param;
    if (GetWindow(window, GW_OWNER) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return TRUE;
    wchar_t path[32768]{};
    DWORD length = 32768;
    bool match = QueryFullProcessImageNameW(process, 0, path, &length) &&
                 _wcsicmp(applicationHost(path).c_str(), search.executable.c_str()) == 0;
    CloseHandle(process);
    if (!match)
        return TRUE;
    if (IsWindowVisible(window)) {
        search.window = window; // Prefer an already-visible top-level window.
        return FALSE;
    }
    // HoYo hides its real Qt main window on close-to-tray. Do not mistake its
    // hidden Chromium/IME/tray message windows for the user interface.
    if (!search.window && _wcsicmp(PathFindFileNameW(path), L"HYP.exe") == 0 &&
        classOf(window).find(L"QWindowIcon") != wstring::npos &&
        GetWindowTextLengthW(window) > 0 &&
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION)
        search.window = window;
    return TRUE;
}
struct WindowActivation {
    HWND window = nullptr;
    DWORD pid = 0, inputTime = 0;
    wstring windowClass;
    int attempts = 0;
    bool wasVisible = false;
} pendingActivation;
bool windowActivationPending(HWND window) {
    return window && pendingActivation.window &&
           (pendingActivation.window == window ||
            GetAncestor(pendingActivation.window, GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER));
}
void cancelWindowActivation() {
    if (dock) KillTimer(dock, 6);
    pendingActivation = {};
}
bool tryWindowActivation() {
    HWND selected = pendingActivation.window;
    // A disabled owner can only receive input through its modal popup. Do not
    // otherwise replace a specifically selected article with another page.
    for (int depth = 0; depth < 16 && !IsWindowEnabled(selected); ++depth) {
        HWND popup = GetLastActivePopup(selected);
        if (!popup || popup == selected || !IsWindowVisible(popup)) break;
        selected = popup;
    }
    std::vector<HWND> owners;
    for (HWND owner = GetWindow(selected, GW_OWNER); owner && owners.size() < 16;
         owner = GetWindow(owner, GW_OWNER)) owners.push_back(owner);
    for (auto owner = owners.rbegin(); owner != owners.rend(); ++owner)
        if (IsIconic(*owner)) ShowWindowAsync(*owner, SW_RESTORE);
    if (IsIconic(selected)) ShowWindowAsync(selected, SW_RESTORE);
    else if (!IsWindowVisible(selected)) ShowWindowAsync(selected, SW_SHOW);
    SetWindowPos(selected, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    SetForegroundWindow(selected);
    return GetForegroundWindow() == selected && IsWindowVisible(selected) && !IsIconic(selected) &&
           std::none_of(owners.begin(), owners.end(), [](HWND owner) { return IsIconic(owner) != FALSE; });
}
void retryWindowActivation() {
    if (!pendingActivation.window) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(pendingActivation.window, &pid);
    if (pid != pendingActivation.pid || classOf(pendingActivation.window) != pendingActivation.windowClass) {
        cancelWindowActivation();
        return;
    }
    if (std::any_of(entries.begin(), entries.end(), [](const Entry &entry) {
        return entry.window == pendingActivation.window && entry.windowPid == pendingActivation.pid && entry.closeRequested;
    })) {
        cancelWindowActivation();
        return;
    }
    LASTINPUTINFO input{sizeof(input)};
    HWND foreground = GetForegroundWindow();
    if (GetLastInputInfo(&input) && input.dwTime != pendingActivation.inputTime &&
        ((pendingActivation.wasVisible && !IsWindowVisible(pendingActivation.window)) ||
         (foreground != dock && foreground != pendingActivation.window &&
          GetAncestor(foreground, GA_ROOTOWNER) != GetAncestor(pendingActivation.window, GA_ROOTOWNER)))) {
        cancelWindowActivation(); // A newer user action takes priority over this click.
        return;
    }
    pendingActivation.wasVisible = pendingActivation.wasVisible || IsWindowVisible(pendingActivation.window);
    if (tryWindowActivation()) {
        if (dock) SetPropW(dock, L"QuietDock.ActivationSucceeded", (HANDLE)1);
        cancelWindowActivation();
    } else if (++pendingActivation.attempts >= 8) {
        startupLog(L"activation: existing window did not become visible foreground hwnd=" +
                   std::to_wstring((uintptr_t)pendingActivation.window));
        FLASHWINFO flash{sizeof(flash), pendingActivation.window, FLASHW_TRAY, 2, 0};
        FlashWindowEx(&flash);
        cancelWindowActivation();
    }
}
bool requestWindowActivation(HWND window) {
    cancelWindowActivation();
    if (!IsWindow(window)) return false;
    auxiliaryCloseRequests.erase(std::remove_if(auxiliaryCloseRequests.begin(), auxiliaryCloseRequests.end(),
        [&](const AuxiliaryCloseRequest &request) { return request.entry.window == window; }), auxiliaryCloseRequests.end());
    HWND root = GetAncestor(window, GA_ROOT);
    pendingActivation.window = root ? root : window;
    GetWindowThreadProcessId(pendingActivation.window, &pendingActivation.pid);
    pendingActivation.windowClass = classOf(pendingActivation.window);
    pendingActivation.wasVisible = IsWindowVisible(pendingActivation.window) != FALSE;
    LASTINPUTINFO input{sizeof(input)};
    if (GetLastInputInfo(&input)) pendingActivation.inputTime = input.dwTime;
    if (dock) {
        SetPropW(dock, L"QuietDock.ActivationTarget", pendingActivation.window);
        SetPropW(dock, L"QuietDock.ActivationSucceeded", nullptr);
    }
    retryWindowActivation();
    // ShowWindowAsync and cross-thread activation complete in the target's
    // message loop. Recheck briefly without blocking the Dock or sharing queues.
    if (pendingActivation.window && dock) SetTimer(dock, 6, 60, nullptr);
    return true;
}
bool activateExistingBrowser(const wstring &executable) {
    if (executable.empty())
        return false;
    BrowserWindowSearch search{executable};
    EnumWindows(findBrowserWindow, (LPARAM)&search);
    if (!search.window)
        return false;
    requestWindowActivation(search.window);
    // A focus restriction must never turn a switch request into a new window.
    return true;
}
void launch(int i) {
    if (hasOverflow() && i == visibleCount()) {
        showOverflow();
        return;
    }
    if (i < 0 || i >= (int)entries.size())
        return;
    openEntry(i);
}
void openEntry(int i) {
    cancelWindowActivation();
    auto &e = entries[i];
    if (e.window) {
        if ((!e.closeRequested || IsWindowVisible(e.window)) && validAuxiliaryWindow(e, false)) {
            e.closeRequested = false;
            requestWindowActivation(e.window);
        }
        return;
    }
    wstring key = systemWindowKey(e);
    if (!key.empty()) {
        refreshSystemWindows();
        for (HWND window : systemWindows[key]) {
            if (!IsWindow(window)) continue;
            requestWindowActivation(window);
            return;
        }
    }
    wstring browser = browserExecutable(e);
    wstring executable = applicationExecutable(e);
    // HYP runs elevated. Its own stable launcher can signal the existing
    // instance; a medium-integrity Dock cannot reliably restore its window.
    if (_wcsicmp(PathFindFileNameW(executable.c_str()), L"launcher.exe") == 0) {
        size_t slash = executable.find_last_of(L"\\/");
        wstring folder = slash == wstring::npos ? L"" : executable.substr(0, slash);
        wchar_t game[128]{};
        GetPrivateProfileStringW(L"hyp", L"primary_game", L"", game, 128,
                                  (folder + L"\\config.ini").c_str());
        if (wcsncmp(game, L"hyp_", 4) == 0) {
            BrowserWindowSearch search{executable};
            EnumWindows(findBrowserWindow, (LPARAM)&search);
            if (search.window) {
                DWORD pid = 0;
                GetWindowThreadProcessId(search.window, &pid);
                AllowSetForegroundWindow(pid);
            }
            HINSTANCE result = ShellExecuteW(dock, L"open", executable.c_str(), nullptr,
                                               folder.c_str(), SW_SHOWNORMAL);
            if ((INT_PTR)result <= 32)
                MessageBoxW(dock, L"无法唤起米哈游启动器，请检查安装路径。",
                            L"QuietDock", MB_ICONWARNING);
            return;
        }
    }
    if (activateExistingBrowser(executable))
        return;
    // The built-in browser entry opens the browser itself, not a new Bing page.
    wstring target = e.path == L"https://www.bing.com" && !browser.empty() ? browser : e.path;
    target = applicationHost(target);
    HINSTANCE r = ShellExecuteW(dock, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        MessageBoxW(
            dock,
            (L"无法打开：\n" + e.path + L"\n\n请检查文件是否仍存在。右键可移除此入口。").c_str(),
            L"QuietDock", MB_ICONWARNING);
}
void resizeDock(int percent) {
    percent = std::clamp(percent, 75, 200);
    if (dockZoom == percent)
        return;
    dockZoom = percent;
    KillTimer(dock, 2);
    animationActive = false;
    hover = -1;
    pointerX = -10000.f;
    magnification.clear();
    iconVelocity.clear();
    positionDock();
    saveConfig();
    InvalidateRect(dock, nullptr, FALSE);
}
int hitTest(POINT p) {
    for (int i = 0; i < itemCount(); i++) {
        RECT r = itemRect(i);
        if (PtInRect(&r, p))
            return i;
    }
    return -1;
}
wstring startupKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
wstring startupCommand() { return L"\"" + exePath + L"\" --data-dir \"" + dataDir + L"\""; }
bool startupEnabled() {
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, startupKey.c_str(), L"QuietDock", RRF_RT_REG_SZ,
                        nullptr, value, &bytes) == ERROR_SUCCESS && value == startupCommand();
}
bool setStartup(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, startupKey.c_str(), 0, nullptr, 0,
                       KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    wstring command = startupCommand();
    LSTATUS status = enabled ?
        RegSetValueExW(key, L"QuietDock", 0, REG_SZ, (const BYTE *)command.c_str(),
                       (DWORD)(command.size() + 1) * sizeof(wchar_t)) :
        RegDeleteValueW(key, L"QuietDock");
    RegCloseKey(key);
    return (status == ERROR_SUCCESS || (!enabled && status == ERROR_FILE_NOT_FOUND)) &&
           startupEnabled() == enabled;
}
void showMenu(POINT pt, int index) {
    if (index >= visibleCount())
        index = -1;
    HMENU m = CreatePopupMenu();
    if (index >= 0 && index < (int)entries.size()) {
        AppendMenuW(m, MF_STRING, 10, L"打开");
        if (entries[index].temporary && !entries[index].window)
            AppendMenuW(m, MF_STRING, 16, L"固定到 Dock");
        if (!entries[index].fixed && !entries[index].temporary)
            AppendMenuW(m, MF_STRING, 11, L"移除此入口");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    if (hasOverflow())
        AppendMenuW(m, MF_STRING, 15, L"更多入口…");
    AppendMenuW(m, MF_STRING, 12, L"添加应用或文件…");
    AppendMenuW(m, MF_STRING | (showRecent ? MF_CHECKED : 0), 18, L"显示最近打开（左侧 3 个保留）");
    HMENU sizes = CreatePopupMenu();
    for (int percent : {75, 100, 125, 150, 175, 200}) {
        wstring label = std::to_wstring(percent) + L"%" + (percent == 125 ? L"（默认）" : L"");
        AppendMenuW(sizes, MF_STRING | (dockZoom == percent ? MF_CHECKED : 0),
                    1000 + percent, label.c_str());
    }
    AppendMenuW(sizes, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sizes, MF_STRING | (dockZoom == 200 ? MF_GRAYED : 0), 20, L"放大 5%\tCtrl + +");
    AppendMenuW(sizes, MF_STRING | (dockZoom == 75 ? MF_GRAYED : 0), 21, L"缩小 5%\tCtrl + -");
    wstring sizeLabel = L"Dock 大小 · " + std::to_wstring(dockZoom) + L"%";
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sizes, sizeLabel.c_str());
    AppendMenuW(m, MF_STRING | (clean ? MF_CHECKED : 0), 13, L"净桌面模式");
    AppendMenuW(m, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), 17, L"开机自启");
    AppendMenuW(m, MF_STRING, 14, L"恢复桌面并退出");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"恢复热键  Ctrl + Alt + F12");
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"调整大小  Ctrl + 滚轮");
    SetForegroundWindow(dock);
    menuOpen = true;
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, dock, nullptr);
    menuOpen = false;
    DestroyMenu(m);
    if (cmd >= 1075 && cmd <= 1200)
        resizeDock(cmd - 1000);
    if (cmd == 20 || cmd == 21)
        resizeDock(dockZoom + (cmd == 20 ? 5 : -5));
    if (cmd == 10)
        launch(index);
    if (cmd == 16 && index >= 0)
        addPath(entries[index].path);
    if (cmd == 11) {
        if (!entries[index].temporary && !recordPin(entries[index].path, false)) return;
        DestroyIcon(entries[index].icon);
        delete entries[index].image;
        entries.erase(entries.begin() + index);
        hover = -1;
        focusIndex = std::min(focusIndex, itemCount() - 1);
        saveConfig();
        positionDock();
        InvalidateRect(dock, nullptr, FALSE);
    }
    if (cmd == 18) {
        saveRecentOrder();
        showRecent = !showRecent;
        saveRecent();
        syncRecentEntries();
        positionDock();
        InvalidateRect(dock, nullptr, FALSE);
    }
    if (cmd == 15)
        showOverflow();
    if (cmd == 12)
        chooseFile();
    if (cmd == 13)
        toggleClean();
    if (cmd == 17 && !setStartup(!startupEnabled()))
        MessageBoxW(dock, L"开机自启设置未能保存，请稍后重试。", L"QuietDock", MB_ICONWARNING);
    if (cmd == 14)
        DestroyWindow(dock);
}
void paint() {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(dock, &ps);
    RECT client;
    GetClientRect(dock, &client);
    int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0) {
        EndPaint(dock, &ps);
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(target, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        EndPaint(dock, &ps);
        return;
    }
    auto old = SelectObject(dc, bitmap);
    ZeroMemory(pixels, width * height * 4);
    {
        using namespace Gdiplus;
        Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, (BYTE *)pixels);
        Graphics g(&surface);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        if (!glassActive) {
            g.Clear(Color(255, 225, 229, 235));
        }
        GraphicsPath base;
        roundedPath(base, .5f, .5f, (float)width - 1, (float)height - 1, (float)MulDiv(8, dpi, 96));
        // Translucent highlight over the system-rendered material; no captured desktop pixels.
        LinearGradientBrush tint(Point(0, 0), Point(0, height),
                                 glassActive ? Color(22, 255, 255, 255) : Color(255, 235, 237, 240),
                                 glassActive ? Color(8, 230, 235, 242) : Color(255, 213, 218, 225));
        g.FillRectangle(&tint, 0, 0, width, height);
        for (int i = 0; i < itemCount(); i++) {
            RECT r = itemRect(i);
            if (i < visibleCount() && isRunning(i)) {
                SolidBrush runningDot(Color(255, 66, 216, 115));
                float diameter = (float)std::max(3, scale(3));
                g.FillEllipse(&runningDot, (r.left + r.right - diameter) * .5f,
                              height - diameter - scale(1), diameter, diameter);
            }
            if ((verticalDrag || dragReturning) && i == ghostEntry) continue;
            float amount = i < (int)magnification.size() ? magnification[i] : 0.f;
            float icon = scale(34) + scale(16) * amount;
            float x = (r.left + r.right - icon) * .5f;
            float y = (height - icon) * .5f - scale(2) * amount;
            bool selected = GetFocus() == dock && i == focusIndex;
            if (selected) {
                GraphicsPath active;
                roundedPath(active, (float)r.left + scale(3), (float)r.top + scale(2),
                            (float)(r.right - r.left) - scale(6),
                            (float)(r.bottom - r.top) - scale(4), (float)scale(13));
                if (GetFocus() == dock && i == focusIndex) {
                    Pen focus(Color(180, 72, 118, 178), 1.5f);
                    g.DrawPath(&focus, &active);
                }
            }
            if (i < visibleCount() && entries[i].icon) {
                if (entries[i].image) {
                    // Built-in utilities share the rounded-square silhouette of macOS icons.
                    // User applications retain their original artwork and transparency.
                    bool tile = entries[i].fixed && entries[i].path != L"https://www.bing.com";
                    if (tile) {
                        GraphicsPath tileShadow, tileShape;
                        roundedPath(tileShadow, (float)x, (float)y + scale(1), (float)icon,
                                    (float)icon, icon * .23f);
                        SolidBrush shade(Color(40, 0, 0, 0));
                        g.FillPath(&shade, &tileShadow);
                        roundedPath(tileShape, (float)x, (float)y, (float)icon, (float)icon, icon * .23f);
                        LinearGradientBrush tileFill(PointF(x, y), PointF(x, y + icon),
                                                      Color(255, 251, 252, 253), Color(255, 214, 222, 232));
                        g.FillPath(&tileFill, &tileShape);
                        Pen tileEdge(Color(180, 255, 255, 255), 1.f);
                        g.DrawPath(&tileEdge, &tileShape);
                    }
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    float inset = tile ? icon * .09f : 0.f;
                    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                    g.DrawImage(entries[i].image, x + inset, y + inset, icon - inset * 2, icon - inset * 2);
                }
            } else {
                Pen symbol(Color(220, 76, 83, 96), scale(2));
                symbol.SetStartCap(LineCapRound);
                symbol.SetEndCap(LineCapRound);
                int cx = (r.left + r.right) / 2, cy = height / 2;
                if (hasOverflow() && i == visibleCount()) {
                    SolidBrush dot(Color(220, 76, 83, 96));
                    for (int dx = -scale(9); dx <= scale(9); dx += scale(9))
                        g.FillEllipse(&dot, cx + dx - scale(2), cy - scale(2), scale(4), scale(4));
                }

            }
        }
        if (recentGap() > 0) {
            int separator = scale(10) + recentStart() * slotWidth() + recentGap() / 2;
            // Rounded graphite stroke with a light rim for contrast on glass.
            float stroke = std::max(1.5f, scale(2) * 1.f);
            Pen rim(Color(115, 255, 255, 255), stroke + 2.f);
            rim.SetStartCap(LineCapRound);
            rim.SetEndCap(LineCapRound);
            Pen line(Color(165, 62, 70, 82), stroke);
            line.SetStartCap(LineCapRound);
            line.SetEndCap(LineCapRound);
            g.DrawLine(&rim, separator, scale(12), separator, height - scale(12));
            g.DrawLine(&line, separator, scale(12), separator, height - scale(12));
        }
    }
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(dock, &ps);
}
void updateFullscreenVisibility() {
    HWND foreground = GetForegroundWindow();
    // Preserve interaction with the Dock and its owned menus/file dialogs.
    if (foreground == dock || (foreground && GetAncestor(foreground, GA_ROOTOWNER) == dock))
        return;
    wstring cls = classOf(foreground);
    bool full = false;
    if (foreground && !IsIconic(foreground) && cls != L"Progman" && cls != L"WorkerW" &&
        cls != L"Shell_TrayWnd" && cls != L"Shell_SecondaryTrayWnd") {
        RECT bounds{};
        if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS,
                                         &bounds, sizeof(bounds))) || IsRectEmpty(&bounds))
            GetWindowRect(foreground, &bounds);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(dock, MONITOR_DEFAULTTOPRIMARY), &monitor);
        bool maximizedHere = IsZoomed(foreground) &&
            MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY) ==
            MonitorFromWindow(dock, MONITOR_DEFAULTTOPRIMARY);
        full = maximizedHere || (bounds.left <= monitor.rcMonitor.left &&
               bounds.top <= monitor.rcMonitor.top && bounds.right >= monitor.rcMonitor.right &&
               bounds.bottom >= monitor.rcMonitor.bottom);
    }
    hiddenForFullscreen = full;
    if (hiddenForFullscreen) {
        verticalDrag = dragReturning = dragging = false;
        dragIndex = ghostEntry = -1;
        if (GetCapture() == dock) ReleaseCapture();
        if (dragGhost) ShowWindow(dragGhost, SW_HIDE);
        ShowWindow(dock, SW_HIDE);
        if (shadow)
            ShowWindow(shadow, SW_HIDE);
        KillTimer(dock, 2);
        animationActive = false;
        hover = -1;
        pointerX = -10000.f;
        magnification.clear();
        iconVelocity.clear();
        hideName();
    } else {
        // Stay above ordinary windows; fullscreen applications hide both surfaces.
        if (shadow)
            SetWindowPos(shadow, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(dock, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(dock, nullptr, FALSE);
    }
}
void CALLBACK winEvent(HWINEVENTHOOK, DWORD event, HWND w, LONG object, LONG, DWORD, DWORD) {
    if (!dock || shutting)
        return;
    if (event == EVENT_SYSTEM_FOREGROUND ||
        (object == OBJID_WINDOW && (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE ||
                                   event == EVENT_OBJECT_DESTROY)))
        PostMessageW(dock, WM_APP + 5, 0, 0);
    if (event == EVENT_SYSTEM_FOREGROUND ||
        (event == EVENT_OBJECT_LOCATIONCHANGE && object == OBJID_WINDOW &&
         w == GetForegroundWindow())) {
        PostMessageW(dock, WM_FOREGROUND, 0, 0);
        return;
    }
    if (event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW)
        return;
    if (object == OBJID_WINDOW && w && w != dock) {
        wstring cls = classOf(w);
        if (cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd" ||
            cls == L"SysListView32" || cls == L"SHELLDLL_DefView")
            PostMessageW(dock, WM_SHELLCHECK, 0, 0);
    }
}
void recreateFont() {
    if (font)
        DeleteObject(font);
    font = CreateFontW(-scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
}
LRESULT CALLBACK nameProc(HWND h, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(h, &paint);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH background = CreateSolidBrush(RGB(40, 42, 48));
        FillRect(dc, &r, background);
        DeleteObject(background);
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        wchar_t text[1024]{};
        GetWindowTextW(h, text, 1024);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(h, &paint);
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}
LRESULT CALLBACK windowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarMessage && msg == taskbarMessage) {
        PostMessageW(h, WM_SHELLCHECK, 0, 0);
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        dock = h;
        dpi = GetDpiForWindow(h);
        recreateFont();
        {
        WNDCLASSW labelClass{};
        labelClass.lpfnWndProc = nameProc;
        labelClass.hInstance = instance;
        labelClass.lpszClassName = L"QuietDock.Name";
        RegisterClassW(&labelClass);
        tip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                              labelClass.lpszClassName, nullptr, WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, h, nullptr, instance, nullptr);
        }
        DragAcceptFiles(h, FALSE);
        refreshRunning();
        SetTimer(h, 4, 1500, nullptr);
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int next = hitTest(p);
        if (dragIndex >= 0 && (wp & MK_LBUTTON)) {
            int dx = p.x - dragStart.x, dy = p.y - dragStart.y;
            if (!dragging && (std::abs(dx) > std::max(scale(6), GetSystemMetrics(SM_CXDRAG)) ||
                std::abs(dy) > std::max(scale(6), GetSystemMetrics(SM_CYDRAG)))) {
                dragging = true;
                verticalDrag = std::abs(dy) > std::abs(dx);
                if (verticalDrag) {
                    ghostEntry = dragIndex;
                    RECT r = itemRect(dragIndex), client{};
                    GetClientRect(h, &client);
                    ghostHome = {(r.left + r.right) / 2, client.bottom / 2};
                    ClientToScreen(h, &ghostHome);
                    ghostX = (float)ghostHome.x;
                    ghostY = (float)ghostHome.y;
                    ghostExpansion = 1.f;
                    startIconAnimation();
                }
            }
            if (dragging) {
                hideName();
                if (verticalDrag) {
                    upwardDistance = -dy;
                    ghostTargetX = (float)ghostHome.x;
                    ghostTargetY = (float)ghostHome.y + dy;
                    startIconAnimation();
                    return 0;
                }
                if (next >= 0 && next < visibleCount() && next != dragIndex &&
                    entries[dragIndex].temporary == entries[next].temporary) {
                    reorderEntry(dragIndex, next);
                    dragIndex = next;
                    hover = next;
                    focusIndex = next;
                    magnification.assign(itemCount(), 0.f);
                    updateTooltips();
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
        }
        if (next != hover) {
            hideName();
            hover = next;
            pointerX = (float)p.x;
            startIconAnimation();
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (dragIndex >= 0) return 0;
        hideName();
        hover = -1;
        pointerX = -10000.f;
        startIconAnimation();
        return 0;
    case WM_LBUTTONDOWN: {
        cancelWindowActivation();
        if (dragReturning) {
            dragReturning = false;
            ghostEntry = -1;
            ShowWindow(dragGhost, SW_HIDE);
        }
        dragStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hit = hitTest(dragStart);
        dragIndex = hit >= 0 && hit < visibleCount() ? hit : -1;
        dragging = false;
        if (dragIndex >= 0 && GetCapture() != h) SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        bool closing = verticalDrag;
        bool reordered = dragging;
        if (closing) {
            upwardDistance = dragStart.y - GET_Y_LPARAM(lp);
            finishVerticalDrag(true);
        }
        dragIndex = -1;
        dragging = false;
        if (GetCapture() == h) ReleaseCapture();
        if (reordered) {
            if (!closing) saveConfig();
            startIconAnimation();
            return 0;
        }
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i = hitTest(p);
        SetFocus(h);
        if (i >= 0) {
            focusIndex = i;
            launch(i);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (verticalDrag) finishVerticalDrag(false);
        else if (dragging) saveConfig();
        dragging = false;
        dragIndex = -1;
        return 0;
    case WM_CONTEXTMENU: {
        cancelWindowActivation();
        POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, local = screen;
        if (screen.x == -1) {
            RECT r = itemRect(focusIndex);
            local = {r.left, r.bottom};
            screen = local;
            ClientToScreen(h, &screen);
        } else
            ScreenToClient(h, &local);
        showMenu(screen, hitTest(local));
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && dragIndex >= 0) {
            if (verticalDrag) finishVerticalDrag(false);
            else if (dragging) saveConfig();
            dragIndex = -1;
            dragging = false;
            if (GetCapture() == h) ReleaseCapture();
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) &&
            (wp == VK_OEM_PLUS || wp == VK_ADD || wp == VK_OEM_MINUS || wp == VK_SUBTRACT)) {
            resizeDock(dockZoom + (wp == VK_OEM_PLUS || wp == VK_ADD ? 5 : -5));
            return 0;
        }
        if (wp == VK_RIGHT || wp == VK_TAB)
            focusIndex = (focusIndex + 1) % itemCount();
        else if (wp == VK_LEFT)
            focusIndex = (focusIndex + itemCount() - 1) % itemCount();
        else if (wp == VK_HOME)
            focusIndex = 0;
        else if (wp == VK_END)
            focusIndex = itemCount() - 1;
        else if (wp == VK_RETURN || wp == VK_SPACE)
            launch(focusIndex);
        else if (wp == VK_ESCAPE)
            restoreShell();
        else if (wp == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000))
            SendMessageW(h, WM_CONTEXTMENU, (WPARAM)h, -1);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp);
            int steps = wheelRemainder / WHEEL_DELTA;
            wheelRemainder %= WHEEL_DELTA;
            if (steps)
                resizeDock(dockZoom + steps * 5);
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        for (UINT i = 0; i < count; i++) {
            wchar_t p[32768]{};
            DragQueryFileW(drop, i, p, 32768);
            addPath(p);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_HOTKEY:
        if (wp == 1)
            restoreShell();
        return 0;
    case WM_TOGGLE:
        toggleClean();
        return 0;
    case WM_RESTORE:
        if (restoreEvent)
            SetEvent(restoreEvent);
        restoreShell();
        updateFullscreenVisibility();
        return 0;
    case WM_SHELLCHECK:
        refreshShell();
        return 0;
    case WM_FOREGROUND:
        updateFullscreenVisibility();
        // Win+D may hide tool windows after delivering the desktop foreground event.
        // Reconcile once after that shell transition; this is not an idle poll.
        SetTimer(h, 3, 250, nullptr);
        return 0;
    case WM_DPICHANGED:
        dpi = HIWORD(wp);
        recreateFont();
        positionDock();
        return 0;
    case WM_DWMCOMPOSITIONCHANGED:
    case WM_THEMECHANGED:
        applyAppearance();
        positionDock();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        applyAppearance();
        positionDock();
        updateFullscreenVisibility();
        return 0;
    case WM_TIMER:
        if (wp == 1)
            DestroyWindow(h);
        else if (wp == 2)
            animateIcons();
        else if (wp == 3) {
            KillTimer(h, 3);
            updateFullscreenVisibility();
        }
        else if (wp == 4)
            refreshRunning();
        else if (wp == 5) {
            KillTimer(h, 5);
            refreshRunning();
            // Window-close notifications can arrive before the drag return
            // animation ends. Retry promptly once list mutation is safe.
            if (dragIndex >= 0 || dragReturning || menuOpen) SetTimer(h, 5, 100, nullptr);
        }
        else if (wp == 6)
            retryWindowActivation();
        return 0;
    case WM_APP + 5:
        SetTimer(h, 5, 100, nullptr);
        return 0;
    case WM_APP + 6:
        refreshRunning();
        return 0;
    case WM_QUERYENDSESSION:
        restoreShell();
        return TRUE;
    case WM_ENDSESSION:
        if (wp)
            restoreShell();
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        cancelWindowActivation();
        saveConfig();
        shutting = true;
        if (dragGhost) { DestroyWindow(dragGhost); dragGhost = nullptr; }
        if (shadow) {
            DestroyWindow(shadow);
            shadow = nullptr;
        }
        restoreShell();
        UnregisterHotKey(h, 1);
        if (foregroundHook)
            UnhookWinEvent(foregroundHook);
        if (objectHook)
            UnhookWinEvent(objectHook);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void appendWindowDiagnostic(wstring &text, HWND w) {
    if (!w)
        return;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    text += L"window hwnd=" + std::to_wstring((uintptr_t)w) + L" pid=" + std::to_wstring(pid) +
            L" class=" + classOf(w) + L" visible=" + std::to_wstring(IsWindowVisible(w) != FALSE) +
            L"\r\n";
}
BOOL CALLBACK diagnosticEnum(HWND w, LPARAM param) {
    auto &text = *(wstring *)param;
    wstring cls = classOf(w);
    if (cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd")
        appendWindowDiagnostic(text, w);
    if (cls == L"Progman" || cls == L"WorkerW") {
        HWND def = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr);
        if (def)
            appendWindowDiagnostic(text, FindWindowExW(def, nullptr, L"SysListView32", nullptr));
    }
    return TRUE;
}
int writeDiagnostic(const wstring &path, bool selfTest) {
    wstring text = L"QuietDock diagnostic (read-only shell inspection)\r\n";
    text += L"pointer_bits=" + std::to_wstring(sizeof(void *) * 8) + L"\r\ndata_dir=" + dataDir +
            L"\r\n";
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\QuietDock.Native.SingleInstance.v1");
    DWORD mutexError = mutex ? 0 : GetLastError();
    text += L"mutex_open=" + std::to_wstring(mutex != nullptr) + L"\r\nmutex_error=" +
            std::to_wstring(mutexError) + L"\r\n";
    if (mutex)
        CloseHandle(mutex);
    HWND current = FindWindowW(CLASS_NAME, nullptr);
    text += L"dock_running=" + std::to_wstring(current != nullptr) + L"\r\n";
    text += L"dock_visible=" + std::to_wstring(current && IsWindowVisible(current)) + L"\r\n";
    text += L"clean=" + std::to_wstring(current && GetPropW(current, L"QuietDock.Clean")) + L"\r\n";
    text += L"glass=" + std::to_wstring(current && GetPropW(current, L"QuietDock.Glass")) + L"\r\n";
    text += L"scale=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.Scale") : dockZoom) + L"\r\n";
    text += L"loaded_items=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.Items") : 0) + L"\r\n";
    text += L"recent_enabled=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.RecentEnabled") : 0) + L"\r\n";
    text += L"recent_count=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.RecentCount") : 0) + L"\r\n";
    text += L"auxiliary_count=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.AuxiliaryCount") : 0) + L"\r\n";
    text += L"activation_target=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.ActivationTarget") : 0) + L"\r\n";
    text += L"activation_succeeded=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.ActivationSucceeded") : 0) + L"\r\n";
    text += L"pinned_items=" + std::to_wstring(current ? (uintptr_t)GetPropW(current, L"QuietDock.Pinned") : 0) + L"\r\n";
    text += L"can_clean=" + std::to_wstring(current && GetPropW(current, L"QuietDock.CanClean")) +
            L"\r\n";
    appendWindowDiagnostic(text, current);
    EnumWindows(diagnosticEnum, (LPARAM)&text);
    bool passed = true;
    if (selfTest) {
        // Pure geometry and input parsing checks; never hide or show a window.
        RECT r = itemRect(0);
        passed = hitTest({r.left + 1, r.top + 1}) == 0 && hitTest({-1, -1}) == -1;
        passed = passed && labelFor(L"C:\\Apps\\sample.lnk") == L"sample";
        passed = passed && readSession(dataDir + L"\\nonexistent-self-test-file").empty();
        passed = passed && !explorerShellTarget(GetDesktopWindow());
        text += passed ? L"self_test=PASS (geometry, labels, missing records, "
                         L"shell target rejection)\r\n"
                       : L"self_test=FAIL\r\n";
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return 4;
    DWORD n = 0;
    WORD bom = 0xfeff;
    bool ok = WriteFile(f, &bom, 2, &n, nullptr) &&
              WriteFile(f, text.data(), (DWORD)text.size() * 2, &n, nullptr);
    CloseHandle(f);
    return !ok ? 4 : passed ? 0 : 5;
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    instance = hi;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wstring overrideDir = L" ";
    for (int i = 1; i + 1 < argc; i++)
        if (wstring(argv[i]) == L"--data-dir")
            overrideDir = argv[i + 1];
    initPaths(overrideDir);
    wstring mode = argc > 1 ? argv[1] : L"";
    if (mode != L"--diagnose" && mode != L"--self-test")
        startupLog(L"start mode=" + mode);
    if (mode == L"--watchdog") {
        DWORD pid = argc > 2 ? wcstoul(argv[2], nullptr, 10) : 0;
        LocalFree(argv);
        return watchdog(pid);
    }
    if (mode == L"--toggle") {
        if (HWND w = FindWindowW(CLASS_NAME, nullptr))
            SendMessageTimeoutW(w, WM_TOGGLE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        LocalFree(argv);
        return 0;
    }
    if (mode == L"--restore") {
        HANDLE event =
            OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\QuietDock.RestoreRequested.v1");
        if (event) {
            SetEvent(event);
            CloseHandle(event);
        }
        HWND w = FindWindowW(CLASS_NAME, nullptr);
        if (w)
            SendMessageTimeoutW(w, WM_RESTORE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        restoreAllFiles();
        LocalFree(argv);
        return 0;
    }
    if (mode == L"--exit") {
        if (HWND w = FindWindowW(CLASS_NAME, nullptr))
            PostMessageW(w, WM_CLOSE, 0, 0);
        LocalFree(argv);
        return 0;
    }
    if (mode == L"--diagnose" || mode == L"--self-test") {
        wstring out =
            (argc > 2 && wcsncmp(argv[2], L"--", 2) != 0) ? argv[2] : dataDir + L"\\diagnose.txt";
        int result = writeDiagnostic(out, mode == L"--self-test");
        LocalFree(argv);
        return result;
    }
    singleton = CreateMutexW(nullptr, TRUE, L"Local\\QuietDock.Native.SingleInstance.v1");
    DWORD mutexError = GetLastError();
    if (!singleton) {
        startupLog(L"CreateMutex failed error=" + std::to_wstring(mutexError));
        MessageBoxW(nullptr, L"无法取得单实例锁，程序未隐藏桌面。请查看数据目录中的 startup.log。",
                    L"QuietDock", MB_ICONERROR);
        LocalFree(argv);
        return 6;
    }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(CLASS_NAME, nullptr);
        startupLog(L"existing instance hwnd=" + std::to_wstring((uintptr_t)w));
        if (w) {
            ShowWindow(w, SW_SHOWNORMAL);
            SetForegroundWindow(w);
        }
        CloseHandle(singleton);
        LocalFree(argv);
        return w ? 0 : 7;
    }
    startupLog(L"single instance acquired");
    restoreAllFiles();
    restoreEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\QuietDock.RestoreRequested.v1");
    if (!restoreEvent)
        return 1;
    ResetEvent(restoreEvent);
    Gdiplus::GdiplusStartupInput gi;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gi, nullptr);
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&ic);
    loadEntries();
    saveConfig(); // Migrate existing pins to independent verified user settings immediately.
    for (int i = 1; i < argc; i++)
        if (wstring(argv[i]) == L"--inspect-ui")
            inspectUi = true;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    DWORD dockStyle =
        (inspectUi ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) : WS_POPUP) |
        WS_CLIPCHILDREN;
    DWORD dockExStyle = (inspectUi ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW);
    dock = CreateWindowExW(dockExStyle, CLASS_NAME, L"QuietDock", dockStyle, 0, 0, 300, 100,
                           nullptr, nullptr, hi, nullptr);
    if (!dock) {
        startupLog(L"CreateWindowEx failed error=" + std::to_wstring(GetLastError()));
        return 1;
    }
    startupLog(L"window created hwnd=" + std::to_wstring((uintptr_t)dock));
    WNDCLASSW shadowClass{};
    shadowClass.lpfnWndProc = shadowProc;
    shadowClass.hInstance = hi;
    shadowClass.lpszClassName = L"QuietDock.Shadow.v1";
    RegisterClassW(&shadowClass);
    shadow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        shadowClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hi, nullptr);
    applyAppearance();
    positionDock();
    updateFullscreenVisibility();
    bool hotkey = RegisterHotKey(dock, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12);
    watchdogReady = startWatchdog();
    canClean = hotkey && watchdogReady;
    startupLog(L"hotkey=" + std::to_wstring(hotkey) + L" watchdog=" +
               std::to_wstring(watchdogReady));
    if (!canClean)
        MessageBoxW(dock,
                    L"恢复热键或守护进程未能启动，净桌面模式已保持关闭。请退出占用 "
                    L"Ctrl+Alt+F12 的程序后重试。",
                    L"QuietDock 安全检查", MB_ICONWARNING);
    foregroundHook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, winEvent, 0, 0,
                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    objectHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                 winEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetPropW(dock, L"QuietDock.CanClean", (HANDLE)(uintptr_t)canClean);
    if (canClean && mode != L"--no-clean") {
        clean = true;
        SetPropW(dock, L"QuietDock.Clean", (HANDLE)1);
        refreshShell();
    }
    for (int i = 1; i + 1 < argc; i++)
        if (wstring(argv[i]) == L"--exit-after") {
            UINT ms = wcstoul(argv[i + 1], nullptr, 10);
            if (ms >= 100)
                SetTimer(dock, 1, ms, nullptr);
        }
    LocalFree(argv);
    updateFullscreenVisibility();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (auto &e : entries) {
        delete e.image;
        if (e.icon)
            DestroyIcon(e.icon);
    }
    for (const auto &app : trackedProcesses)
        for (const auto &process : app.second) CloseHandle(process.handle);
    if (font)
        DeleteObject(font);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CloseHandle(restoreEvent);
    ReleaseMutex(singleton);
    CloseHandle(singleton);
    CoUninitialize();
    return 0;
}
