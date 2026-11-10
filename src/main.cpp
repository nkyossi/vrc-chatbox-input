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
// Y goes to VRChat itself while Shift is held, and Ctrl+Alt+Y turns the
// trigger key off and on for typing Y in the game.
//
// The window uses Hiragana IME when it is installed, whatever IME is chosen
// for the other apps (in VRChat it is usually Microsoft IME).
//
//   vrc-chatbox-input.exe [--target VRChat.exe] [--port 9000] [--no-cursor-check] [--no-ime-open]
//                         [--keep-ime] [--startup | --no-startup] [--start-menu | --no-start-menu]

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <commctrl.h>
#include <msctf.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "Osc.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_APP_SHOW = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_TOGGLE = WM_APP + 3;
constexpr UINT WM_APP_ALREADY_RUNNING = WM_APP + 4;
constexpr UINT ID_TRAY_ENABLED = 1;
constexpr UINT ID_TRAY_QUIT = 2;
constexpr UINT ID_TRAY_STARTUP = 3;
constexpr UINT ID_TRAY_SHORTCUT = 4;
constexpr UINT kTriggerKey = 'Y';
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"VRChatChatboxInput";
constexpr UINT_PTR kOpenImeTimer = 1;
constexpr int kMaxChars = 144;  // the limit of the chatbox
const wchar_t kTitle[] = L"VRChat チャット入力";
constexpr COLORREF kBackground = RGB(24, 24, 28);
constexpr COLORREF kText = RGB(245, 245, 245);

struct Options {
    std::wstring target = L"VRChat.exe";
    int port = 9000;
    bool cursor_check = true;
    bool ime_open = true;        // turn the IME on when the window opens
    bool hiragana_ime = true;    // switch to Hiragana IME when the window opens
    bool startup = false;        // only add the sign-in entry, then exit
    bool no_startup = false;     // only remove it, then exit
    bool start_menu = false;     // only add the Start menu shortcut, then exit
    bool no_start_menu = false;  // only remove it, then exit
} g_options;

// Hiragana IME for Windows (https://github.com/nkyossi/hiragana-ime-for-windows)
constexpr CLSID kHiraganaImeClsid = {0x1c841c5a, 0x1337, 0x4e4e, {0xbc, 0x44, 0x5b, 0x0e, 0xa1, 0x76, 0x90, 0x18}};
constexpr GUID kHiraganaImeProfile = {0xc27534cd, 0x9a44, 0x4271, {0xbd, 0xde, 0xe9, 0xfc, 0x24, 0x6a, 0x1d, 0xd5}};

HINSTANCE g_instance = nullptr;
HWND g_popup = nullptr;
HWND g_edit = nullptr;
HWND g_target = nullptr;  // the VRChat window the popup was opened over
HHOOK g_hook = nullptr;
HFONT g_font = nullptr;
bool g_enabled = true;
bool g_swallow_up = false;  // the release of the trigger key is ours too
bool g_swallow_toggle_up = false;  // the release of Ctrl+Alt+Y is ours too
bool g_showing = false;     // in show_popup(): the activation may flicker
SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_address = {};
NOTIFYICONDATAW g_tray = {sizeof g_tray};

std::wstring g_arguments;  // the options, quoted, for the startup entry

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

// Ctrl+Alt+Y: the trigger key off and on, for typing Y in the game.
bool toggle_combo_down() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000);
}

LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (key->vkCode == kTriggerKey) {
            bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
            if (!down && g_swallow_toggle_up) {
                g_swallow_toggle_up = false;
                return 1;
            }
            if (down && toggle_combo_down()) {
                if (!g_swallow_toggle_up) {
                    g_swallow_toggle_up = true;
                    PostMessageW(g_popup, WM_APP_TOGGLE, 0, 0);
                }
                return 1;
            }
            if (down && g_swallow_up) return 1;  // auto-repeat
            if (!down && g_swallow_up) {
                g_swallow_up = false;
                return 1;
            }
            if (down && g_enabled && !IsWindowVisible(g_popup) && !any_modifier_down()) {
                HWND foreground = GetForegroundWindow();
                // The cursor check is the cheap one; while it is shown, the
                // game is in a menu or a text field, and there is no need to
                // look at the process.
                if ((!g_options.cursor_check || is_cursor_hidden()) && is_target_window(foreground)) {
                    g_swallow_up = true;
                    PostMessageW(g_popup, WM_APP_SHOW, 0, reinterpret_cast<LPARAM>(foreground));
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_hook, code, wparam, lparam);
}

// Makes Hiragana IME the IME of this process, if it is installed; returns
// true if it has switched to it now.
bool use_hiragana_ime() {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&profiles))))
        return false;
    TF_INPUTPROCESSORPROFILE active = {};
    if (SUCCEEDED(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active)) &&
        IsEqualCLSID(active.clsid, kHiraganaImeClsid))
        return false;
    return SUCCEEDED(profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN),
                              kHiraganaImeClsid, kHiraganaImeProfile, nullptr,
                              TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE));
}

