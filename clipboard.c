#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON     (WM_USER + 1)
#define WM_DOPASTE      (WM_USER + 2)
#define TIMER_CAPTURE   1
#define MAX_HISTORY     50
#define HISTORY_FILE    L"clipboard_history.txt"
#define HOTKEY_ID       1
#define HOTKEY_MODS     (MOD_CONTROL | MOD_SHIFT)
#define HOTKEY_VK       'V'

static wchar_t* history[MAX_HISTORY] = { 0 };
static int      historyCount = 0;
static HWND     hMainWindow = NULL;
static HINSTANCE hInst = NULL;
static BOOL     suppressCapture = FALSE;

// Helpers ────────────────────────────────────────────────

static wchar_t* utf8_to_wchar(const char* str)
{
    if (!str || !*str) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t* w = malloc(len * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, str, -1, w, len);
    return w;
}

static char* wchar_to_utf8(const wchar_t* wstr)
{
    if (!wstr || !*wstr) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char* u = malloc(len);
    if (u) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, u, len, NULL, NULL);
    return u;
}

static void AddToHistory(const wchar_t* text)
{
    if (!text || !*text)
        return;

    // Avoid exact duplicates
    for (int i = 0; i < historyCount; i++)
    {
        if (wcscmp(history[i], text) == 0)
        {
            free(history[i]);
            for (int j = i; j < historyCount - 1; j++)
                history[j] = history[j + 1];
            historyCount--;
            break;
        }
    }

    // Make room if full (remove oldest)
    if (historyCount == MAX_HISTORY)
    {
        free(history[MAX_HISTORY - 1]);
        historyCount--;
    }

    // Shift right → insert at front (newest first)
    for (int i = historyCount; i > 0; i--)
        history[i] = history[i - 1];

    history[0] = _wcsdup(text);
    historyCount++;
}

static void SaveHistoryToFile(void)
{
    FILE* f = _wfopen(HISTORY_FILE, L"w, ccs=UTF-8");
    if (!f) return;

    for (int i = 0; i < historyCount; i++)
        fwprintf(f, L"%s\n", history[i]);

    fclose(f);
}

static void LoadHistoryFromFile(void)
{
    FILE* f = _wfopen(HISTORY_FILE, L"r, ccs=UTF-8");
    if (!f) return;

    wchar_t line[4096];
    while (fgetws(line, _countof(line), f))
    {
        line[wcscspn(line, L"\r\n")] = L'\0';
        if (wcslen(line) == 0) continue;
        if (historyCount >= MAX_HISTORY) break;

        AddToHistory(line);   // will handle deduplication
    }
    fclose(f);
}

// Core logic ─────────────────────────────────────────────

static void CaptureClipboard(void)
{
    if (!OpenClipboard(NULL)) return;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData)
    {
        const wchar_t* text = GlobalLock(hData);
        if (text && *text)
        {
            AddToHistory(text);
            SaveHistoryToFile();
        }
        GlobalUnlock(hData);
    }

    CloseClipboard();
}

static void CopyToClipboard(int index)
{
    if (index < 0 || index >= historyCount) return;

    suppressCapture = TRUE;
    if (!OpenClipboard(hMainWindow)) { suppressCapture = FALSE; return; }
    EmptyClipboard();

    size_t len = wcslen(history[index]) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
    if (hMem)
    {
        wchar_t* dst = GlobalLock(hMem);
        if (dst)
        {
            wcscpy_s(dst, len, history[index]);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }

    CloseClipboard();
    // suppressCapture is cleared by WM_CLIPBOARDUPDATE when it arrives

    // Optional brief balloon tip
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hMainWindow;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 1500;
    wcscpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), L"Copied");
    wcscpy_s(nid.szInfo, _countof(nid.szInfo), L"Item copied to clipboard.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void PopAndPaste(void)
{
    if (historyCount == 0) return;

    // Write the top item to the clipboard
    suppressCapture = TRUE;
    if (!OpenClipboard(hMainWindow)) { suppressCapture = FALSE; return; }
    EmptyClipboard();

    size_t len = wcslen(history[0]) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
    if (hMem)
    {
        wchar_t* dst = GlobalLock(hMem);
        if (dst)
        {
            wcscpy_s(dst, len, history[0]);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    // suppressCapture is cleared by WM_CLIPBOARDUPDATE when it arrives

    // Pop history[0]
    free(history[0]);
    for (int i = 0; i < historyCount - 1; i++)
        history[i] = history[i + 1];
    history[historyCount - 1] = NULL;
    historyCount--;
    SaveHistoryToFile();

    // Defer the paste until after WM_CLIPBOARDUPDATE has been processed
    PostMessage(hMainWindow, WM_DOPASTE, 0, 0);
}

static void ShowContextMenu(void)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    wchar_t buf[256];
    for (int i = 0; i < historyCount; i++)
    {
        wcsncpy_s(buf, _countof(buf), history[i], _TRUNCATE);
        buf[180] = 0;

        wchar_t item[300];
        if (wcslen(history[i]) > 60)
            swprintf_s(item, _countof(item), L"%.60s\u2026", buf);
        else
            wcscpy_s(item, _countof(item), buf);

        AppendMenuW(hMenu, MF_STRING, 1000 + i, item);
    }

    if (historyCount > 0)
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, 9999, L"Exit");

    SetForegroundWindow(hMainWindow);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hMainWindow, NULL);

    DestroyMenu(hMenu);
}

