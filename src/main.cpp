// VRChat Chatbox Input
//
// A small window for typing the chatbox of VRChat with any IME. VRChat runs
// under Easy Anti-Cheat, which keeps many IMEs out of the game; this window is
// a process of its own, so every IME works in it, and the text goes to VRChat
// through OSC.
//
// While VRChat is in the foreground and the mouse cursor is hidden (moving
// around in the desktop mode), the trigger key (Y) opens the window over the
// lower part of VRChat instead of the chatbox of the game. Enter sends the
// text and goes back to VRChat; Esc cancels.
//
//   vrc-chatbox-input.exe [--target VRChat.exe] [--port 9000] [--no-cursor-check] [--no-ime-open]

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <commctrl.h>
#include <msctf.h>
#include <richedit.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Osc.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_APP_SHOW = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT ID_TRAY_ENABLED = 1;
constexpr UINT ID_TRAY_QUIT = 2;
constexpr UINT kTriggerKey = 'Y';
constexpr int kMaxChars = 144;  // the limit of the chatbox
const wchar_t kTitle[] = L"VRChat チャット入力";
constexpr COLORREF kBackground = RGB(255, 255, 255);
constexpr COLORREF kText = RGB(20, 20, 20);

struct Options {
    std::wstring target = L"VRChat.exe";
    int port = 9000;
    bool cursor_check = true;
    bool ime_open = true;  // turn the IME on when the window opens
} g_options;

HINSTANCE g_instance = nullptr;
HWND g_popup = nullptr;
HWND g_edit = nullptr;
HWND g_target = nullptr;  // the VRChat window the popup was opened over
HHOOK g_hook = nullptr;
HFONT g_font = nullptr;
bool g_enabled = true;
bool g_swallow_up = false;  // the release of the trigger key is ours too
bool g_showing = false;     // in show_popup(): the activation may flicker
SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_address = {};
NOTIFYICONDATAW g_tray = {sizeof g_tray};

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

void send_osc(const std::vector<uint8_t>& message) {
    if (g_socket == INVALID_SOCKET) return;
    sendto(g_socket, reinterpret_cast<const char*>(message.data()), static_cast<int>(message.size()), 0,
           reinterpret_cast<const sockaddr*>(&g_address), sizeof g_address);
}

bool is_target_window(HWND hwnd) {
    if (!hwnd) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return false;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2];
        DWORD size = static_cast<DWORD>(std::size(path));
        bool match = false;
        if (QueryFullProcessImageNameW(process, 0, path, &size)) {
            const wchar_t* name = wcsrchr(path, L'\\');
            match = _wcsicmp(name ? name + 1 : path, g_options.target.c_str()) == 0;
        }
        CloseHandle(process);
        return match;
    }
    // The anti-cheat may refuse even this; fall back on the window title.
    wchar_t title[64] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    std::wstring target = g_options.target;
    if (target.size() > 4 && _wcsicmp(target.c_str() + target.size() - 4, L".exe") == 0) target.resize(target.size() - 4);
    return _wcsicmp(title, target.c_str()) == 0;
}

// In the desktop mode VRChat hides the cursor while the player looks around,
// and shows it in the menus and the text fields of the game.
bool is_cursor_hidden() {
    CURSORINFO info = {sizeof info};
    return GetCursorInfo(&info) && !(info.flags & CURSOR_SHOWING);
}

bool any_modifier_down() {
    for (int vk : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN}) {
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    }
    return false;
}

LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (key->vkCode == kTriggerKey) {
            bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
            if (down && g_swallow_up) return 1;  // auto-repeat
            if (!down && g_swallow_up) {
                g_swallow_up = false;
                return 1;
            }
            if (down && g_enabled && !IsWindowVisible(g_popup) && !any_modifier_down()) {
                HWND foreground = GetForegroundWindow();
                if (is_target_window(foreground) && (!g_options.cursor_check || is_cursor_hidden())) {
                    g_swallow_up = true;
                    PostMessageW(g_popup, WM_APP_SHOW, 0, reinterpret_cast<LPARAM>(foreground));
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_hook, code, wparam, lparam);
}

// Turns the IME on (あ) for the window: the open/close compartment of the
// thread, as the 半角/全角 key does.
void open_ime() {
    ComPtr<ITfThreadMgr> thread_mgr;
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr)))) return;
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> open;
    if (FAILED(thread_mgr.As(&compartments)) ||
        FAILED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &open)))
        return;
    TfClientId client = TF_CLIENTID_NULL;
    if (FAILED(thread_mgr->Activate(&client))) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = 1;
    open->SetValue(client, &v);
    thread_mgr->Deactivate();
}