// The thread manager of TSF, activated once for the life of the program.
// Deactivating it would also take the document of the RichEdit control away
// from TSF, and the IME would then treat the window like an old program that
// does not show it the text (no conversion of the text before the cursor).
ComPtr<ITfThreadMgr> g_thread_mgr;
TfClientId g_client_id = TF_CLIENTID_NULL;

void activate_tsf() {
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_thread_mgr))) &&
        FAILED(g_thread_mgr->Activate(&g_client_id)))
        g_thread_mgr.Reset();
}

// Turns the IME on (あ) for the window: the open/close compartment of the
// thread, as the 半角/全角 key does.
void open_ime() {
    if (!g_thread_mgr) return;
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> open;
    if (FAILED(g_thread_mgr.As(&compartments)) ||
        FAILED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &open)))
        return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = 1;
    open->SetValue(g_client_id, &v);
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
    // The edit control may still have the focus from the last time; then
    // SetFocus() does nothing, the control does not give its document to TSF
    // again, and the IME cannot read the text before the cursor. Move the
    // focus away and back.
    SetFocus(nullptr);
    SetFocus(g_edit);
    bool switched = g_options.hiragana_ime && use_hiragana_ime();
    if (g_options.ime_open) {
        open_ime();
        // A text service that has just started sets its own initial mode;
        // turn it on again after that.
        if (switched) SetTimer(g_popup, kOpenImeTimer, 300, nullptr);
    }
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

// A short, quiet blip in WAV format, played when the text is sent.
const std::vector<uint8_t>& send_sound() {
    static const std::vector<uint8_t> wav = [] {
        const uint32_t rate = 44100;
        const uint32_t samples = rate * 70 / 1000;
        std::vector<int16_t> pcm(samples);
        const double pi = 3.14159265358979;
        double phase = 0;
        for (uint32_t i = 0; i < samples; ++i) {
            double t = static_cast<double>(i) / samples;
            double frequency = 880 + 660 * t;  // rising
            phase += 2 * pi * frequency / rate;
            double envelope = std::min(1.0, t * 20) * (1 - t) * (1 - t);
            pcm[i] = static_cast<int16_t>(std::sin(phase) * envelope * 0.18 * 32767);
        }
        auto u32 = [](std::vector<uint8_t>& v, uint32_t x) {
            for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
        };
        auto u16 = [](std::vector<uint8_t>& v, uint16_t x) {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >> 8));
        };
        std::vector<uint8_t> v;
        uint32_t data = samples * 2;
        v.insert(v.end(), {'R', 'I', 'F', 'F'});
        u32(v, 36 + data);
        v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        u32(v, 16);
        u16(v, 1);  // PCM
        u16(v, 1);  // mono
        u32(v, rate);
        u32(v, rate * 2);
        u16(v, 2);
        u16(v, 16);
        v.insert(v.end(), {'d', 'a', 't', 'a'});
        u32(v, data);
        size_t at = v.size();
        v.resize(at + data);
        std::memcpy(v.data() + at, pcm.data(), data);
        return v;
    }();
    return wav;
}

