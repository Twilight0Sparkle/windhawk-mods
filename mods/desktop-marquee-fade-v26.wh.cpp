// ==WindhawkMod==
// @id              desktop-marquee-fade-v26
// @name            Desktop Marquee Fade (Async)
// @description     Add a smooth, modern fade effect to your desktop selection
// @version         26.0.4
// @author          angel_lov
// @github          https://github.com/Twilight0Sparkle
// @include         explorer.exe
// @compilerOptions -luser32 -lgdi32 -lcomctl32 -ladvapi32
// @license         MIT
// ==/WindhawkMod==
 
// ==WindhawkModReadme==
/*
 
# Desktop Marquee Fade
 
This mod adds a smooth fade animation to the desktop selection rectangle.
Instead of disappearing instantly when you release the mouse button, the 
selection area dissolves smoothly, making the Windows interface look more
like MacOS.
 
---
 
## Key Features
 
* **Smooth Fade Effect**: The selection rectangle fades out gracefully.
* **Highly Customizable**: Adjust animation speed and duration via mod settings.
* **Smart Opacity**: The mod automatically calculates opacity for a natural look.
* **Smart Interrupt**: If you open windows or interact with the desktop while 
  the animation is playing, the mod instantly speeds up (4x) to avoid 
  interfering with your workflow.
* **Single Instance Mode**: Prevents overlapping animations by ensuring only 
  one selection rectangle is active at a time.
* **Live System Color Customization:** Allows you to override default Windows selection colors (Fill/Border) with custom HEX values. Changes are applied instantly in memory without requiring a system reboot or Explorer restart.
* **Robust Persistence:** Automatically backs up your original Windows registry selection colors on first launch and safely restores them when the mod is disabled or uninstalled.
 
## How to use
1. Click "Create a New Mod" button.
2. Paste mod's code directly into Code Editor.
3. Apply the settings (click "Compile Mod" button).
4. Try selecting an area on your desktop (as if selecting files).
5. After releasing the mouse button, you will see the rectangle fade out smoothly.
 
## Configuration Settings
 
* **Animation Speed (%):** Speed multiplier for the fade effect (adjustable from 50% to 500%).
* **Animation Duration (ms):** Total lifespan of the fade effect in milliseconds (100ms to 2000ms).
* **Auto-calculate Opacity:** When enabled, automatically scales the initial visibility threshold of the marquee to match your speed and duration settings.
* **Manual Initial Opacity:** Custom starting alpha value (1 to 255) if auto-calculation is turned off.
* **Accelerate if Covered:** Automatically speeds up the animation fade rate if another window gains focus, eliminating visual clutter.
* **Single Instance Only:** Limits rendering to only one active fading marquee at any given time.
* **Use Custom Colors:** Toggles the override of native Windows selection aesthetics.
* **Custom Fill/Border Color:** Set your preferred hex colors.
 
*/
// ==/WindhawkModReadme==
 
// ==WindhawkModSettings==
/*
- speed: 100
  $name: Animation Speed (%)
  $description: Animation speed in percent (50-500). Default is 100.
- duration: 300
  $name: Animation Duration (ms)
  $description: Animation duration in milliseconds (100-2000). Default is 300.
- initial_alpha: 55
  $name: Manual Initial Opacity
  $description: Starting opacity (1-255). Only used if Auto-calculate Opacity is disabled. Default is 55.
- auto_calc_opacity: true
  $name: Auto-calculate Opacity
  $description: Automatically sets opacity based on speed/duration. Toggled by default.
- use_defaults: true
  $name: Accelerate if covered
  $description: If ON, animations speed up 4x if covered (or focused) by windows. Toggled by default.
- single_instance: false
  $name: Single Instance Only
  $description: If ON, only one rectangle can be active at a time. Turned off by default.
- use_custom_color: false
  $name: Use Custom Colors
  $description: Turn ON to override Windows system colors instantly. Turned off by default.
- custom_fill_color: "8C8C8C"
  $name: Custom Fill Color (HEX)
  $description: Fill color (e.g., 783838).
- custom_border_color: "B4B4B4"
  $name: Custom Border Color (HEX)
  $description: Border color (e.g., FF8F8F).
*/
// ==/WindhawkModSettings==
 
