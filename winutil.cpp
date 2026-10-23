#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif

#include "winutil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

namespace {

HWND H(quintptr h) { return reinterpret_cast<HWND>(h); }

QString processExe(DWORD pid)
{
    QString name;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH];
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(p, 0, buf, &len))
            name = QFileInfo(QString::fromWCharArray(buf, int(len))).fileName();
        CloseHandle(p);
    }
    return name;
}

bool tokenElevated(HANDLE process, bool* ok)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        *ok = false;
        return false;
    }
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    *ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) != 0;
    CloseHandle(token);
    return *ok && e.TokenIsElevated;
}

BOOL CALLBACK collect(HWND h, LPARAM param)
{
    auto* out = reinterpret_cast<QVector<WinUtil::WindowInfo>*>(param);
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return TRUE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    DWORD cloaked = 0; // скрытые системой окна (например, приложения на другом рабочем столе)
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    const int len = GetWindowTextLengthW(h);
    if (len <= 0)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;

    std::wstring title(size_t(len) + 1, L'\0');
    GetWindowTextW(h, title.data(), len + 1);

    WinUtil::WindowInfo info;
    info.hwnd = reinterpret_cast<quintptr>(h);
    info.pid = pid;
    info.title = QString::fromWCharArray(title.c_str(), len);
    info.exe = processExe(pid);
    if (info.title == QLatin1String("Program Manager"))
        return TRUE; // рабочий стол
    out->push_back(info);
    return TRUE;
}

} // namespace
#endif

namespace WinUtil {

#ifdef Q_OS_WIN

QVector<WindowInfo> listWindows()
{
    QVector<WindowInfo> out;
    EnumWindows(collect, reinterpret_cast<LPARAM>(&out));
    return out;
}

bool isAlive(quintptr hwnd) { return hwnd && IsWindow(H(hwnd)); }

bool isMinimized(quintptr hwnd) { return hwnd && IsIconic(H(hwnd)); }

quint32 windowPid(quintptr hwnd)
{
    DWORD pid = 0;
    if (hwnd)
        GetWindowThreadProcessId(H(hwnd), &pid);
    return pid;
}

QPoint clientOrigin(quintptr hwnd)
{
    POINT p{0, 0};
    ClientToScreen(H(hwnd), &p);
    return QPoint(p.x, p.y);
}

bool bringToFront(quintptr hwnd)
{
    const HWND h = H(hwnd);
    if (!IsWindow(h))
        return false;
    if (IsIconic(h))
        ShowWindow(h, SW_RESTORE);
    if (GetForegroundWindow() == h)
        return true;

    // Windows не даёт фоновой программе забирать фокус. Обходим это,
    // временно «подключаясь» к потоку активного окна и потоку нужного окна.
    const HWND fg = GetForegroundWindow();
    const DWORD self = GetCurrentThreadId();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD targetThread = GetWindowThreadProcessId(h, nullptr);
    const bool a1 = fgThread && fgThread != self && AttachThreadInput(self, fgThread, TRUE);
    const bool a2 = targetThread && targetThread != self && targetThread != fgThread
                    && AttachThreadInput(self, targetThread, TRUE);

    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetActiveWindow(h);

    if (a2)
        AttachThreadInput(self, targetThread, FALSE);
    if (a1)
        AttachThreadInput(self, fgThread, FALSE);

    if (GetForegroundWindow() != h) {
        // Запасной вариант: нажатие Alt снимает запрет на смену активного окна.
        // Ждём, пока оно дойдёт до старого окна, и только потом переключаемся.
        INPUT in[2]{};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_MENU;
        in[1] = in[0];
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        Sleep(40);
        SetForegroundWindow(h);
    }
    if (GetForegroundWindow() != h)
        SwitchToThisWindow(h, TRUE);
    return GetForegroundWindow() == h;
}

bool isForegroundProcess(quint32 pid)
{
    DWORD fgPid = 0;
    if (HWND fg = GetForegroundWindow())
        GetWindowThreadProcessId(fg, &fgPid);
    return pid && fgPid == pid;
}

bool isForegroundWindow(quintptr hwnd)
{
    const HWND h = H(hwnd);
    const HWND fg = GetForegroundWindow();
    if (!fg || !h)
        return false;
    if (fg == h)
        return true;
    // всплывающие окна и диалоги, которыми владеет это окно
    return GetAncestor(fg, GA_ROOTOWNER) == h || GetWindow(fg, GW_OWNER) == h;
}

bool isProcessElevated(quint32 pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p)
        return true; // даже открыть не дали — значит, процесс с повышенными правами
    bool ok = false;
    const bool elevated = tokenElevated(p, &ok);
    CloseHandle(p);
    return ok ? elevated : true;
}

bool isSelfElevated()
{
    bool ok = false;
    return tokenElevated(GetCurrentProcess(), &ok);
}

bool relaunchAsAdmin()
{
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const auto r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"runas", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return r > 32;
}

#else // не Windows: заглушки, чтобы проект собирался

QVector<WindowInfo> listWindows() { return {}; }
bool isAlive(quintptr) { return false; }
bool isMinimized(quintptr) { return false; }
quint32 windowPid(quintptr) { return 0; }
QPoint clientOrigin(quintptr) { return {}; }
bool bringToFront(quintptr) { return false; }
bool isForegroundProcess(quint32) { return false; }
bool isForegroundWindow(quintptr) { return false; }
bool isProcessElevated(quint32) { return false; }
bool isSelfElevated() { return false; }
bool relaunchAsAdmin() { return false; }

#endif

} // namespace WinUtil
