#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include "avatar.h"
#include "microphone.h"

using namespace Gdiplus;

namespace
{
const wchar_t kWindowClass[] = L"BoykisserAvatarWindow";
const wchar_t kWindowTitle[] = L"Boykisser Avatar";
const UINT kRenderTimer = 1;
const UINT kTrayCallback = WM_APP + 1;
const UINT kShowMessage = WM_APP + 2;
const UINT kTrayId = 1;

const int kMenuShowHide = 1001;
const int kMenuInteractive = 1003;
const int kMenuMute = 1004;
const int kMenuQuit = 1005;

const int kHotkeyInteractive = 3;
const int kHotkeyMute = 4;

HINSTANCE g_instance = NULL;
HWND g_window = NULL;
Avatar* g_avatar = NULL;
Microphone g_microphone;
ULONG_PTR g_gdiplusToken = 0;

HDC g_memoryDc = NULL;
HBITMAP g_bitmap = NULL;
HGDIOBJ g_oldBitmap = NULL;
void* g_bits = NULL;
Bitmap* g_surface = NULL;
int g_width = 600;
int g_height = 780;

HICON g_trayIcon = NULL;
HICON g_windowIcon = NULL;
NOTIFYICONDATAW g_trayData = {};
bool g_visible = true;
bool g_interactive = false;
bool g_muted = false;
double g_lastFrameMs = 0.0;

HANDLE g_showEvent = NULL;
HANDLE g_exitEvent = NULL;
HANDLE g_mutex = NULL;
std::thread g_showThread;

double NowMilliseconds()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::wstring ExecutableDirectory()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (length == 0)
    {
        return L".";
    }

    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

void EnableDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != NULL)
    {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(HANDLE);
        SetProcessDpiAwarenessContextProc setContext = reinterpret_cast<SetProcessDpiAwarenessContextProc>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext != NULL && setContext(reinterpret_cast<HANDLE>(-4)))
        {
            return;
        }
    }

    SetProcessDPIAware();
}

void SetTrayTip()
{
    if (g_trayIcon == NULL)
    {
        return;
    }

    const wchar_t* tip = g_visible ? L"Boykisser Avatar" : L"Boykisser Avatar (hidden)";
    lstrcpynW(g_trayData.szTip, tip, ARRAYSIZE(g_trayData.szTip));
    g_trayData.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_trayData);
}

bool CreateSurface(int width, int height)
{
    HDC screen = GetDC(NULL);
    g_memoryDc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);

    if (g_memoryDc == NULL)
    {
        return false;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    g_bitmap = CreateDIBSection(g_memoryDc, &info, DIB_RGB_COLORS, &g_bits, NULL, 0);
    if (g_bitmap == NULL || g_bits == NULL)
    {
        return false;
    }

    g_oldBitmap = SelectObject(g_memoryDc, g_bitmap);
    g_surface = new Bitmap(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(g_bits));
    if (g_surface->GetLastStatus() != Ok)
    {
        return false;
    }

    g_width = width;
    g_height = height;
    return true;
}

void DestroySurface()
{
    delete g_surface;
    g_surface = NULL;

    if (g_memoryDc != NULL && g_oldBitmap != NULL)
    {
        SelectObject(g_memoryDc, g_oldBitmap);
        g_oldBitmap = NULL;
    }

    if (g_bitmap != NULL)
    {
        DeleteObject(g_bitmap);
        g_bitmap = NULL;
    }

    if (g_memoryDc != NULL)
    {
        DeleteDC(g_memoryDc);
        g_memoryDc = NULL;
    }

    g_bits = NULL;
}