#include <map>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <stdio.h>
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <windhawk_api.h>
 
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
 
struct SavedColors {
    COLORREF hilight;
    COLORREF hotTracking;
};
 
struct AnimPayload {
    RECT rc;
};
 
COLORREF g_defaultHilight = 0;
COLORREF g_defaultHotTracking = 0;
 
std::atomic<int> g_settingSpeed{100};
std::atomic<int> g_settingDuration{300};
std::atomic<int> g_settingInitialAlpha{70};
std::atomic<bool> g_settingAutoCalcAlpha{true};
std::atomic<bool> g_settingUseDefaults{true};
std::atomic<bool> g_settingSingleInstance{false};
std::atomic<bool> g_settingUseCustomColor{false};
std::atomic<DWORD> g_settingCustomFillColor{0x0078D7};
std::atomic<DWORD> g_settingCustomBorderColor{0x005A9E};
 
HWND g_hManagerWnd = NULL;
HHOOK g_hMouseHook = NULL;
HANDLE g_hThread = NULL;
DWORD g_dwThreadId = 0;
 
POINT g_dragStart = { 0 };
bool g_isDragging = false;
 
std::atomic<bool> g_isStartPointValid{false};
HWND g_hCachedDesktopTopLevel = NULL;
HWND g_hCachedSysListView32 = NULL;
 
#define TIMER_ID_FADE 1
#define WM_START_ANIMATION (WM_USER + 1)
#define WM_PRECHECK_START (WM_USER + 2)
 
struct AnimState {
    float borderAlpha;   
    float decrement;     
    int targetFillAlpha; 
    int w;
    int h;
};
std::map<HWND, AnimState> g_animations;
 
void ApplySystemColors(COLORREF hilight, COLORREF hotTracking) {
    int elements[2] = { COLOR_HIGHLIGHT, COLOR_HOTLIGHT };
    COLORREF colors[2] = { hilight, hotTracking };
    SetSysColors(2, elements, colors);
 
    auto applyColor = [&](const wchar_t* val, COLORREF color) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d %d %d", GetRValue(color), GetGValue(color), GetBValue(color));
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, val, 0, REG_SZ, (BYTE*)buf, (wcslen(buf) + 1) * sizeof(wchar_t));
            RegCloseKey(hKey);
        }
    };
    applyColor(L"Hilight", hilight);
    applyColor(L"HotTrackingColor", hotTracking);
}
 
void InitPersistentDefaults() {
    wchar_t path[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH)) {
        wcscat_s(path, L"\\Windhawk_MarqueeFade_Defaults.bin");
        FILE* f = _wfopen(path, L"rb");
        if (f) {
            SavedColors saved;
            if (fread(&saved, sizeof(SavedColors), 1, f) == 1) {
                g_defaultHilight = saved.hilight;
                g_defaultHotTracking = saved.hotTracking;
            }
            fclose(f);
        } else {
            g_defaultHilight = GetSysColor(COLOR_HIGHLIGHT);
            g_defaultHotTracking = GetSysColor(COLOR_HOTLIGHT);
            f = _wfopen(path, L"wb");
            if (f) {
                SavedColors saved = { g_defaultHilight, g_defaultHotTracking };
                fwrite(&saved, sizeof(SavedColors), 1, f);
                fclose(f);
            }
        }
    } else {
        g_defaultHilight = GetSysColor(COLOR_HIGHLIGHT);
        g_defaultHotTracking = GetSysColor(COLOR_HOTLIGHT);
    }
}
 
DWORD ParseHexColor(PCWSTR hexStr) {
    if (!hexStr) return 0;
    PCWSTR p = hexStr;
    while (*p == L' ' || *p == L'\t') p++;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) p += 2;
    else if (p[0] == L'#') p += 1;
    return wcstoul(p, nullptr, 16);
}
 
COLORREF GetTargetFillColor() {
    if (g_settingUseCustomColor.load()) {
        DWORD hex = g_settingCustomFillColor.load();
        return RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
    }
    return g_defaultHilight; 
}
 
