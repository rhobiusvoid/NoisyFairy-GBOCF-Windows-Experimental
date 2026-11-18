// SPDX-License-Identifier: GPL-2.0-or-later
// ps4rt: keyboard and mouse controls panel, shared by the launcher and the in-game menu (Insert).
// Everything lives in ps4rt_mouse.ini next to ps4rt.ini: the runtime (src/runtime_pad.c) re-reads it
// every second, so edits made here apply to a running game within a second. Header only: the host
// (launcher or overlay) needs SDL3 and Dear ImGui and forwards key presses to OnKey() while
// Capturing().
#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Ps4RtControls {

struct Binding {
    const char* key;     ///< ini key: key_<key>
    const char* label;
    SDL_Scancode def;
    const char* group;
};

inline const Binding kBindings[] = {
    {"move_up", "Forward (left stick up)", SDL_SCANCODE_W, "Movement (left stick)"},
    {"move_left", "Left", SDL_SCANCODE_A, "Movement (left stick)"},
    {"move_down", "Back", SDL_SCANCODE_S, "Movement (left stick)"},
    {"move_right", "Right", SDL_SCANCODE_D, "Movement (left stick)"},
    {"look_up", "Camera up (right stick up)", SDL_SCANCODE_UP, "Camera keys (right stick)"},
    {"look_left", "Camera left", SDL_SCANCODE_LEFT, "Camera keys (right stick)"},
    {"look_down", "Camera down", SDL_SCANCODE_DOWN, "Camera keys (right stick)"},
    {"look_right", "Camera right", SDL_SCANCODE_RIGHT, "Camera keys (right stick)"},
    {"cross", "Cross", SDL_SCANCODE_SPACE, "Buttons"},
    {"circle", "Circle", SDL_SCANCODE_LSHIFT, "Buttons"},
    {"square", "Square", SDL_SCANCODE_E, "Buttons"},
    {"triangle", "Triangle", SDL_SCANCODE_Q, "Buttons"},
    {"l1", "L1", SDL_SCANCODE_1, "Buttons"},
    {"r1", "R1", SDL_SCANCODE_3, "Buttons"},
    {"l2", "L2", SDL_SCANCODE_R, "Buttons"},
    {"r2", "R2", SDL_SCANCODE_F, "Buttons"},
    {"l3", "L3 (left stick click)", SDL_SCANCODE_Z, "Buttons"},
    {"r3", "R3 (right stick click)", SDL_SCANCODE_C, "Buttons"},
    {"options", "Options", SDL_SCANCODE_RETURN, "Buttons"},
    {"touchpad_left", "Touchpad (left)", SDL_SCANCODE_TAB, "Buttons"},
    {"touchpad_right", "Touchpad (right)", SDL_SCANCODE_BACKSPACE, "Buttons"},
    {"dpad_up", "D-pad up", SDL_SCANCODE_I, "D-pad"},
    {"dpad_down", "D-pad down", SDL_SCANCODE_K, "D-pad"},
    {"dpad_left", "D-pad left", SDL_SCANCODE_J, "D-pad"},
    {"dpad_right", "D-pad right", SDL_SCANCODE_L, "D-pad"},
};
inline constexpr int kBindingCount = int(sizeof(kBindings) / sizeof(kBindings[0]));

struct PsName { const char* key; const char* label; };
inline const PsName kPsNames[] = {
    {"", "(nothing)"}, {"cross", "Cross"}, {"circle", "Circle"}, {"square", "Square"}, {"triangle", "Triangle"},
    {"l1", "L1"}, {"r1", "R1"}, {"l2", "L2"}, {"r2", "R2"}, {"l3", "L3"}, {"r3", "R3"}, {"options", "Options"},
    {"touchpad_left", "Touchpad (left)"}, {"touchpad_right", "Touchpad (right)"},
    {"up", "D-pad up"}, {"down", "D-pad down"}, {"left", "D-pad left"}, {"right", "D-pad right"},
};
inline constexpr int kPsNameCount = int(sizeof(kPsNames) / sizeof(kPsNames[0]));