void RenderFrame()
{
    if (g_surface == NULL || g_window == NULL || g_memoryDc == NULL)
    {
        return;
    }

    {
        Graphics graphics(g_surface);
        graphics.Clear(Color(0, 0, 0, 0));
        if (g_avatar != NULL)
        {
            g_avatar->Draw(graphics, NowMilliseconds());
        }
    }

    RECT windowRect;
    GetWindowRect(g_window, &windowRect);

    POINT destination = { windowRect.left, windowRect.top };
    SIZE size = { g_width, g_height };
    POINT source = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    HDC screen = GetDC(NULL);
    UpdateLayeredWindow(g_window, screen, &destination, &size, g_memoryDc, &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

void SetVisible(bool visible)
{
    if (g_window == NULL)
    {
        return;
    }

    g_visible = visible;

    if (visible)
    {
        ShowWindow(g_window, SW_SHOWNOACTIVATE);
        g_lastFrameMs = NowMilliseconds();
        SetTimer(g_window, kRenderTimer, 16, NULL);
        RenderFrame();
    }
    else
    {
        KillTimer(g_window, kRenderTimer);
        ShowWindow(g_window, SW_HIDE);
    }

    SetTrayTip();
}

void SetInteractive(bool interactive)
{
    g_interactive = interactive;
    if (g_window == NULL)
    {
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(g_window, GWL_EXSTYLE);
    if (interactive)
    {
        style &= ~WS_EX_TRANSPARENT;
        SetVisible(true);
        SetForegroundWindow(g_window);
        SetFocus(g_window);
        SetCursor(LoadCursorW(NULL, IDC_SIZEALL));
    }
    else
    {
        style |= WS_EX_TRANSPARENT;
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
    }

    SetWindowLongPtrW(g_window, GWL_EXSTYLE, style);
    SetWindowPos(
        g_window,
        NULL,
        0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetMuted(bool muted)
{
    g_muted = muted;
    g_microphone.SetMuted(muted);
}

void ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, kMenuShowHide, g_visible ? L"Hide avatar" : L"Show avatar");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_interactive ? MF_CHECKED : MF_UNCHECKED), kMenuInteractive, L"Move mode");
    AppendMenuW(menu, MF_STRING | (g_muted ? MF_CHECKED : MF_UNCHECKED), kMenuMute, L"Mute microphone");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);

    const int command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        0,
        g_window,
        NULL);

    DestroyMenu(menu);

    if (command != 0)
    {
        PostMessageW(g_window, WM_COMMAND, static_cast<WPARAM>(command), 0);
    }
}

void CreateTray()
{
    g_trayIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(1));
    if (g_trayIcon == NULL)
    {
        g_trayIcon = LoadIconW(NULL, IDI_APPLICATION);
    }

    ZeroMemory(&g_trayData, sizeof(g_trayData));
    g_trayData.cbSize = sizeof(g_trayData);
    g_trayData.hWnd = g_window;
    g_trayData.uID = kTrayId;
    g_trayData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_trayData.uCallbackMessage = kTrayCallback;
    g_trayData.hIcon = g_trayIcon;
    lstrcpynW(g_trayData.szTip, kWindowTitle, ARRAYSIZE(g_trayData.szTip));

    Shell_NotifyIconW(NIM_ADD, &g_trayData);
}

void RemoveTray()
{
    if (g_trayIcon != NULL)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_trayData);
        g_trayIcon = NULL;
    }
}

void RegisterHotKeys()
{
    RegisterHotKey(g_window, kHotkeyInteractive, MOD_CONTROL | MOD_SHIFT, 'B');
    RegisterHotKey(g_window, kHotkeyMute, MOD_CONTROL | MOD_SHIFT, 'M');
}

void UnregisterHotKeys()
{
    UnregisterHotKey(g_window, kHotkeyInteractive);
    UnregisterHotKey(g_window, kHotkeyMute);
}

void UpdateFrame()
{
    if (g_avatar == NULL)
    {
        return;
    }

    const double now = NowMilliseconds();
    double delta = (now - g_lastFrameMs) / 1000.0;
    g_lastFrameMs = now;

    if (delta < 0.001)
    {
        delta = 0.001;
    }
    if (delta > 0.04)
    {
        delta = 0.04;
    }

    g_avatar->Update(g_microphone.Level(), g_muted, now, delta);
    RenderFrame();
}

