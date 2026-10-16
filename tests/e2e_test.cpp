// End-to-end test: this program plays VRChat. It opens a window, starts
// vrc-chatbox-input.exe with itself as the target, types with SendInput and
// receives the OSC messages on a UDP port. Do not touch the keyboard or the
// mouse while it runs.
//
//   e2e_test <vrc-chatbox-input.exe>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>

namespace {

constexpr int kPort = 19000;
int g_failures = 0;
HWND g_window = nullptr;
std::wstring g_typed;  // characters the stand-in received
SOCKET g_socket = INVALID_SOCKET;

void check(const char* name, bool ok, const std::string& detail = std::string()) {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : ": ", detail.c_str());
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

void key(WORD vk, bool up) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof in);
    Sleep(30);
}

void tap(WORD vk) {
    key(vk, false);
    key(vk, true);
}

void type(const char* s) {
    for (; *s; ++s) tap(*s == ' ' ? VK_SPACE : static_cast<WORD>(toupper(*s)));
}

// The OSC messages received within the time, as readable text: nulls become '|'.
std::string receive(int milliseconds) {
    std::string all;
    DWORD end = GetTickCount() + static_cast<DWORD>(milliseconds);
    for (;;) {
        DWORD now = GetTickCount();
        if (now >= end) break;
        fd_set set;
        FD_ZERO(&set);
        FD_SET(g_socket, &set);
        timeval tv = {0, static_cast<long>((end - now) * 1000)};
        if (select(0, &set, nullptr, nullptr, &tv) <= 0) break;
        char buffer[1024];
        int n = recv(g_socket, buffer, sizeof buffer, 0);
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) all += buffer[i] ? buffer[i] : '|';
        all += '\n';
    }
    return all;
}

HWND popup() {
    HWND hwnd = FindWindowW(L"VRChatChatboxInput", nullptr);
    return hwnd && IsWindowVisible(hwnd) ? hwnd : nullptr;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CHAR) {
        g_typed += static_cast<wchar_t>(wparam);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void run() {
    Sleep(1500);
    // Share the input state with the window's thread so that this thread may
    // bring it to the front, as a console program cannot by itself.
    AttachThreadInput(GetCurrentThreadId(), GetWindowThreadProcessId(g_window, nullptr), TRUE);
    SetForegroundWindow(g_window);
    SetFocus(g_window);
    Sleep(300);
    check("the stand-in is in front", GetForegroundWindow() == g_window);
    receive(100);

    tap('Y');
    Sleep(800);
    check("Y opens the window", popup() != nullptr);
    check("the window has the focus", popup() && GetForegroundWindow() == popup());
    check("Y does not reach the game", g_typed.find(L'y') == std::wstring::npos);
    std::string got = receive(300);
    check("typing indicator on", got.find("/chatbox/typing|,T") != std::string::npos, got);

    type("hello world");
    tap(VK_RETURN);
    Sleep(500);
    got = receive(500);
    check("Enter sends the text", got.find("/chatbox/input||,sTT||||hello world|") != std::string::npos, got);
    check("typing indicator off", got.find("/chatbox/typing|,F") != std::string::npos, got);
    check("the window closes", popup() == nullptr);
    check("back to the game", GetForegroundWindow() == g_window);
    check("the text does not reach the game", g_typed.empty(), std::to_string(g_typed.size()) + " chars");

    tap('Y');
    Sleep(800);
    type("abc");
    tap(VK_ESCAPE);
    Sleep(500);
    got = receive(500);
    check("Esc closes the window", popup() == nullptr);
    check("Esc sends nothing", got.find("/chatbox/input") == std::string::npos, got);
    check("Esc goes back to the game", GetForegroundWindow() == g_window);

    // Shift+Y is not the trigger.
    key(VK_SHIFT, false);
    tap('Y');
    key(VK_SHIFT, true);
    Sleep(500);
    check("Shift+Y reaches the game", popup() == nullptr && g_typed == L"Y");

    std::printf("%d failure(s)\n", g_failures);
    std::fflush(stdout);
    PostMessageW(g_window, WM_CLOSE, 0, 0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: e2e_test <vrc-chatbox-input.exe>\n");
        return 2;
    }
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(g_socket, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        std::printf("cannot listen on %d\n", kPort);
        return 2;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VRChatStandIn";
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0, wc.lpszClassName, L"VRChat stand-in", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
                               1280, 720, nullptr, nullptr, wc.hInstance, nullptr);

    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    const char* name = strrchr(self, '\\');
    std::string command = std::string("\"") + argv[1] + "\" --target " + (name ? name + 1 : self) + " --port " +
                          std::to_string(kPort) + " --no-cursor-check --no-ime-open";
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("cannot start %s\n", argv[1]);
        return 2;
    }

    std::thread worker(run);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    worker.join();
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    closesocket(g_socket);
    WSACleanup();
    return g_failures == 0 ? 0 : 1;
}