struct MouseButton { const char* key; const char* label; const char* def; };
inline const MouseButton kMouseButtons[] = {
    {"mouse_left", "Left button", "r2"}, {"mouse_right", "Right button", "l2"},
    {"mouse_middle", "Middle button", "r3"}, {"mouse_x1", "Side button 1 (back)", "l1"},
    {"mouse_x2", "Side button 2 (forward)", "r1"}, {"mouse_wheel_up", "Wheel up", ""},
    {"mouse_wheel_down", "Wheel down", ""},
};
inline constexpr int kMouseButtonCount = int(sizeof(kMouseButtons) / sizeof(kMouseButtons[0]));

class Panel {
public:
    void SetPath(std::filesystem::path path) {
        path_ = std::move(path);
        Load();
    }
    bool Capturing() const { return capture_ >= 0; }
    void CancelCapture() { capture_ = -1; }

    /// Feed a key press while Capturing(); returns true when it was used.
    bool OnKey(SDL_Scancode sc) {
        if (capture_ < 0) return false;
        const int index = capture_;
        if (sc == SDL_SCANCODE_ESCAPE) {
            capture_ = -1;
            note_.clear();
            return true;
        }
        if (sc == SDL_SCANCODE_DELETE) {
            Set(std::string("key_") + kBindings[index].key, "", true);
            capture_ = -1;
            note_ = std::string(kBindings[index].label) + " is now unbound.";
            return true;
        }
        if (sc == SDL_SCANCODE_F7 || sc == SDL_SCANCODE_F8 || sc == SDL_SCANCODE_F9 || sc == SDL_SCANCODE_INSERT) {
            note_ = "F7, F8, F9 and Insert are reserved. Press another key.";
            return true;
        }
        const std::string name = SDL_GetScancodeName(sc);
        if (name.empty()) {
            note_ = "That key cannot be used.";
            return true;
        }
        std::string moved;
        for (int i = 0; i < kBindingCount; ++i) {
            if (i != index && SDL_strcasecmp(KeyName(i).c_str(), name.c_str()) == 0) {
                Set(std::string("key_") + kBindings[i].key, "", false);
                moved = kBindings[i].label;
            }
        }
        Set(std::string("key_") + kBindings[index].key, name, true);
        capture_ = -1;
        note_ = moved.empty() ? std::string() : (name + " was bound to \"" + moved + "\"; that one is now unbound.");
        return true;
    }

    void Draw() {
        Refresh();
        ImGui::SeparatorText("Mouse");
        bool look = GetInt("mouse_look", 1) != 0;
        if (ImGui::Checkbox("Mouse look (the camera follows the mouse)", &look)) Set("mouse_look", look ? "1" : "0", true);
        float sens = GetFloat("mouse_sens", 1.0f);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderFloat("Sensitivity", &sens, 0.2f, 3.0f, "%.2f")) Set("mouse_sens", Num(sens), true);
        bool invert = GetInt("mouse_invert_y", 0) != 0;
        if (ImGui::Checkbox("Invert vertical look", &invert)) Set("mouse_invert_y", invert ? "1" : "0", true);
        ImGui::TextDisabled("F8 frees or recaptures the mouse while playing. A controller is not affected.");