void send_text() {
    int n = GetWindowTextLengthW(g_edit);
    std::wstring text(static_cast<size_t>(n), L'\0');
    GetWindowTextW(g_edit, text.data(), n + 1);
    // Leading and trailing spaces, including the full-width ones
    size_t first = text.find_first_not_of(L" \t\r\n\x3000");
    size_t last = text.find_last_not_of(L" \t\r\n\x3000");
    text = first == std::wstring::npos ? std::wstring() : text.substr(first, last - first + 1);
    if (!text.empty()) {
        send_osc(vrcinput::chatbox_input(to_utf8(text), true, true));
        PlaySoundW(reinterpret_cast<LPCWSTR>(send_sound().data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
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

// A message in a balloon tip from the tray icon.
void show_balloon(const wchar_t* text, const wchar_t* tip) {
    NOTIFYICONDATAW icon = g_tray;
    icon.uFlags = NIF_TIP | NIF_INFO;
    icon.dwInfoFlags = NIIF_INFO;
    wcscpy_s(icon.szTip, tip);
    wcscpy_s(icon.szInfoTitle, kTitle);
    wcscpy_s(icon.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &icon);
}

void show_balloon(const wchar_t* text) {
    show_balloon(text, kTitle);
}

// Starting with Windows
//
// The trigger key works only while this program is running, so it should
// already be there when VRChat starts. An entry under HKCU\...\Run starts it
// at sign-in, with the same options as the copy that made the entry.

std::wstring exe_path() {
    wchar_t path[MAX_PATH * 2] = {};
    DWORD size = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::wstring(path, size);
}

std::wstring startup_command() {
    std::wstring command = L"\"" + exe_path() + L"\"";
    if (!g_arguments.empty()) command += L" " + g_arguments;
    return command;
}

// The command line registered for the sign-in, or empty if there is none.
std::wstring startup_entry() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return std::wstring();
    wchar_t value[2048] = {};
    DWORD size = sizeof value;
    DWORD type = 0;
    std::wstring command;
    if (RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ))
        command.assign(value, size / sizeof(wchar_t));
    RegCloseKey(key);
    while (!command.empty() && command.back() == L'\0') command.pop_back();
    return command;
}

bool write_startup_command(const std::wstring& command) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LONG result = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                 static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool write_startup(bool on) {
    if (on) return write_startup_command(startup_command());
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LONG result = RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

// Only corrects the program path of the entry, in case the program has been
// moved to another folder since the entry was made. The options chosen then
// are kept.
void refresh_startup() {
    std::wstring entry = startup_entry();
    if (entry.empty()) return;
    size_t end;
    if (entry[0] == L'"') {
        end = entry.find(L'"', 1);
        end = end == std::wstring::npos ? entry.size() : end + 1;
    } else {
        end = entry.find(L' ');
        if (end == std::wstring::npos) end = entry.size();
    }
    std::wstring command = L"\"" + exe_path() + L"\"";
    if (end < entry.size()) command += entry.substr(end);
    if (command != entry) write_startup_command(command);
}

// The Start menu shortcut
//
// A shortcut in the per-user Start menu folder makes the program easy to find
// by typing its name in the Start menu. (Windows search reads that folder.)

const wchar_t kShortcutName[] = L"VRChat チャット入力.lnk";

std::wstring start_menu_shortcut() {
    PWSTR folder = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &folder))) {
        path = folder;
        CoTaskMemFree(folder);
        path += L"\\";
        path += kShortcutName;
    }
    return path;
}

bool shortcut_exists() {
    std::wstring path = start_menu_shortcut();
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool write_shortcut(bool on) {
    std::wstring path = start_menu_shortcut();
    if (path.empty()) return false;
    if (!on) return DeleteFileW(path.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;

    std::wstring exe = exe_path();
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    link->SetPath(exe.c_str());
    size_t slash = exe.find_last_of(L'\\');
    std::wstring folder = slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash);
    link->SetWorkingDirectory(folder.c_str());
    if (!g_arguments.empty()) link->SetArguments(g_arguments.c_str());
    link->SetDescription(L"VRChat のチャットボックスに、どの IME でも入力する");
    link->SetIconLocation(exe.c_str(), 0);
    return SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(path.c_str(), TRUE));
}

// Turns the trigger key on and off, from the tray menu or Ctrl+Alt+Y, and
// says so in a balloon tip: with the key off, Y does nothing in this program.
void set_enabled(bool on) {
    g_enabled = on;
    show_balloon(on ? L"Y キーでチャット入力をひらきます"
                    : L"Y キーはそのまま VRChat にとどきます。もう一度 Ctrl+Alt+Y でもどります",
                 on ? kTitle : L"VRChat チャット入力（Y キーはオフ）");
}