COLORREF GetTargetBorderColor() {
    if (g_settingUseCustomColor.load()) {
        DWORD hex = g_settingCustomBorderColor.load();
        return RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
    }
    return g_defaultHotTracking;
}
 
int CalculateSmartAlpha(int speed, int duration) {
    double baseAlpha = 70.0; 
    double speedFactor = 0.15 * (speed - 100);
    double durationFactor = -0.05 * (duration - 300);
    double calculated = baseAlpha + speedFactor + durationFactor;
    return std::max(35, std::min(130, (int)lround(calculated)));
}
 
void LoadSettings() {
    g_settingSpeed.store(std::max(50, std::min(500, Wh_GetIntSetting(L"speed"))));
    g_settingDuration.store(std::max(100, std::min(2000, Wh_GetIntSetting(L"duration"))));
    g_settingInitialAlpha.store(std::max(1, std::min(255, Wh_GetIntSetting(L"initial_alpha"))));
    g_settingAutoCalcAlpha.store(Wh_GetIntSetting(L"auto_calc_opacity") != 0);
    g_settingUseDefaults.store(Wh_GetIntSetting(L"use_defaults") != 0);
    g_settingSingleInstance.store(Wh_GetIntSetting(L"single_instance") != 0);
    g_settingUseCustomColor.store(Wh_GetIntSetting(L"use_custom_color") != 0);
 
    PCWSTR fillStr = Wh_GetStringSetting(L"custom_fill_color");
    g_settingCustomFillColor.store(ParseHexColor(fillStr ? fillStr : L"0x0078D7"));
    if (fillStr) Wh_FreeStringSetting(fillStr);
 
    PCWSTR borderStr = Wh_GetStringSetting(L"custom_border_color");
    g_settingCustomBorderColor.store(ParseHexColor(borderStr ? borderStr : L"0x005A9E"));
    if (borderStr) Wh_FreeStringSetting(borderStr);
}
 
