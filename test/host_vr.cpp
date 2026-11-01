// host_vr.cpp - imitates REFramework's OpenVR input path (VR.cpp
// initialize_openvr_input, is_action_active, get_joystick_axis) and checks
// what RE2's openvr_input_to_re2_re3 would see. Built as re2.exe.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <openvr.h>
#include <reframework/API.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

typedef void* (*PFN_GGI)(const char*, vr::EVRInitError*);
typedef void (*PFN_set)(const char*, float);
typedef void (*PFN_clear)();
typedef int (*PFN_fb)();

static const char* kNames[] = {"Trigger", "Grip", "JoystickClick", "AButton", "BButton", "DPad_Up", "DPad_Right", "DPad_Down",
                               "DPad_Left", "SystemButton", "WeaponDial_Start", "RE3_Dodge", "RE2_Quickturn",
                               "RE2_FirstPerson_Toggle", "RE2_Reset_View", "RE2_Change_Ammo", "RE2_Toggle_Flashlight", "MiniMap"};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // REFramework.cpp:505 loads openvr_api.dll from the game folder at startup
    HMODULE ovr = LoadLibraryA("openvr_api.dll");
    if (!ovr) { printf("FAIL openvr_api.dll\n"); return 1; }
    HMODULE plug = LoadLibraryA("reframework\\plugins\\re2_frame_controllers.dll");
    if (!plug) { printf("FAIL plugin %lu\n", GetLastError()); return 1; }
    auto plugin_init = (REFPluginInitializeFn)GetProcAddress(plug, "reframework_plugin_initialize");
    // Real order seen in the user's log: SteamVR is set up BEFORE reframework_plugin_initialize.
    const bool late_init = getenv("RE2_TEST_LATE_INIT") != nullptr;
    Sleep(200);  // gap between PluginLoader::early_init and VR init
    if (!late_init) plugin_init(nullptr);

    // VR::initialize_openvr_input: REFramework writes actions.json (done by the test script),
    // then vr::VRInput()->SetActionManifestPath(...)
    auto ggi = (PFN_GGI)GetProcAddress(ovr, "VR_GetGenericInterface");
    vr::EVRInitError err;
    auto* in = (vr::IVRInput*)ggi("IVRInput_010", &err);
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    std::string manifest = std::string(cwd) + "\\actions.json";
    printf("SetActionManifestPath -> %d\n", (int)in->SetActionManifestPath(manifest.c_str()));
    auto set = (PFN_set)GetProcAddress(ovr, "mock_vr_set");
    auto clear = (PFN_clear)GetProcAddress(ovr, "mock_vr_clear");
    printf("SteamVR mock uses frame_controller binding: %s\n", ((PFN_fb)GetProcAddress(ovr, "mock_vr_frame_binding"))() ? "yes" : "NO");

    if (late_init) { printf("plugin_initialize called AFTER SetActionManifestPath\n"); plugin_init(nullptr); }
    vr::VRActionSetHandle_t aset;
    in->GetActionSetHandle("/actions/default", &aset);
    std::map<std::string, vr::VRActionHandle_t> h;
    for (auto* n : kNames) {
        std::string full = std::string("/actions/default/in/") + n;
        in->GetActionHandle(full.c_str(), &h[n]);
    }
    vr::VRActionHandle_t joy, blk, heal;
    in->GetActionHandle("/actions/default/in/Joystick", &joy);
    in->GetActionHandle("/actions/default/in/Block", &blk);
    in->GetActionHandle("/actions/default/in/Heal", &heal);
    vr::VRInputValueHandle_t hand[2];
    in->GetInputSourceHandle("/user/hand/left", &hand[0]);
    in->GetInputSourceHandle("/user/hand/right", &hand[1]);

    vr::VRActiveActionSet_t as{};
    as.ulActionSet = aset;
    auto update = [&]() { in->UpdateActionState(&as, sizeof(as), 1); };
    auto active = [&](vr::VRActionHandle_t a, int hh, bool* ch = nullptr) {
        vr::InputDigitalActionData_t d{};
        in->GetDigitalActionData(a, &d, sizeof(d), hand[hh]);
        if (ch) *ch = d.bChanged;
        return d.bActive && d.bState;
    };
    auto snapshot = [&]() {
        std::set<std::string> s;
        for (auto& kv : h)
            for (int hh = 0; hh < 2; ++hh)
                if (active(kv.second, hh)) {
                    std::string n = kv.first;
                    for (auto& c : n) c = tolower(c);
                    s.insert(n + (hh ? "/right" : "/left"));
                }
        return s;
    };
    auto join = [](const std::set<std::string>& s) { std::string o; for (auto& x : s) o += x + " "; return o; };

    struct Case { const char* phys; const char* label; std::set<std::string> expect; };
    const Case cases[] = {
        {"/user/hand/right/input/a", "A (interact)", {"abutton/right", "re3_dodge/right"}},
        {"/user/hand/right/input/b", "B (back)", {"abutton/left"}},
        {"/user/hand/right/input/x", "X (reload)", {"bbutton/right"}},
        {"/user/hand/right/input/y", "Y (inventory)", {"bbutton/left"}},
        {"/user/hand/left/input/bumper", "LB (sub weapon)", {"grip/left"}},
        {"/user/hand/right/input/bumper", "RB (ammo, tab R)", {"re2_change_ammo/right", "grip/right"}},
        {"/user/hand/left/input/dpad_up", "D-up", {"dpad_up/left"}},
        {"/user/hand/left/input/dpad_down", "D-down", {"dpad_down/left"}},
        {"/user/hand/left/input/dpad_left", "D-left", {"dpad_left/left"}},
        {"/user/hand/left/input/dpad_right", "D-right", {"dpad_right/left"}},
        {"/user/hand/left/input/view", "View (map)", {"minimap/left"}},
        {"/user/hand/right/input/menu", "Menu (pause)", {"systembutton/right"}},
        {"/user/hand/left/input/thumbstick", "L3 (run)", {"joystickclick/left"}},
        {"/user/hand/right/input/thumbstick", "R3 (reset cam)", {"joystickclick/right", "re2_reset_view/right"}},
        {"/user/hand/left/input/trigger", "LT (defense)", {"trigger/left"}},
        {"/user/hand/right/input/grip", "R grip (aim)", {"grip/right"}},
        {"/user/hand/left/input/grip", "L grip (support)", {"grip/left"}},
    };
    int fails = 0;
    update();
    printf("idle: %s\n", join(snapshot()).c_str());
    for (auto& c : cases) {
        clear();
        set(c.phys, 1.0f);
        update();
        auto s = snapshot();
        bool ok = s == c.expect;
        if (!ok) fails++;
        printf("%-5s %-18s -> %s\n", ok ? "OK" : "FAIL", c.label, join(s).c_str());
        if (!ok) printf("      expected %s\n", join(c.expect).c_str());
        clear();
        update();
        if (c.label[0] == 'B' || c.label[0] == 'Y') {
            bool ch = false;
            bool st = active(c.label[0] == 'B' ? h["AButton"] : h["BButton"], 0, &ch);
            bool ok2 = !st && ch;
            if (!ok2) fails++;
            printf("%-5s %-18s    release edge %s/left: state=%d changed=%d\n", ok2 ? "OK" : "FAIL", "",
                   c.label[0] == 'B' ? "abutton" : "bbutton", (int)st, (int)ch);
        }
    }
    // trigger_aims sequence (defaults: fire_delay 150 ms, aim_hold 800 ms)
    {
        auto chk = [&](const char* label, bool grip, bool trig) {
            bool g = active(h["Grip"], 1), t = active(h["Trigger"], 1);
            bool ok = g == grip && t == trig;
            if (!ok) fails++;
            printf("%-5s %-34s grip/right=%d trigger/right=%d (want %d %d)\n", ok ? "OK" : "FAIL", label, (int)g, (int)t, (int)grip, (int)trig);
        };
        const char* T = "/user/hand/right/input/trigger";
        clear(); update(); Sleep(900); update();
        set(T, 1.0f); update(); chk("RT press: aims, fire delayed", true, false);
        Sleep(200); update(); chk("RT held 200 ms: fires", true, true);
        clear(); update(); chk("RT released: still aiming", true, false);
        Sleep(100); set(T, 1.0f); update(); chk("RT again within hold: fires at once", true, true);
        clear(); update(); Sleep(900); update(); chk("900 ms later: aim released", false, false);
    }
    if (getenv("RE2_FRAME_TEST_NO_PLAYER")) {
        clear(); set("/user/hand/left/input/dpad_up", 1.0f); update();
        vr::InputAnalogActionData_t d{};
        in->GetAnalogActionData(joy, &d, sizeof(d), hand[0]);
        bool ok = d.bActive && d.x == 0.0f && d.y == 1.0f;
        if (!ok) fails++;
        printf("%-5s menu D-up -> left stick x=%.2f y=%.2f\n", ok ? "OK" : "FAIL", d.x, d.y);
        clear(); update();
    }
    for (int hh = 0; hh < 2; ++hh) {
        clear();
        std::string p = hh ? "/user/hand/right/input/thumbstick" : "/user/hand/left/input/thumbstick";
        set((p + "/x").c_str(), 0.0f);
        set((p + "/y").c_str(), 1.0f);
        update();
        vr::InputAnalogActionData_t d{};
        in->GetAnalogActionData(joy, &d, sizeof(d), hand[hh]);
        bool ok = d.bActive && d.x == 0.0f && d.y == 1.0f;
        if (!ok) fails++;
        printf("%-5s %s stick up -> x=%.2f y=%.2f\n", ok ? "OK" : "FAIL", hh ? "right" : "left", d.x, d.y);
    }
    // REFramework menu pointer: emulate OverlayComponent::update_input
    {
        auto* ov = (vr::IVROverlay*)ggi("IVROverlay_025", &err);
        ggi("IVRSystem_022", &err);
        ggi("IVRRenderModels_006", &err);
        const vr::VROverlayHandle_t H = 42;
        auto frame = [&](std::vector<vr::VREvent_t>& out) {
            out.clear();
            update();
            vr::VREvent_t e{};
            while (ov->PollNextOverlayEvent(H, &e, sizeof(e))) out.push_back(e);
        };
        auto has = [](const std::vector<vr::VREvent_t>& v, uint32_t t) {
            for (auto& e : v) if (e.eventType == t) return &e;
            return (const vr::VREvent_t*)nullptr;
        };
        std::vector<vr::VREvent_t> ev;
        auto chk = [&](const char* label, bool ok) { if (!ok) fails++; printf("%-5s pointer: %s\n", ok ? "OK" : "FAIL", label); };
        clear(); set("ptr/x", 0.1f); set("ptr/y", 0.2f);
        frame(ev); chk("menu closed -> no events", ev.empty());
        ov->SetOverlayFlag(H, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);  // what REFramework does on open
        chk("SteamVR did NOT get the interactive flag (input kept)", ((int (*)())GetProcAddress(ovr, "mock_vr_interactive_flag"))() == 0);
        frame(ev);
        auto* mv = has(ev, vr::VREvent_MouseMove);
        chk("menu open -> MouseMove", mv != nullptr);
        if (mv) chk("MouseMove at x=1152 y=756 (uv 0.6/0.7 * 1920x1080)", fabsf(mv->data.mouse.x - 1152) < 0.5f && fabsf(mv->data.mouse.y - 756) < 0.5f);
        {   // ImGui Win32 backend fallback: GetCursorPos + ScreenToClient on the foreground window
            HWND w = CreateWindowExA(0, "STATIC", "re2test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 480, nullptr, nullptr, nullptr, nullptr);
            SetForegroundWindow(w);
            HWND fg = GetForegroundWindow();
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(fg, &pt);
            printf("      foreground %p (ours %p), cursor client %ld %ld\n", (void*)fg, (void*)w, pt.x, pt.y);
            chk("GetCursorPos from REFramework -> pointer (client 1152 324)", fg && pt.x == 1152 && pt.y == 324);
            DestroyWindow(w);
        }
        set("/user/hand/right/input/trigger", 1.0f);
        frame(ev); chk("trigger -> MouseButtonDown", has(ev, vr::VREvent_MouseButtonDown) != nullptr);
        chk("heal NOT held while pointing (pointer_hold_menu = 0 default)", !active(heal, 1));
        chk("trigger NOT sent to the game while on the menu", !active(h["Trigger"], 1));
        frame(ev); chk("trigger held -> no second Down", has(ev, vr::VREvent_MouseButtonDown) == nullptr);
        set("/user/hand/right/input/trigger", 0.0f);
        frame(ev); chk("trigger released -> MouseButtonUp", has(ev, vr::VREvent_MouseButtonUp) != nullptr);
        set("/user/hand/right/input/thumbstick/y", 1.0f);
        frame(ev);
        auto* sc = has(ev, vr::VREvent_ScrollSmooth);
        chk("right stick up -> ScrollSmooth +0.2", sc && fabsf(sc->data.scroll.ydelta - 0.2f) < 1e-4f);
        set("/user/hand/right/input/thumbstick/y", 0.0f);
        set("/user/hand/right/input/trigger", 1.0f); frame(ev);
        set("ptr/x", 2.0f);
        frame(ev); chk("pointer leaves menu while held -> MouseButtonUp", has(ev, vr::VREvent_MouseButtonUp) != nullptr);
        ov->SetOverlayFlag(H, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
        clear(); frame(ev); chk("menu closed again -> no events", ev.empty());
        set("/user/hand/right/input/trigger", 1.0f); update();
        chk("trigger reaches the game again", active(h["Trigger"], 1));
        chk("heal no longer held after the menu closed", !active(heal, 1));
        {
            POINT a{}, b{};
            GetCursorPos(&a);
            HWND w = CreateWindowExA(0, "STATIC", "re2test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 480, nullptr, nullptr, nullptr, nullptr);
            SetForegroundWindow(w);
            GetCursorPos(&b); ScreenToClient(w, &b);
            chk("menu closed -> GetCursorPos is the real cursor again", !(b.x == 1152 && b.y == 324));
            DestroyWindow(w);
        }
        clear(); update();
    }
    printf("RESULT: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 2 : 0;
}