void bring_to_front(HWND hwnd) {
    // Windows lets only the foreground process change the foreground window;
    // share the input state with it for a moment.
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = GetWindowThreadProcessId(foreground, nullptr);
    DWORD this_thread = GetCurrentThreadId();
    bool attached = foreground_thread != this_thread && AttachThreadInput(this_thread, foreground_thread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (attached) AttachThreadInput(this_thread, foreground_thread, FALSE);
    if (GetForegroundWindow() != hwnd) {
        // A tap of Alt also gives the right to change it.
        INPUT inputs[2] = {};
        inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = inputs[1].ki.wVk = VK_MENU;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        SetForegroundWindow(hwnd);
    }
}

void show_popup(HWND target) {
    g_target = target;
    RECT client = {};
    GetClientRect(target, &client);
    MapWindowPoints(target, nullptr, reinterpret_cast<POINT*>(&client), 2);
    UINT dpi = GetDpiForWindow(target);
    auto scale = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    int width = std::clamp(static_cast<int>((client.right - client.left) * 0.5), scale(360), scale(900));
    int height = scale(44);
    int x = client.left + (client.right - client.left - width) / 2;
    int y = client.top + static_cast<int>((client.bottom - client.top) * 0.72);

    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(-scale(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    // WM_SETFONT resets the character format; set the color after it.
    CHARFORMAT2W format = {};
    format.cbSize = sizeof format;
    format.dwMask = CFM_COLOR;
    format.crTextColor = kText;
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
    SetWindowPos(g_popup, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    int pad = scale(8);
    SetWindowPos(g_edit, nullptr, pad, pad, width - 2 * pad, height - 2 * pad, SWP_NOZORDER);
    SetWindowTextW(g_edit, L"");
    g_showing = true;
    bring_to_front(g_popup);
    g_showing = false;
    SetFocus(g_edit);
    if (g_options.ime_open) open_ime();
    send_osc(vrcinput::chatbox_typing(true));
}

void hide_popup(bool back_to_target) {
    if (!IsWindowVisible(g_popup)) return;
    // Give the foreground back while this process still owns it; once the
    // window is hidden, Windows may not allow it any more.
    if (back_to_target && g_target && IsWindow(g_target)) {
        g_showing = true;  // not a click elsewhere
        bring_to_front(g_target);
        g_showing = false;
    }
    ShowWindow(g_popup, SW_HIDE);
    send_osc(vrcinput::chatbox_typing(false));
}

void send_text() {
    int n = GetWindowTextLengthW(g_edit);
    std::wstring text(static_cast<size_t>(n), L'\0');
    GetWindowTextW(g_edit, text.data(), n + 1);
    // Leading and trailing spaces, including the full-width ones
    size_t first = text.find_first_not_of(L" \t\r\n\x3000");
    size_t last = text.find_last_not_of(L" \t\r\n\x3000");
    text = first == std::wstring::npos ? std::wstring() : text.substr(first, last - first + 1);
    if (!text.empty()) send_osc(vrcinput::chatbox_input(to_utf8(text), true, true));
    hide_popup(true);
}

LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        // The IME takes Enter and Esc while it converts; these are the ones it left.
        if (wparam == VK_RETURN) {
            send_text();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            hide_popup(true);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wparam == L'\r' || wparam == 0x1b)) return 0;
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_TRAY_ENABLED, L"Y キーでチャット入力をひらく");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"終了");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (id == ID_TRAY_ENABLED) {
        g_enabled = !g_enabled;
    } else if (id == ID_TRAY_QUIT) {
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK popup_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_APP_SHOW:
            show_popup(reinterpret_cast<HWND>(lparam));
            return 0;
        case WM_ACTIVATE:
            // Clicking elsewhere cancels, as Esc does.
            if (LOWORD(wparam) == WA_INACTIVE && !g_showing) hide_popup(false);
            break;
        case WM_SETFOCUS:
            SetFocus(g_edit);
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) show_tray_menu(hwnd);
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void parse_options() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--target" && i + 1 < argc) {
            g_options.target = argv[++i];
        } else if (a == L"--port" && i + 1 < argc) {
            g_options.port = _wtoi(argv[++i]);
        } else if (a == L"--no-cursor-check") {
            g_options.cursor_check = false;
        } else if (a == L"--no-ime-open") {
            g_options.ime_open = false;
        }
    }
    LocalFree(argv);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VRChatChatboxInput");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    g_instance = instance;
    parse_options();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        g_address.sin_family = AF_INET;
        g_address.sin_port = htons(static_cast<u_short>(g_options.port));
        inet_pton(AF_INET, "127.0.0.1", &g_address.sin_addr);
    }

    LoadLibraryW(L"msftedit.dll");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = popup_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = CreateSolidBrush(kBackground);
    wc.lpszClassName = L"VRChatChatboxInput";
    RegisterClassW(&wc);
    g_popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, kTitle, WS_POPUP | WS_BORDER, 0, 0,
                              400, 44, nullptr, nullptr, instance, nullptr);
    g_edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 100, 30, g_popup,
                             nullptr, instance, nullptr);
    // Let the IME (TSF) read the text before the cursor, as in other apps.
    SendMessageW(g_edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    SendMessageW(g_edit, EM_SETLANGOPTIONS, 0, 0);
    SendMessageW(g_edit, EM_EXLIMITTEXT, 0, kMaxChars);
    // Light, so that the text being converted, which the IMEs show with
    // their own colors, is easy to read.
    SendMessageW(g_edit, EM_SETBKGNDCOLOR, 0, kBackground);
    SetWindowSubclass(g_edit, edit_proc, 1, 0);

    g_tray.hWnd = g_popup;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = wc.hIcon;
    wcscpy_s(g_tray.szTip, kTitle);
    Shell_NotifyIconW(NIM_ADD, &g_tray);

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, instance, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_hook) UnhookWindowsHookEx(g_hook);
    if (g_socket != INVALID_SOCKET) closesocket(g_socket);
    WSACleanup();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