void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_TRAY_ENABLED,
                L"Y キーでチャット入力をひらく\tCtrl+Alt+Y");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    bool startup = !startup_entry().empty();
    AppendMenuW(menu, MF_STRING | (startup ? MF_CHECKED : 0), ID_TRAY_STARTUP, L"Windows と一緒に起動");
    bool shortcut = shortcut_exists();
    AppendMenuW(menu, MF_STRING | (shortcut ? MF_CHECKED : 0), ID_TRAY_SHORTCUT, L"スタートメニューに登録");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"終了");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (id == ID_TRAY_ENABLED) {
        set_enabled(!g_enabled);
    } else if (id == ID_TRAY_STARTUP) {
        bool on = startup_entry().empty();
        if (!write_startup(on)) {
            show_balloon(L"自動起動の設定を変えられませんでした");
        } else {
            show_balloon(on ? L"次に Windows にサインインしたときから、自動で動きます"
                            : L"Windows と一緒には起動しません");
        }
    } else if (id == ID_TRAY_SHORTCUT) {
        bool on = !shortcut_exists();
        if (!write_shortcut(on)) {
            show_balloon(L"スタートメニューに登録できませんでした");
        } else {
            show_balloon(on ? L"スタートメニューに「VRChat チャット入力」を登録しました"
                            : L"スタートメニューからはずしました");
        }
    } else if (id == ID_TRAY_QUIT) {
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK popup_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_APP_SHOW:
            show_popup(reinterpret_cast<HWND>(lparam));
            return 0;
        case WM_APP_TOGGLE:
            set_enabled(!g_enabled);
            return 0;
        case WM_APP_ALREADY_RUNNING:
            show_balloon(L"すでに起動しています。通知領域のアイコンから操作できます");
            return 0;
        case WM_ACTIVATE:
            // Clicking elsewhere cancels, as Esc does.
            if (LOWORD(wparam) == WA_INACTIVE && !g_showing) hide_popup(false);
            break;
        case WM_TIMER:
            if (wparam == kOpenImeTimer) {
                KillTimer(hwnd, kOpenImeTimer);
                if (IsWindowVisible(hwnd)) open_ime();
            }
            return 0;
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
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"--target" && i + 1 < args.size()) {
            g_options.target = args[++i];
        } else if (a == L"--port" && i + 1 < args.size()) {
            g_options.port = _wtoi(args[++i].c_str());
        } else if (a == L"--no-cursor-check") {
            g_options.cursor_check = false;
        } else if (a == L"--no-ime-open") {
            g_options.ime_open = false;
        } else if (a == L"--keep-ime") {
            g_options.hiragana_ime = false;
        } else if (a == L"--startup") {
            g_options.startup = true;
        } else if (a == L"--no-startup") {
            g_options.no_startup = true;
        } else if (a == L"--start-menu") {
            g_options.start_menu = true;
        } else if (a == L"--no-start-menu") {
            g_options.no_start_menu = true;
        }
    }
    // Keep the options as they were given, so the sign-in entry and the Start
    // menu shortcut start the program the same way. The four below are for
    // that setup only; the copy started from it must not see them, or it
    // would exit at once.
    for (const auto& a : args) {
        if (a == L"--startup" || a == L"--no-startup" || a == L"--start-menu" || a == L"--no-start-menu") continue;
        if (!g_arguments.empty()) g_arguments += L' ';
        g_arguments += L"\"" + a + L"\"";
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    parse_options();
    if (g_options.startup) return write_startup(true) ? 0 : 1;
    if (g_options.no_startup) return write_startup(false) ? 0 : 1;
    if (g_options.start_menu || g_options.no_start_menu) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = write_shortcut(g_options.start_menu);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VRChatChatboxInput");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A second copy: tell the one that is already running to say so, so
        // that a double click on the icon is not just silence.
        HWND existing = nullptr;
        for (int i = 0; i < 10 && !existing; ++i) {
            existing = FindWindowW(L"VRChatChatboxInput", nullptr);
            if (!existing) Sleep(100);
        }
        if (existing) PostMessageW(existing, WM_APP_ALREADY_RUNNING, 0, 0);
        CloseHandle(mutex);
        return 0;
    }
    refresh_startup();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        g_address.sin_family = AF_INET;
        g_address.sin_port = htons(static_cast<u_short>(g_options.port));
        inet_pton(AF_INET, "127.0.0.1", &g_address.sin_addr);
    }

    activate_tsf();
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
    if (g_thread_mgr) g_thread_mgr->Deactivate();
    if (g_socket != INVALID_SOCKET) closesocket(g_socket);
    WSACleanup();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
