// VRChat Chatbox Input
//
// Encoding of the OSC messages for the chatbox of VRChat:
//
//   /chatbox/input s b n   s: the text, b: send it at once, n: play the sound
//   /chatbox/typing b      b: show the typing indicator
//
// See https://docs.vrchat.com/docs/osc-as-input-controller

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vrcinput {

// An OSC string: UTF-8, null terminated, padded with nulls to a multiple of 4.
inline void append_osc_string(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    size_t pad = 4 - s.size() % 4;
    out.insert(out.end(), pad, 0);
}

inline std::vector<uint8_t> chatbox_input(const std::string& utf8, bool send_now, bool sound) {
    std::vector<uint8_t> out;
    append_osc_string(out, "/chatbox/input");
    append_osc_string(out, std::string(",s") + (send_now ? "T" : "F") + (sound ? "T" : "F"));
    append_osc_string(out, utf8);
    return out;
}

inline std::vector<uint8_t> chatbox_typing(bool typing) {
    std::vector<uint8_t> out;
    append_osc_string(out, "/chatbox/typing");
    append_osc_string(out, typing ? ",T" : ",F");
    return out;
}

}  // namespace vrcinput