        ImGui::SeparatorText("Mouse buttons");
        for (int i = 0; i < kMouseButtonCount; ++i) {
            const MouseButton& mb = kMouseButtons[i];
            std::string current = Get(mb.key, mb.def);
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(mb.label);
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 170.0f);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("##mb", PsLabel(current))) {
                for (int n = 0; n < kPsNameCount; ++n) {
                    if (ImGui::Selectable(kPsNames[n].label, current == kPsNames[n].key)) Set(mb.key, kPsNames[n].key, true);
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }

        const char* group = nullptr;
        for (int i = 0; i < kBindingCount; ++i) {
            if (!group || std::string(group) != kBindings[i].group) {
                group = kBindings[i].group;
                ImGui::SeparatorText(group);
                if (i == 0) ImGui::TextDisabled("Click a key, then press the new one. Esc cancels, Delete unbinds.");
            }
            ImGui::PushID(1000 + i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kBindings[i].label);
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 170.0f);
            const bool waiting = capture_ == i;
            std::string shown = waiting ? "Press a key..." : KeyName(i);
            if (shown.empty()) shown = "(none)";
            if (ImGui::Button((shown + "###key").c_str(), ImVec2(160.0f, 0.0f))) {
                capture_ = waiting ? -1 : i;
                note_.clear();
            }
            ImGui::PopID();
        }
        if (!note_.empty()) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(note_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        if (ImGui::Button("Reset keys")) {
            for (int i = 0; i < kBindingCount; ++i) Set(std::string("key_") + kBindings[i].key, SDL_GetScancodeName(kBindings[i].def), false);
            Save();
            capture_ = -1;
            note_ = "Keys are back to their defaults.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset mouse")) {
            Set("mouse_look", "1", false);
            Set("mouse_sens", "1.00", false);
            Set("mouse_invert_y", "0", false);
            for (int i = 0; i < kMouseButtonCount; ++i) Set(kMouseButtons[i].key, kMouseButtons[i].def, false);
            Save();
            note_ = "Mouse settings are back to their defaults.";
        }
    }

private:
    std::filesystem::path path_;
    std::vector<std::string> lines_;
    int capture_ = -1;
    std::string note_;
    std::filesystem::file_time_type stamp_{};
    std::chrono::steady_clock::time_point last_check_{};

    static std::string Num(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }
    static const char* PsLabel(const std::string& key) {
        for (const PsName& n : kPsNames)
            if (key == n.key) return n.label;
        return key.empty() ? "(nothing)" : key.c_str();
    }
    void Load() {
        lines_.clear();
        std::ifstream f(path_);
        std::string l;
        while (std::getline(f, l)) {
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
            lines_.push_back(l);
        }
        std::error_code ec;
        stamp_ = std::filesystem::last_write_time(path_, ec);
    }
    void Refresh() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_check_ < std::chrono::milliseconds(700)) return;
        last_check_ = now;
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path_, ec);
        if (!ec && t != stamp_) Load();
    }
    static std::string Trim(std::string s) {
        const char* ws = " \t\r\n";
        s.erase(0, s.find_first_not_of(ws) == std::string::npos ? s.size() : s.find_first_not_of(ws));
        const auto e = s.find_last_not_of(ws);
        s.erase(e == std::string::npos ? 0 : e + 1);
        return s;
    }
    bool Find(const std::string& key, std::string& value) const {
        for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
            if (it->empty() || (*it)[0] == '#' || (*it)[0] == ';') continue;
            const auto eq = it->find('=');
            if (eq != std::string::npos && Trim(it->substr(0, eq)) == key) {
                value = Trim(it->substr(eq + 1));
                return true;
            }
        }
        return false;
    }
    std::string Get(const std::string& key, const std::string& def) const {
        std::string v;
        return Find(key, v) ? v : def;
    }
    int GetInt(const std::string& key, int def) const {
        std::string v;
        return Find(key, v) ? std::atoi(v.c_str()) : def;
    }
    float GetFloat(const std::string& key, float def) const {
        std::string v;
        return Find(key, v) ? float(std::atof(v.c_str())) : def;
    }
    std::string KeyName(int i) const {
        std::string v;
        if (Find(std::string("key_") + kBindings[i].key, v)) return v;
        return SDL_GetScancodeName(kBindings[i].def);
    }
    void Set(const std::string& key, const std::string& value, bool save) {
        bool done = false;
        for (auto& l : lines_) {
            if (l.empty() || l[0] == '#' || l[0] == ';') continue;
            const auto eq = l.find('=');
            if (eq != std::string::npos && Trim(l.substr(0, eq)) == key) {
                l = key + "=" + value;
                done = true;
            }
        }
        if (!done) lines_.push_back(key + "=" + value);
        if (save) Save();
    }
    void Save() {
        if (path_.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
        {
            std::ofstream f(path_, std::ios::trunc);
            for (const auto& l : lines_) f << l << '\n';
        }
        stamp_ = std::filesystem::last_write_time(path_, ec);
    }
};

}  // namespace Ps4RtControls