void Cleanup()
{
    KillTimer(g_window, kRenderTimer);
    UnregisterHotKeys();
    RemoveTray();
    g_microphone.Stop();

    delete g_avatar;
    g_avatar = NULL;
    DestroySurface();

    if (g_showThread.joinable())
    {
        SetEvent(g_exitEvent);
        g_showThread.join();
    }

    if (g_showEvent != NULL)
    {
        CloseHandle(g_showEvent);
        g_showEvent = NULL;
    }
    if (g_exitEvent != NULL)
    {
        CloseHandle(g_exitEvent);
        g_exitEvent = NULL;
    }
    if (g_mutex != NULL)
    {
        CloseHandle(g_mutex);
        g_mutex = NULL;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
        if (wParam == kRenderTimer)
        {
            UpdateFrame();
        }
        return 0;

    case WM_HOTKEY:
        switch (wParam)
        {
        case kHotkeyInteractive:
            SetInteractive(!g_interactive);
            break;
        case kHotkeyMute:
            SetMuted(!g_muted);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kMenuShowHide:
            SetVisible(!g_visible);
            break;
        case kMenuInteractive:
            SetInteractive(!g_interactive);
            break;
        case kMenuMute:
            SetMuted(!g_muted);
            break;
        case kMenuQuit:
            PostMessageW(g_window, WM_CLOSE, 0, 0);
            break;
        }
        return 0;

    case kTrayCallback:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
        {
            ShowTrayMenu();
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            SetVisible(true);
            SetForegroundWindow(g_window);
        }
        return 0;

    case kShowMessage:
        SetVisible(true);
        SetForegroundWindow(g_window);
        return 0;

    case WM_LBUTTONDOWN:
        if (g_interactive)
        {
            ReleaseCapture();
            SendMessageW(g_window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursorW(NULL, g_interactive ? IDC_SIZEALL : IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        Cleanup();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    EnableDpiAwareness();
    timeBeginPeriod(1);

    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL) != Ok)
    {
        return 1;
    }

    g_showEvent = CreateEventW(NULL, FALSE, FALSE, L"Local\\BoykisserAvatar.Show");
    g_mutex = CreateMutexW(NULL, FALSE, L"Local\\BoykisserAvatar.Mutex");
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;

    if (alreadyRunning)
    {
        if (g_showEvent != NULL)
        {
            SetEvent(g_showEvent);
            CloseHandle(g_showEvent);
        }
        if (g_mutex != NULL)
        {
            CloseHandle(g_mutex);
        }
        GdiplusShutdown(g_gdiplusToken);
        timeEndPeriod(1);
        return 0;
    }

    g_exitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    g_instance = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

    const int workWidth = workArea.right - workArea.left;
    const int workHeight = workArea.bottom - workArea.top;
    g_height = std::min(620, std::max(360, static_cast<int>(workHeight * 0.60)));
    g_width = static_cast<int>(g_height * 600.0 / 780.0);

    const int x = workArea.left + (workWidth - g_width) / 2;
    int y = workArea.top + (workHeight - g_height) / 2;
    if (y + g_height > workArea.bottom - 8)
    {
        y = workArea.bottom - g_height - 8;
    }

    g_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_APPWINDOW,
        kWindowClass,
        kWindowTitle,
        WS_POPUP,
        x, y, g_width, g_height,
        NULL,
        NULL,
        instance,
        NULL);

    if (g_window == NULL)
    {
        GdiplusShutdown(g_gdiplusToken);
        timeEndPeriod(1);
        return 1;
    }

    g_windowIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (g_windowIcon == NULL)
    {
        g_windowIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    SendMessageW(g_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_windowIcon));
    SendMessageW(g_window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_windowIcon));

    if (!CreateSurface(g_width, g_height))
    {
        DestroyWindow(g_window);
        GdiplusShutdown(g_gdiplusToken);
        timeEndPeriod(1);
        return 1;
    }

    const std::wstring directory = ExecutableDirectory();
    g_avatar = new Avatar(directory, g_width, g_height);

    CreateTray();
    RegisterHotKeys();
    g_microphone.Start();

    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    SetVisible(true);

    g_showThread = std::thread([]() {
        HANDLE handles[2] = { g_showEvent, g_exitEvent };
        while (true)
        {
            const DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0)
            {
                if (g_window != NULL)
                {
                    PostMessageW(g_window, kShowMessage, 0, 0);
                }
            }
            else
            {
                break;
            }
        }
    });

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    timeEndPeriod(1);
    GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(message.wParam);
}