// Window procedure ───────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        hMainWindow = hwnd;

        // Tray icon
        NOTIFYICONDATAW nid = { sizeof(nid) };
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        wcscpy_s(nid.szTip, _countof(nid.szTip), L"Clipboard History");

        Shell_NotifyIconW(NIM_ADD, &nid);

        // Auto-capture: fires WM_CLIPBOARDUPDATE on every clipboard change (Ctrl+C)
        AddClipboardFormatListener(hwnd);

        // Hotkey: Ctrl+Shift+V → pop top item and paste
        RegisterHotKey(hwnd, HOTKEY_ID, HOTKEY_MODS, HOTKEY_VK);

        // Load saved items
        LoadHistoryFromFile();

        break;
    }

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
            ShowContextMenu();
        break;

    case WM_CLIPBOARDUPDATE:
        if (suppressCapture)
            suppressCapture = FALSE;  // consume the one update caused by our own write
        else
            SetTimer(hwnd, TIMER_CAPTURE, 50, NULL);  // defer read; source app may still hold clipboard
        break;

    case WM_TIMER:
        if (wParam == TIMER_CAPTURE)
        {
            KillTimer(hwnd, TIMER_CAPTURE);
            if (!OpenClipboard(NULL))
            {
                // Source app still hasn't released — retry once after another 100 ms
                SetTimer(hwnd, TIMER_CAPTURE, 100, NULL);
            }
            else
            {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData)
                {
                    const wchar_t* text = GlobalLock(hData);
                    if (text && *text)
                    {
                        AddToHistory(text);
                        SaveHistoryToFile();
                    }
                    GlobalUnlock(hData);
                }
                CloseClipboard();
            }
        }
        break;

    case WM_DOPASTE:
    {
        // Modifiers from the hotkey (Ctrl+Shift) may still be held — release them
        // first so the target app receives a clean Ctrl+V.
        INPUT inputs[6] = { 0 };
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_SHIFT;
        inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_CONTROL;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = VK_CONTROL;
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = 'V';
        inputs[4].type = INPUT_KEYBOARD;
        inputs[4].ki.wVk = 'V';
        inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[5].type = INPUT_KEYBOARD;
        inputs[5].ki.wVk = VK_CONTROL;
        inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(6, inputs, sizeof(INPUT));
        break;
    }

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID)
            PopAndPaste();
        break;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id >= 1000 && id < 1000 + historyCount)
        {
            CopyToClipboard(id - 1000);
        }
        else if (id == 9999)
        {
            DestroyWindow(hwnd);
        }
        break;
    }

    case WM_DESTROY:
    {
        for (int i = 0; i < historyCount; i++)
            free(history[i]);
        historyCount = 0;

        SaveHistoryToFile();

        NOTIFYICONDATAW nid = { sizeof(nid) };
        nid.hWnd = hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);

        RemoveClipboardFormatListener(hwnd);
        UnregisterHotKey(hwnd, HOTKEY_ID);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return 0;
}

// Entry point ────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow)
{
    hInst = hInstance;
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ClipboardHistoryClass";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);

    if (!RegisterClassExW(&wc))
        return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Clipboard History",
        0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    if (!hwnd)
        return 1;

    ShowWindow(hwnd, SW_HIDE);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}