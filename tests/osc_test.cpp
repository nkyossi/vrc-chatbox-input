// Checks the bytes of the OSC messages.

#include <cstdio>
#include <string>
#include <vector>

#include "Osc.h"

namespace {

int g_failures = 0;

void expect(const char* name, const std::vector<uint8_t>& actual, const std::string& expected) {
    bool ok = std::string(actual.begin(), actual.end()) == expected;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace vrcinput;
    // "/chatbox/input" is 14 bytes: 2 nulls; ",sTT": 4 nulls; "あ" (3 bytes): 1 null.
    expect("chatbox/input", chatbox_input(u8"あ", true, true),
           std::string("/chatbox/input\0\0,sTT\0\0\0\0", 24) + u8"あ" + std::string(1, '\0'));
    expect("chatbox/input to the keyboard without the sound", chatbox_input("abcd", false, false),
           std::string("/chatbox/input\0\0,sFF\0\0\0\0abcd\0\0\0\0", 32));
    // "/chatbox/typing" is 15 bytes: 1 null.
    expect("chatbox/typing", chatbox_typing(true), std::string("/chatbox/typing\0,T\0\0", 20));
    expect("chatbox/typing off", chatbox_typing(false), std::string("/chatbox/typing\0,F\0\0", 20));
    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