void PaintMarqueeDIB(HWND hwnd, int width, int height, int alphaBorder, int alphaFill) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
 
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; 
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
 
    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
 
    if (pBits) {
        COLORREF fillCol = GetTargetFillColor();
        COLORREF borderCol = GetTargetBorderColor();
 
        BYTE fillR = GetRValue(fillCol);
        BYTE fillG = GetGValue(fillCol);
        BYTE fillB = GetBValue(fillCol);
 
        BYTE borderR = GetRValue(borderCol);
        BYTE borderG = GetGValue(borderCol);
        BYTE borderB = GetBValue(borderCol);
 
        DWORD* pixels = (DWORD*)pBits;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                bool isBorder = (x == 0 || x == width - 1 || y == 0 || y == height - 1);
                if (isBorder) {
                    BYTE r = (borderR * alphaBorder) / 255;
                    BYTE g = (borderG * alphaBorder) / 255;
                    BYTE b = (borderB * alphaBorder) / 255;
                    pixels[y * width + x] = (alphaBorder << 24) | (r << 16) | (g << 8) | b;
                } else {
                    BYTE r = (fillR * alphaFill) / 255;
                    BYTE g = (fillG * alphaFill) / 255;
                    BYTE b = (fillB * alphaFill) / 255;
                    pixels[y * width + x] = (alphaFill << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
 
        POINT ptSrc = {0, 0};
        SIZE sizeWnd = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, hdcScreen, NULL, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    }
 
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}
 
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER: {
            if (wParam == TIMER_ID_FADE) {
                auto it = g_animations.find(hwnd);
                if (it != g_animations.end()) {
                    AnimState& st = it->second;
                    if (g_settingUseDefaults.load()) {
                        HWND fg = GetForegroundWindow();
                        if (fg) {
                            HWND hFgRoot = GetAncestor(fg, GA_ROOT);
                            wchar_t szClass[64];
                            if (GetClassNameW(hFgRoot, szClass, 64)) {
                                if (wcscmp(szClass, L"Progman") != 0 && wcscmp(szClass, L"WorkerW") != 0) {
                                    float fastDec = 255.0f / 5.0f; 
                                    if (st.decrement < fastDec) st.decrement = fastDec; 
                                }
                            }
                        }
                    }
 
                    st.borderAlpha -= st.decrement;
                    if (st.borderAlpha <= 0.0f) {
                        KillTimer(hwnd, TIMER_ID_FADE);
                        g_animations.erase(it);
                        DestroyWindow(hwnd);
                    } else {
                        int currentFillAlpha = (int)(st.borderAlpha * (double)st.targetFillAlpha / 255.0);
                        PaintMarqueeDIB(hwnd, st.w, st.h, (int)st.borderAlpha, currentFillAlpha);
                    }
                }
                return 0;
            }
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
 
HWND GetRealDesktopListView(HWND* outDesktopTopLevel) {
    HWND hProgman = FindWindowW(L"Progman", L"Program Manager");
    HWND hDesktopDefView = FindWindowExW(hProgman, NULL, L"SHELLDLL_DefView", NULL);
 
    if (hDesktopDefView) {
        if (outDesktopTopLevel) *outDesktopTopLevel = hProgman;
    } else {
        HWND hWorkerW = NULL;
        while ((hWorkerW = FindWindowExW(NULL, hWorkerW, L"WorkerW", NULL)) != NULL) {
            hDesktopDefView = FindWindowExW(hWorkerW, NULL, L"SHELLDLL_DefView", NULL);
            if (hDesktopDefView) {
                if (outDesktopTopLevel) *outDesktopTopLevel = hWorkerW;
                break;
            }
        }
    }
 
    if (!hDesktopDefView) return NULL;
    return FindWindowExW(hDesktopDefView, NULL, L"SysListView32", L"FolderView");
}
 
LRESULT CALLBACK ManagerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PRECHECK_START) {
        POINT* ppt = (POINT*)lParam;
        g_hCachedSysListView32 = GetRealDesktopListView(&g_hCachedDesktopTopLevel);
        bool isValid = false;
 
        if (g_hCachedSysListView32 && g_hCachedDesktopTopLevel) {
            RECT rcDesktop;
            GetWindowRect(g_hCachedSysListView32, &rcDesktop);
            if (PtInRect(&rcDesktop, *ppt)) {
                bool isOnIcon = false;
                DWORD sysListViewPid = 0;
                GetWindowThreadProcessId(g_hCachedSysListView32, &sysListViewPid);
 
                if (sysListViewPid == GetCurrentProcessId()) {
                    LVHITTESTINFO lvhti;
                    ZeroMemory(&lvhti, sizeof(lvhti));
                    lvhti.pt = *ppt;
                    ScreenToClient(g_hCachedSysListView32, &lvhti.pt);
                    DWORD_PTR dwResult = 0;
 
                    if (SendMessageTimeoutW(g_hCachedSysListView32, LVM_HITTEST, 0, (LPARAM)&lvhti, SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &dwResult)) {
                        if ((int)dwResult != -1) isOnIcon = true;
                    }
                }
                if (!isOnIcon) {
                    isValid = true;
                }
            }
        }
        g_isStartPointValid.store(isValid);
        delete ppt;
        return 0;
    }
    else if (msg == WM_START_ANIMATION) {
        AnimPayload* payload = (AnimPayload*)lParam;
        Wh_Log(L"[Marquee] Creating instant animation overlay.");
 
        if (g_settingSingleInstance.load()) {
            for (auto const& pair : g_animations) {
                KillTimer(pair.first, TIMER_ID_FADE);
                DestroyWindow(pair.first);
            }
            g_animations.clear();
        }
 
        HWND hOverlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"FadeMarqueeOverlayClass", NULL, WS_POPUP, 
            payload->rc.left, payload->rc.top, 
            payload->rc.right - payload->rc.left, 
            payload->rc.bottom - payload->rc.top, 
            NULL, NULL, GetModuleHandle(NULL), NULL 
        );
 
        if (hOverlay) {
            int speed = g_settingSpeed.load();
            int duration = g_settingDuration.load();
            int targetFillAlpha = g_settingAutoCalcAlpha.load() ? CalculateSmartAlpha(speed, duration) : g_settingInitialAlpha.load();
 
            int actualDuration = (duration * 100) / speed;
            if (actualDuration < 16) actualDuration = 16;
 
            AnimState st;
            st.borderAlpha = 255.0f;
            st.decrement = 255.0f / ((float)actualDuration / 16.0f);
            st.targetFillAlpha = targetFillAlpha;
            st.w = payload->rc.right - payload->rc.left;
            st.h = payload->rc.bottom - payload->rc.top;
 
            g_animations[hOverlay] = st;
 
            PaintMarqueeDIB(hOverlay, st.w, st.h, 255, targetFillAlpha);
 
            HWND hWindowAboveDesktop = GetWindow(g_hCachedDesktopTopLevel, GW_HWNDPREV);
            if (hWindowAboveDesktop) {
                SetWindowPos(hOverlay, hWindowAboveDesktop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            } else {
                SetWindowPos(hOverlay, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
 
            SetTimer(hOverlay, TIMER_ID_FADE, 16, NULL);
        }
 
        delete payload;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
 
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_LBUTTONDOWN) {
            g_dragStart = pMouse->pt;
            g_isDragging = true;
            g_isStartPointValid.store(false);
 
            POINT* ppt = new POINT{pMouse->pt.x, pMouse->pt.y};
            PostMessageW(g_hManagerWnd, WM_PRECHECK_START, 0, (LPARAM)ppt);
 
        } else if (wParam == WM_LBUTTONUP && g_isDragging) {
            g_isDragging = false;
 
            if (g_isStartPointValid.load()) {
                int w = abs((int)pMouse->pt.x - (int)g_dragStart.x);
                int h = abs((int)pMouse->pt.y - (int)g_dragStart.y);
 
                if (w > 10 && h > 10) {
                    int left = (g_dragStart.x < pMouse->pt.x) ? (int)g_dragStart.x : (int)pMouse->pt.x;
                    int top = (g_dragStart.y < pMouse->pt.y) ? (int)g_dragStart.y : (int)pMouse->pt.y;
 
                    AnimPayload* payload = new AnimPayload;
                    payload->rc = {left, top, left + w, top + h};
 
                    PostMessageW(g_hManagerWnd, WM_START_ANIMATION, 0, (LPARAM)payload);
                }
            }
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}
 
DWORD WINAPI WorkerThreadProc(LPVOID lpParam) {
    WNDCLASSW wcm = {0, ManagerWndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"FadeMarqueeManagerClass"};
    RegisterClassW(&wcm);
    WNDCLASSW wco = {0, OverlayWndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"FadeMarqueeOverlayClass"};
    RegisterClassW(&wco);
    g_hManagerWnd = CreateWindowExW(0, L"FadeMarqueeManagerClass", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
    g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
 
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { 
        DispatchMessage(&msg); 
    }
 
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_START_ANIMATION) {
            if (msg.lParam) delete (AnimPayload*)msg.lParam;
        } else if (msg.message == WM_PRECHECK_START) {
            if (msg.lParam) delete (POINT*)msg.lParam;
        }
    }
 
    for (auto const& pair : g_animations) {
        KillTimer(pair.first, TIMER_ID_FADE);
        DestroyWindow(pair.first);
    }
    g_animations.clear();
 
    if (g_hMouseHook) UnhookWindowsHookEx(g_hMouseHook);
    if (g_hManagerWnd) DestroyWindow(g_hManagerWnd);
 
    UnregisterClassW(L"FadeMarqueeManagerClass", GetModuleHandle(NULL));
    UnregisterClassW(L"FadeMarqueeOverlayClass", GetModuleHandle(NULL));
 
    return 0;
}
 
BOOL Wh_ModInit() {
    InitPersistentDefaults();
    LoadSettings();
    ApplySystemColors(GetTargetBorderColor(), GetTargetFillColor());
 
    g_hThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, &g_dwThreadId);
    return TRUE;
}
 
void Wh_ModSettingsChanged() { 
    LoadSettings(); 
    ApplySystemColors(GetTargetBorderColor(), GetTargetFillColor());
}
 
void Wh_ModUninit() { 
    ApplySystemColors(g_defaultHilight, g_defaultHotTracking);
 
    if (g_dwThreadId) PostThreadMessageW(g_dwThreadId, WM_QUIT, 0, 0);
    if (g_hThread) { 
        WaitForSingleObject(g_hThread, 1000); 
        CloseHandle(g_hThread); 
    }
}