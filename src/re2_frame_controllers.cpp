// re2_frame_controllers.cpp
//
// REFramework plugin + OpenXR API layer + OpenVR IVRInput hook in ONE DLL.
// Makes Steam Frame controllers work in Resident Evil 2 (2019, RT build)
// played through REFramework VR, on BOTH runtimes, with an Xbox-like button
// layout. Based on re7_frame_controllers v0.5.
//
// Install:
//   reframework/plugins/re2_frame_controllers.dll
//   reframework/plugins/re2_frame_controllers.ini
//   reframework/plugins/re2_frame_controllers/re2_frame_controllers.json
//
// RE2 input path (verified in REFramework source): VR::openvr_input_to_re2_re3
// reads REFramework actions PER HAND and writes app.ropeway.InputDefine bits:
//   abutton/right = ACTION, DECIDE     abutton/left = CANCEL
//   bbutton/right = RELOAD, SKIP_EVENT bbutton/left = INVENTORY
//   grip/right = HOLD (aim)            grip/left = SUPPORT_HOLD (sub weapon), UI LB
//   dpad_* = SHORTCUT_*                minimap = MINIMAP, systembutton = PAUSE
//   re2_change_ammo = CHANGE_BULLET    re2_reset_view = RESET_CAMERA
//
// Xbox layout on the Frame:
//   A = interact, B = back (abutton/left), X = reload, Y = inventory (bbutton/left)
//   LB = sub weapon, RB = change ammo, D-pad = weapons, View = map, Menu = pause
//   L3 = run, R3 = reset camera. Triggers and grips stay VR-native.
//
// Frame B and Y are on the RIGHT controller but RE2 reads them as LEFT-hand
// actions. A plain binding cannot do that (the subaction / restrict device is
// the physical hand), so:
//   OpenXR: the layer owns actions frame_b/frame_y and ORs them into
//           abutton/left and bbutton/left in xrGetActionStateBoolean.
//   OpenVR: the injected Frame binding puts B/Y on the carrier actions
//           block/heal (not read by RE2), and the IVRInput hook ORs them into
//           abutton/left and bbutton/left in GetDigitalActionData.
//
// OpenVR: REFramework rewrites actions.json on every start. The hook on
// IVRInput::SetActionManifestPath adds a frame_controller default binding to
// that file and writes bindings_frame_controller.json next to it, just before
// SteamVR reads it.
//
// Code, comments and log messages: English, ASCII only.

#ifndef DEBUG_LOG
#define DEBUG_LOG 0  // 1 = write re2_frame_controllers_log.txt + loader diagnostics (debug build)
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <reframework/API.h>
#include <openvr.h>  // types only (IVRInput_010 structs), nothing is linked
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Compile-time checks against the official headers
// ---------------------------------------------------------------------------
static_assert(sizeof(XrPath) == 8, "XrPath must be 64-bit");
static_assert(offsetof(XrActionSuggestedBinding, binding) == 8, "XrActionSuggestedBinding layout");
static_assert(offsetof(XrActionStateBoolean, currentState) == 16, "XrActionStateBoolean layout");
static_assert(offsetof(XrActionStateBoolean, changedSinceLastSync) == 20, "XrActionStateBoolean layout");
static_assert(offsetof(XrActionStateBoolean, isActive) == 32, "XrActionStateBoolean layout");
static_assert(sizeof(vr::InputDigitalActionData_t) == 24, "InputDigitalActionData_t size");
static_assert(offsetof(vr::InputDigitalActionData_t, activeOrigin) == 8, "InputDigitalActionData_t layout");
static_assert(offsetof(vr::InputDigitalActionData_t, bState) == 16, "InputDigitalActionData_t layout");
static_assert(offsetof(vr::InputDigitalActionData_t, bChanged) == 17, "InputDigitalActionData_t layout");
static_assert(offsetof(vr::InputAnalogActionData_t, x) == 16, "InputAnalogActionData_t layout");
#ifdef NEGATIVE_CONTROL
static_assert(offsetof(XrActionStateBoolean, currentState) == 12, "negative control: must fail");
static_assert(offsetof(vr::InputDigitalActionData_t, bState) == 9, "negative control: must fail");
#endif

#define PLUGIN_VERSION "1.1"
#define BASE_NAME "re2_frame_controllers"
#define LAYER_NAME "XR_APILAYER_ZED_re2_frame_bindings"
#define FRAME_EXT_NAME "XR_VALVE_frame_controller_interaction"
#define FRAME_PROFILE "/interaction_profiles/valve/frame_controller_valve"
// REFramework picks the stick action from its per-profile path_map
// (OpenXR::get_left/right_stick_axis). It has no Frame entry, so it falls back
// to the unbound "touchpad" action and the sticks read 0. The layer therefore
// reports the Touch profile to the app while the runtime uses the Frame profile.
#define SPOOF_PROFILE "/interaction_profiles/oculus/touch_controller"

// Processes the plugin is active in (lower case). Verified names:
// REFramework GameIdentity.cpp maps "re2" and "bhd2" (JP) to RE2.
static const char* kAllowedExe[] = {"re2.exe", "bhd2.exe"};
// Set this env var to "1" to activate in any process (testing).
#define ENV_FORCE "RE2_FRAME_ANY_EXE"

// IVRInput_010 vtable indices (counted in REFramework's openvr.h)
enum : int {
    VT_SetActionManifestPath = 0,
    VT_GetActionHandle = 2,
    VT_GetInputSourceHandle = 3,
    VT_UpdateActionState = 4,
    VT_GetDigitalActionData = 5,
    VT_GetAnalogActionData = 6,
    VT_GetOriginLocalizedName = 23,
};
// IVROverlay_025 / IVRSystem_022 / IVRRenderModels_006 (counted in REFramework's openvr.h)
enum : int {
    VT_OV_SetOverlayFlag = 10,
    VT_OV_GetOverlayFlag = 11,
    VT_OV_PollNextOverlayEvent = 45,
    VT_OV_GetOverlayMouseScale = 48,
    VT_OV_GetOverlayTextureBounds = 28,
    VT_OV_ComputeOverlayIntersection = 50,
    VT_SYS_GetDeviceToAbsoluteTrackingPose = 11,
    VT_SYS_GetTrackedDeviceIndexForControllerRole = 17,
    VT_SYS_GetStringTrackedDeviceProperty = 27,
    VT_RM_GetComponentStateForDevicePath = 13,
};

// ---------------------------------------------------------------------------
// Binding table: REFramework action name -> Frame input path (OpenXR syntax).
// The OpenVR binding file is generated from the same table.
// Physical Frame layout (Valve docs): right = A B X Y, menu, bumper, trigger,
// squeeze, thumbstick; left = dpad_*, view, bumper, trigger, squeeze, thumbstick.
// ---------------------------------------------------------------------------
struct FrameBind {
    const char* action;
    const char* path;
};

static const FrameBind kFrameBinds[] = {
    {"pose", "/user/hand/left/input/grip/pose"},
    {"pose", "/user/hand/right/input/grip/pose"},
    // VR-native: right trigger = attack, right grip = aim, left trigger = DEFENSE/QTE
    {"trigger", "/user/hand/left/input/trigger/value"},
    {"trigger", "/user/hand/right/input/trigger/value"},
    {"grip", "/user/hand/left/input/squeeze/value"},
    {"grip", "/user/hand/right/input/squeeze/value"},
    // Xbox LB = sub weapon (SUPPORT_HOLD) and UI tab left
    {"grip", "/user/hand/left/input/bumper/click"},
    // Xbox RB in menus = tab right. REFramework sends UI_SHIFT_RIGHT only from
    // grip/right (together with HOLD), so RB must drive grip/right too.
    {"grip", "/user/hand/right/input/bumper/click"},
    // Xbox RB = change ammo
    {"re2_change_ammo", "/user/hand/right/input/bumper/click"},
    {"joystick", "/user/hand/left/input/thumbstick"},
    {"joystick", "/user/hand/right/input/thumbstick"},
    // L3 = run (JOG1), R3 = reset camera
    {"joystickclick", "/user/hand/left/input/thumbstick/click"},
    {"joystickclick", "/user/hand/right/input/thumbstick/click"},
    {"re2_reset_view", "/user/hand/right/input/thumbstick/click"},
    // Xbox A = interact (abutton/right), Xbox X = reload (bbutton/right).
    // Xbox B and Y (abutton/left, bbutton/left) are injected, see kInjects.
    {"abutton", "/user/hand/right/input/a/click"},
    {"re3_dodge", "/user/hand/right/input/a/click"},  // same as REFramework's Touch default
    {"bbutton", "/user/hand/right/input/x/click"},
    // D-pad = weapon shortcuts / UI
    {"dpad_up", "/user/hand/left/input/dpad_up/click"},
    {"dpad_down", "/user/hand/left/input/dpad_down/click"},
    {"dpad_left", "/user/hand/left/input/dpad_left/click"},
    {"dpad_right", "/user/hand/left/input/dpad_right/click"},
    // Menu = pause, View = map
    {"systembutton", "/user/hand/right/input/menu/click"},
    {"minimap", "/user/hand/left/input/view/click"},
    // weapondial_start is NOT bound: while it is held, RE2 ignores the D-pad.
    {"haptic", "/user/hand/left/output/haptic"},
    {"haptic", "/user/hand/right/output/haptic"},
};

// Right-hand Frame buttons injected into a LEFT-hand app action.
struct Inject {
    const char* own_name;     // INI name and OpenXR layer action name
    const char* own_path;     // physical Frame path
    const char* app_action;   // app action to OR into (left hand)
    const char* ovr_carrier;  // OpenVR: unused app action that carries the button
};
static const Inject kInjects[] = {
    {"frame_b", "/user/hand/right/input/b/click", "abutton", "block"},  // Xbox B = back / CANCEL
    {"frame_y", "/user/hand/right/input/y/click", "bbutton", "heal"},   // Xbox Y = INVENTORY
};
static const int kInjectCount = sizeof(kInjects) / sizeof(kInjects[0]);

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static std::recursive_mutex g_mtx;
static bool g_process_allowed = false;

// ---------------------------------------------------------------------------
// Logging (file next to the DLL, flushed per line)
// ---------------------------------------------------------------------------
static std::wstring module_dir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&module_dir, &self);
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(self, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : s.substr(0, p);
}

static std::wstring exe_dir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : s.substr(0, p);
}

static void logf(const char* fmt, ...) {
#if DEBUG_LOG
    static FILE* f = nullptr;
    static bool tried = false;
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (!tried) {
        tried = true;
        std::wstring path = module_dir() + L"\\" BASE_NAME "_log.txt";
        f = _wfopen(path.c_str(), L"w");
    }
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d [FRAME] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
#else
    (void)fmt;
#endif
}

static bool process_allowed() {
    char env[8] = {};
    if (GetEnvironmentVariableA(ENV_FORCE, env, sizeof(env)) > 0 && env[0] == '1') return true;
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    std::wstring exe = p == std::wstring::npos ? s : s.substr(p + 1);
    std::string a;
    for (wchar_t c : exe) a += (char)(c < 128 ? tolower((int)c) : '?');
    for (const char* x : kAllowedExe)
        if (a == x) return true;
    return false;
}

static std::string trim(const std::string& x) {
    size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : x.substr(a, b - a + 1);
}

static const char* dir8(float x, float y) {
    if (x * x + y * y < 0.25f) return "center";
    static const char* names[8] = {"right", "up-right", "up", "up-left", "left", "down-left", "down", "down-right"};
    float a = atan2f(y, x);
    int idx = (int)floorf((a + 3.14159265f / 8.0f) / (3.14159265f / 4.0f));
    return names[((idx % 8) + 8) % 8];
}

// ---------------------------------------------------------------------------
// Binding list = built-in defaults, then overrides from re2_frame_controllers.ini
// (next to the DLL). An action listed in the INI replaces ALL its defaults.
// Used by both runtimes.
//   [bindings]
//   re2_change_ammo = /user/hand/right/input/bumper/click
//   re2_toggle_flashlight =        (empty = unbound, the default)
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::string, std::string>> g_binds;

static void load_options();
static void build_binds() {
    load_options();
    g_binds.clear();
    for (const auto& b : kFrameBinds) g_binds.push_back({b.action, b.path});
    for (int i = 0; i < kInjectCount; ++i) g_binds.push_back({kInjects[i].own_name, kInjects[i].own_path});

    std::wstring ini = module_dir() + L"\\" BASE_NAME ".ini";
    FILE* f = _wfopen(ini.c_str(), L"r");
    if (!f) {
        logf("No " BASE_NAME ".ini, using built-in bindings");
        return;
    }
    char line[1024];
    bool in_bindings = false;
    while (fgets(line, sizeof(line), f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') { in_bindings = (l == "[bindings]"); continue; }
        if (!in_bindings) continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string action = trim(l.substr(0, eq)), rest = l.substr(eq + 1);
        std::transform(action.begin(), action.end(), action.begin(), ::tolower);
        size_t sc = rest.find(';');
        if (sc != std::string::npos) rest = rest.substr(0, sc);
        g_binds.erase(std::remove_if(g_binds.begin(), g_binds.end(),
                                     [&](const std::pair<std::string, std::string>& b) { return b.first == action; }),
                      g_binds.end());
        std::string paths;
        size_t start = 0;
        while (start <= rest.size()) {
            size_t c = rest.find(',', start);
            std::string pth = trim(rest.substr(start, c == std::string::npos ? std::string::npos : c - start));
            if (pth.rfind("/user/hand/", 0) == 0) {
                g_binds.push_back({action, pth});
                paths += " " + pth;
            } else if (!pth.empty()) {
                logf("INI: ignored bad path '%s' for %s", pth.c_str(), action.c_str());
            }
            if (c == std::string::npos) break;
            start = c + 1;
        }
        logf("INI: %s ->%s", action.c_str(), paths.empty() ? " (unbound)" : paths.c_str());
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// [options] in the INI
// ---------------------------------------------------------------------------
struct Options {
    bool trigger_aims = false;       // right trigger also aims (HOLD), no grip needed to shoot
    bool menu_dpad_stick = true;     // in menus (no player, or a GUI open except the map) the D-pad also moves the left stick
    bool probe_pause = false;        // diagnostic: log bool getters of GUI/pause singletons when they change
    bool overlay_pointer = true;     // OpenVR: point the right controller at the REFramework menu, trigger = click
    float pointer_scroll = 0.2f;     // OpenVR menu: right stick scroll speed (0 = off)
    bool pointer_keep_input = true;  // keep SteamVR from taking the controllers when the menu turns interactive
    bool pointer_hold_menu = false;  // keep the REFramework menu open while the right controller points at it
    bool pointer_fix_cursor = true;  // stop the desktop mouse position from overriding the pointer in ImGui
    int pointer_uv_mode = 0;         // 0 = hit UV over the whole texture (verified in game), 1 = over the shown part
    unsigned fire_delay_ms = 150;    // first shot waits this long so the weapon is raised
    unsigned aim_hold_ms = 800;      // keep aiming this long after the trigger is released
};
static Options g_opt;

static void load_options() {
    Options o;
    std::wstring ini = module_dir() + L"\\" BASE_NAME ".ini";
    FILE* f = _wfopen(ini.c_str(), L"r");
    if (f) {
        char line[512];
        bool in_opt = false;
        while (fgets(line, sizeof(line), f)) {
            std::string l = trim(line);
            if (l.empty() || l[0] == ';' || l[0] == '#') continue;
            if (l[0] == '[') { in_opt = (l == "[options]"); continue; }
            if (!in_opt) continue;
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(l.substr(0, eq)), v = l.substr(eq + 1);
            size_t sc = v.find(';');
            if (sc != std::string::npos) v = v.substr(0, sc);
            unsigned n = (unsigned)strtoul(trim(v).c_str(), nullptr, 10);
            if (k == "trigger_aims") o.trigger_aims = n != 0;
            else if (k == "fire_delay_ms") o.fire_delay_ms = n;
            else if (k == "aim_hold_ms") o.aim_hold_ms = n;
            else if (k == "menu_dpad_stick") o.menu_dpad_stick = n != 0;
            else if (k == "probe_pause") o.probe_pause = n != 0;
            else if (k == "overlay_pointer") o.overlay_pointer = n != 0;
            else if (k == "pointer_keep_input") o.pointer_keep_input = n != 0;
            else if (k == "pointer_uv_mode") o.pointer_uv_mode = (int)n;
            else if (k == "pointer_hold_menu") o.pointer_hold_menu = n != 0;
            else if (k == "pointer_fix_cursor") o.pointer_fix_cursor = n != 0;
            else if (k == "pointer_scroll") o.pointer_scroll = (float)atof(trim(v).c_str());
        }
        fclose(f);
    }
    g_opt = o;
    logf("Options: trigger_aims=%d fire_delay_ms=%u aim_hold_ms=%u menu_dpad_stick=%d overlay_pointer=%d pointer_scroll=%.2f",
         (int)o.trigger_aims, o.fire_delay_ms, o.aim_hold_ms, (int)o.menu_dpad_stick, (int)o.overlay_pointer, o.pointer_scroll);
}

// RE2 only fires (ATTACK) while aiming (HOLD), and REFramework sends HOLD only
// from grip/right. With trigger_aims the right trigger also drives grip/right;
// the first trigger/right press is delayed until the weapon is raised.
struct AimLogic {
    bool raw_trig_prev = false, aim_at_press = false;
    ULONGLONG press = 0, last = 0;
    bool grip_out = false, grip_prev = false, trig_out = false, trig_prev = false;

    void update(bool raw_trig, bool raw_grip) {
        grip_prev = grip_out;
        trig_prev = trig_out;
        if (!g_opt.trigger_aims) {
            grip_out = raw_grip;
            trig_out = raw_trig;
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (raw_trig && !raw_trig_prev) {
            press = now;
            aim_at_press = grip_out;
#if DEBUG_LOG
            logf("aim: trigger press, already aiming: %s", aim_at_press ? "yes (fire now)" : "no (raise first)");
#endif
        }
        if (raw_trig) last = now;
        const bool linger = last != 0 && (now - last) < g_opt.aim_hold_ms;
        grip_out = raw_grip || raw_trig || linger;
        trig_out = raw_trig && (aim_at_press || (now - press) >= g_opt.fire_delay_ms);
        raw_trig_prev = raw_trig;
#if DEBUG_LOG
        if (trig_out && !trig_prev) logf("aim: fire (trigger/right DOWN to the app)");
        if (!grip_out && grip_prev) logf("aim: aim released (grip/right UP to the app)");
#endif
    }
};
static AimLogic g_aim;

// Title menu (NEW GAME / LOAD GAME ...) does not react to REFramework's D-pad
// bits, but it does follow the left stick. While the game has no player
// (set from the game thread, see game_probe), the D-pad also drives the left
// stick. In gameplay the player exists, so the character never moves from it.
static std::atomic<bool> g_no_player{false};
static std::atomic<bool> g_probe_ok{false};
struct DpadState {
    bool up = false, down = false, left = false, right = false;
    bool any() const { return up || down || left || right; }
    void vec(float& x, float& y) const {
        x = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
        y = (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f);
    }
};
static DpadState g_dpad;
static std::atomic<bool> g_gui_menu{false};
static bool menu_stick_active() { return g_opt.menu_dpad_stick && (g_no_player.load() || g_gui_menu.load()) && g_dpad.any(); }

// ===========================================================================
// OpenXR API layer
// ===========================================================================
namespace xr {

static bool g_active = false;
static bool g_frame_enabled = false;
static bool g_app_suggests_frame = false;

static XrInstance g_instance = XR_NULL_HANDLE;
static XrPath g_left = XR_NULL_PATH, g_right = XR_NULL_PATH, g_frame_profile = XR_NULL_PATH;
static XrPath g_spoof_profile = XR_NULL_PATH;
static bool g_spoof_logged = false;

static XrActionSet g_own_set = XR_NULL_HANDLE;
static XrAction g_own_actions[kInjectCount] = {};

struct AppAction {
    std::string name;
    XrActionType type;
};
static std::map<XrAction, AppAction> g_app_actions;
static std::map<std::string, XrAction> g_app_by_name;

struct InjectState {
    XrAction app = XR_NULL_HANDLE;
    bool merged = false, prev = false, own = false;
    bool valid = false;
};
static InjectState g_inj[kInjectCount];

static std::map<std::pair<XrAction, XrPath>, bool> g_dbg_prev;
static std::map<std::pair<XrAction, XrPath>, std::string> g_dbg_stick;

static PFN_xrGetInstanceProcAddr next_gipa = nullptr;
static PFN_xrDestroyInstance next_DestroyInstance = nullptr;
static PFN_xrStringToPath next_StringToPath = nullptr;
static PFN_xrPathToString next_PathToString = nullptr;
static PFN_xrCreateActionSet next_CreateActionSet = nullptr;
static PFN_xrCreateAction next_CreateAction = nullptr;
static PFN_xrSuggestInteractionProfileBindings next_Suggest = nullptr;
static PFN_xrAttachSessionActionSets next_Attach = nullptr;
static PFN_xrSyncActions next_Sync = nullptr;
static PFN_xrGetActionStateBoolean next_GetBool = nullptr;
static PFN_xrPollEvent next_PollEvent = nullptr;
static PFN_xrGetCurrentInteractionProfile next_GetCurProfile = nullptr;
static PFN_xrGetActionStateVector2f next_GetVec2 = nullptr;
static PFN_xrCreateActionSpace next_CreateActionSpace = nullptr;
static PFN_xrLocateSpace next_LocateSpace = nullptr;
static PFN_xrDestroySpace next_DestroySpace = nullptr;

[[maybe_unused]] static std::string path_str(XrPath p) {
    if (!next_PathToString || p == XR_NULL_PATH) return "<null>";
    char buf[XR_MAX_PATH_LENGTH] = {};
    uint32_t n = 0;
    if (next_PathToString(g_instance, p, sizeof(buf), &n, buf) != XR_SUCCESS) return "<bad>";
    return buf;
}

static XrPath to_path(const char* s) {
    XrPath p = XR_NULL_PATH;
    if (next_StringToPath(g_instance, s, &p) != XR_SUCCESS) return XR_NULL_PATH;
    return p;
}

static XrResult suggest_list(const std::vector<XrActionSuggestedBinding>& v) {
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = g_frame_profile;
    sb.countSuggestedBindings = (uint32_t)v.size();
    sb.suggestedBindings = v.data();
    return next_Suggest(g_instance, &sb);
}

static void suggest_frame(bool final_call) {
    if (!g_frame_enabled || g_app_suggests_frame || g_frame_profile == XR_NULL_PATH) return;

    std::vector<XrActionSuggestedBinding> v;
    std::vector<std::string> names;
    for (const auto& b : g_binds) {
        XrAction a = XR_NULL_HANDLE;
        for (int i = 0; i < kInjectCount; ++i)
            if (b.first == kInjects[i].own_name) a = g_own_actions[i];
        if (!a) {
            auto it = g_app_by_name.find(b.first);
            if (it != g_app_by_name.end()) a = it->second;
        }
        if (!a) {
            if (final_call) logf("  not bound (app has no action '%s'): %s", b.first.c_str(), b.second.c_str());
            continue;
        }
        XrPath p = to_path(b.second.c_str());
        if (p == XR_NULL_PATH) {
            if (final_call) logf("  rejected: bad path %s", b.second.c_str());
            continue;
        }
        v.push_back({a, p});
        names.push_back(b.first + " <- " + b.second);
    }
    if (v.empty()) return;

    XrResult r = suggest_list(v);
    if (r == XR_SUCCESS) {
        if (final_call) {
            logf("Frame suggestion OK: %u bindings", (unsigned)v.size());
            for (auto& n : names) logf("  bound: %s", n.c_str());
        }
        return;
    }

    logf("Frame suggestion of %u bindings failed (%d), checking each", (unsigned)v.size(), (int)r);
    std::vector<XrActionSuggestedBinding> good;
    for (size_t i = 0; i < v.size(); ++i) {
        XrResult ri = suggest_list({v[i]});
        if (ri == XR_SUCCESS)
            good.push_back(v[i]);
        else
            logf("  rejected: %s (%d)", names[i].c_str(), (int)ri);
    }
    r = good.empty() ? XR_ERROR_VALIDATION_FAILURE : suggest_list(good);
    logf("Frame suggestion retry with %u bindings: %d", (unsigned)good.size(), (int)r);
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_CreateAction(XrActionSet set, const XrActionCreateInfo* ci, XrAction* out) {
    XrResult r = next_CreateAction(set, ci, out);
    if (r == XR_SUCCESS && ci && out && set != g_own_set) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        g_app_actions[*out] = {ci->actionName, ci->actionType};
        g_app_by_name[ci->actionName] = *out;
        for (int i = 0; i < kInjectCount; ++i)
            if (strcmp(ci->actionName, kInjects[i].app_action) == 0) {
                g_inj[i].app = *out;
                g_inj[i].valid = ci->actionType == XR_ACTION_TYPE_BOOLEAN_INPUT;
            }
    }
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_Suggest(XrInstance inst, const XrInteractionProfileSuggestedBinding* sb) {
    XrResult r = next_Suggest(inst, sb);
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (sb && sb->interactionProfile == g_frame_profile && g_frame_profile != XR_NULL_PATH) {
        if (!g_app_suggests_frame) logf("App suggests the Frame profile itself; layer stops adding its own");
        g_app_suggests_frame = true;
        return r;
    }
    suggest_frame(false);
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_Attach(XrSession s, const XrSessionActionSetsAttachInfo* ai) {
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (!g_frame_enabled || !ai) return next_Attach(s, ai);

    suggest_frame(true);  // final, complete suggestion before attach
    if (!g_own_set) return next_Attach(s, ai);

    std::vector<XrActionSet> sets(ai->actionSets, ai->actionSets + ai->countActionSets);
    sets.push_back(g_own_set);
    XrSessionActionSetsAttachInfo a2 = *ai;
    a2.countActionSets = (uint32_t)sets.size();
    a2.actionSets = sets.data();
    XrResult r = next_Attach(s, &a2);
    logf("xrAttachSessionActionSets with layer set: %d", (int)r);
    if (r != XR_SUCCESS) {
        logf("Attach with layer set failed, retrying without it (B/Y injection off)");
        g_own_set = XR_NULL_HANDLE;
        r = next_Attach(s, ai);
    }
    return r;
}

static XrBool32 raw_bool(XrSession s, XrAction a, XrPath sub) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    gi.subactionPath = sub;
    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (next_GetBool(s, &gi, &st) != XR_SUCCESS) return XR_FALSE;
    return st.isActive && st.currentState;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_Sync(XrSession s, const XrActionsSyncInfo* si) {
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    XrResult r;
    if (g_frame_enabled && g_own_set && si) {
        std::vector<XrActiveActionSet> v(si->activeActionSets, si->activeActionSets + si->countActiveActionSets);
        v.push_back({g_own_set, XR_NULL_PATH});
        XrActionsSyncInfo s2 = *si;
        s2.countActiveActionSets = (uint32_t)v.size();
        s2.activeActionSets = v.data();
        r = next_Sync(s, &s2);
    } else {
        r = next_Sync(s, si);
    }
    if (r != XR_SUCCESS) return r;  // XR_SESSION_NOT_FOCUSED etc.: keep last states

    if (g_frame_enabled) {
        auto rd = [&](const char* n) {
            auto it = g_app_by_name.find(n);
            return it != g_app_by_name.end() && raw_bool(s, it->second, g_left) == XR_TRUE;
        };
        g_dpad = {rd("dpad_up"), rd("dpad_down"), rd("dpad_left"), rd("dpad_right")};
    }
    if (g_frame_enabled) {
        auto g = g_app_by_name.find("grip"), t = g_app_by_name.find("trigger");
        if (g != g_app_by_name.end() && t != g_app_by_name.end())
            g_aim.update(raw_bool(s, t->second, g_right) == XR_TRUE, raw_bool(s, g->second, g_right) == XR_TRUE);
    }

    if (g_own_set) {
        for (int i = 0; i < kInjectCount; ++i) {
            InjectState& st = g_inj[i];
            if (!st.valid || !g_own_actions[i]) continue;
            bool own = raw_bool(s, g_own_actions[i], XR_NULL_PATH) == XR_TRUE;
            bool app = raw_bool(s, st.app, g_left) == XR_TRUE;
            st.prev = st.merged;
            st.own = own;
            st.merged = own || app;
        }
    }

#if DEBUG_LOG
    for (auto& kv : g_app_actions) {
        if (kv.second.type != XR_ACTION_TYPE_VECTOR2F_INPUT || !next_GetVec2) continue;
        for (XrPath hand : {g_left, g_right}) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = kv.first;
            gi.subactionPath = hand;
            XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (next_GetVec2(s, &gi, &v) != XR_SUCCESS) continue;
            float x = v.isActive ? v.currentState.x : 0.0f, y = v.isActive ? v.currentState.y : 0.0f;
            const char* dir = dir8(x, y);
            std::string& prev = g_dbg_stick[{kv.first, hand}];
            if (prev != dir && !(prev.empty() && strcmp(dir, "center") == 0))
                logf("xr stick %s/%s %s (x=%.2f y=%.2f active=%d)", kv.second.name.c_str(), hand == g_left ? "left" : "right",
                     dir, x, y, (int)v.isActive);
            prev = dir;
        }
    }
    for (auto& kv : g_app_actions) {
        if (kv.second.type != XR_ACTION_TYPE_BOOLEAN_INPUT) continue;
        for (XrPath hand : {g_left, g_right}) {
            bool now = raw_bool(s, kv.first, hand) == XR_TRUE;
            bool& prev = g_dbg_prev[{kv.first, hand}];
            if (now != prev)
                logf("xr raw %s/%s %s", kv.second.name.c_str(), hand == g_left ? "left" : "right", now ? "DOWN" : "UP");
            prev = now;
        }
    }
    for (int i = 0; i < kInjectCount && g_own_set; ++i) {
        bool& prev = g_dbg_prev[{g_own_actions[i], XR_NULL_PATH}];
        if (g_inj[i].own != prev)
            logf("xr raw %s %s -> %s/left", kInjects[i].own_name, g_inj[i].own ? "DOWN" : "UP", kInjects[i].app_action);
        prev = g_inj[i].own;
    }
#endif
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_GetBool(XrSession s, const XrActionStateGetInfo* gi, XrActionStateBoolean* st) {
    XrResult r = next_GetBool(s, gi, st);
    if (r != XR_SUCCESS || !gi || !st) return r;

    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (g_frame_enabled && g_opt.trigger_aims && gi->subactionPath == g_right) {
        auto it = g_app_actions.find(gi->action);
        if (it != g_app_actions.end() && (it->second.name == "grip" || it->second.name == "trigger")) {
            const bool grip = it->second.name == "grip";
            const bool out = grip ? g_aim.grip_out : g_aim.trig_out, prev = grip ? g_aim.grip_prev : g_aim.trig_prev;
            st->isActive = XR_TRUE;
            st->currentState = out ? XR_TRUE : XR_FALSE;
            st->changedSinceLastSync = out != prev ? XR_TRUE : XR_FALSE;
            return r;
        }
    }
    if (!g_own_set || gi->subactionPath != g_left) return r;
    for (int i = 0; i < kInjectCount; ++i) {
        const InjectState& in = g_inj[i];
        if (!in.valid || gi->action != in.app) continue;
        // Override while our button is down AND on the release frame, so the
        // app also sees the UP edge (changedSinceLastSync) of an injected press.
        if (!in.own && in.merged == in.prev) continue;
        st->isActive = XR_TRUE;
        st->currentState = in.merged ? XR_TRUE : XR_FALSE;
        st->changedSinceLastSync = (in.merged != in.prev) ? XR_TRUE : XR_FALSE;
    }
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_GetVec2(XrSession s, const XrActionStateGetInfo* gi, XrActionStateVector2f* st) {
    XrResult r = next_GetVec2(s, gi, st);
    if (r != XR_SUCCESS || !gi || !st || gi->subactionPath != g_left || !g_frame_enabled) return r;
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (!menu_stick_active()) return r;
    auto it = g_app_actions.find(gi->action);
    if (it == g_app_actions.end() || it->second.name != "joystick") return r;
    float x, y;
    g_dpad.vec(x, y);
    if (st->isActive && st->currentState.x * st->currentState.x + st->currentState.y * st->currentState.y > 0.25f) return r;
    st->isActive = XR_TRUE;
    st->currentState = {x, y};
    st->changedSinceLastSync = XR_TRUE;
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_PollEvent(XrInstance inst, XrEventDataBuffer* ev) {
    XrResult r = next_PollEvent(inst, ev);
    if (r == XR_SUCCESS && ev && ev->type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
        auto* e = reinterpret_cast<XrEventDataInteractionProfileChanged*>(ev);
        for (XrPath hand : {g_left, g_right}) {
            XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
            XrResult rr = next_GetCurProfile(e->session, hand, &ps);
            logf("INTERACTION_PROFILE_CHANGED %s -> %s (%d)", hand == g_left ? "left" : "right",
                 path_str(ps.interactionProfile).c_str(), (int)rr);
        }
    }
    return r;
}

// Grip pose correction (INI [pose], live reload). new_pose = grip_pose * offset.
// Rotation in degrees: pitch (local X), yaw (Y), roll (Z), applied yaw*pitch*roll.
// Position in centimeters along local X Y Z.
struct PoseOffset {
    float pitch = 0, yaw = 0, roll = 0;
    float x = 0, y = 0, z = 0;
    XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
    bool identity = true;
};
static PoseOffset g_off[2];
static std::map<XrSpace, int> g_hand_spaces;
static ULONGLONG g_ini_check_ms = 0;
static FILETIME g_ini_mtime{};

static XrQuaternionf qmul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static XrQuaternionf qaxis(float ax, float ay, float az, float deg) {
    float h = deg * 3.14159265f / 360.0f, sn = sinf(h);
    return {ax * sn, ay * sn, az * sn, cosf(h)};
}
static XrVector3f qrot(const XrQuaternionf& q, const XrVector3f& v) {
    XrQuaternionf p{v.x, v.y, v.z, 0}, qc{-q.x, -q.y, -q.z, q.w};
    XrQuaternionf r = qmul(qmul(q, p), qc);
    return {r.x, r.y, r.z};
}
static void finalize_offset(PoseOffset& o) {
    XrQuaternionf q = qmul(qmul(qaxis(0, 1, 0, o.yaw), qaxis(1, 0, 0, o.pitch)), qaxis(0, 0, 1, o.roll));
    o.pose.orientation = q;
    o.pose.position = {o.x / 100.0f, o.y / 100.0f, o.z / 100.0f};
    o.identity = o.pitch == 0 && o.yaw == 0 && o.roll == 0 && o.x == 0 && o.y == 0 && o.z == 0;
}

static void load_pose_offsets() {
    PoseOffset n[2];
    std::wstring ini = module_dir() + L"\\" BASE_NAME ".ini";
    FILE* f = _wfopen(ini.c_str(), L"r");
    if (f) {
        char line[512];
        bool in_pose = false;
        while (fgets(line, sizeof(line), f)) {
            std::string l = trim(line);
            if (l.empty() || l[0] == ';' || l[0] == '#') continue;
            if (l[0] == '[') { in_pose = (l == "[pose]"); continue; }
            if (!in_pose) continue;
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(l.substr(0, eq)), v = l.substr(eq + 1);
            size_t sc = v.find(';');
            if (sc != std::string::npos) v = v.substr(0, sc);
            float a = 0, b = 0, c = 0;
            if (sscanf(v.c_str(), " %f %f %f", &a, &b, &c) != 3) {
                logf("INI [pose]: '%s' needs 3 numbers", k.c_str());
                continue;
            }
            int h = k.rfind("left_", 0) == 0 ? 0 : (k.rfind("right_", 0) == 0 ? 1 : -1);
            if (h < 0) continue;
            if (k.find("rotation") != std::string::npos) { n[h].pitch = a; n[h].yaw = b; n[h].roll = c; }
            else if (k.find("position") != std::string::npos) { n[h].x = a; n[h].y = b; n[h].z = c; }
        }
        fclose(f);
    }
    for (int h = 0; h < 2; ++h) finalize_offset(n[h]);
    g_off[0] = n[0];
    g_off[1] = n[1];
    logf("Pose offset L: rot %.1f %.1f %.1f deg, pos %.1f %.1f %.1f cm | R: rot %.1f %.1f %.1f deg, pos %.1f %.1f %.1f cm",
         n[0].pitch, n[0].yaw, n[0].roll, n[0].x, n[0].y, n[0].z, n[1].pitch, n[1].yaw, n[1].roll, n[1].x, n[1].y, n[1].z);
}

static void maybe_reload_pose_offsets() {
    ULONGLONG now = GetTickCount64();
    if (now - g_ini_check_ms < 1000) return;
    g_ini_check_ms = now;
    std::wstring ini = module_dir() + L"\\" BASE_NAME ".ini";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(ini.c_str(), GetFileExInfoStandard, &fa)) return;
    if (CompareFileTime(&fa.ftLastWriteTime, &g_ini_mtime) != 0) {
        g_ini_mtime = fa.ftLastWriteTime;
        load_pose_offsets();
    }
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_CreateActionSpace(XrSession s, const XrActionSpaceCreateInfo* ci, XrSpace* out) {
    XrResult r = next_CreateActionSpace(s, ci, out);
    if (r == XR_SUCCESS && ci && out) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        auto it = g_app_actions.find(ci->action);
        if (it != g_app_actions.end() && it->second.type == XR_ACTION_TYPE_POSE_INPUT &&
            (ci->subactionPath == g_left || ci->subactionPath == g_right)) {
            g_hand_spaces[*out] = ci->subactionPath == g_left ? 0 : 1;
            logf("Hand pose space %s registered (%s)", ci->subactionPath == g_left ? "left" : "right", it->second.name.c_str());
        }
    }
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_DestroySpace(XrSpace sp) {
    {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        g_hand_spaces.erase(sp);
    }
    return next_DestroySpace(sp);
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_LocateSpace(XrSpace sp, XrSpace base, XrTime t, XrSpaceLocation* loc) {
    XrResult r = next_LocateSpace(sp, base, t, loc);
    if (r != XR_SUCCESS || !loc) return r;
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    auto it = g_hand_spaces.find(sp);
    if (it == g_hand_spaces.end()) return r;
    maybe_reload_pose_offsets();
    const PoseOffset& o = g_off[it->second];
    if (o.identity || !(loc->locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) return r;

    const XrQuaternionf q = loc->pose.orientation;
    const XrVector3f d = qrot(q, o.pose.position);
    loc->pose.position = {loc->pose.position.x + d.x, loc->pose.position.y + d.y, loc->pose.position.z + d.z};
    loc->pose.orientation = qmul(q, o.pose.orientation);

    for (auto* n = (XrBaseOutStructure*)loc->next; n; n = n->next) {
        if (n->type != XR_TYPE_SPACE_VELOCITY) continue;
        auto* v = (XrSpaceVelocity*)n;
        if ((v->velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) && (v->velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)) {
            const XrVector3f w = v->angularVelocity;
            v->linearVelocity.x += w.y * d.z - w.z * d.y;
            v->linearVelocity.y += w.z * d.x - w.x * d.z;
            v->linearVelocity.z += w.x * d.y - w.y * d.x;
        }
    }
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_GetCurProfile(XrSession s, XrPath user, XrInteractionProfileState* st) {
    XrResult r = next_GetCurProfile(s, user, st);
    if (r == XR_SUCCESS && st && g_frame_profile != XR_NULL_PATH && st->interactionProfile == g_frame_profile &&
        g_spoof_profile != XR_NULL_PATH && !g_app_suggests_frame) {
        st->interactionProfile = g_spoof_profile;
        if (!g_spoof_logged) {
            g_spoof_logged = true;
            logf("Reporting %s to the app while the runtime uses the Frame profile (stick fix)", SPOOF_PROFILE);
        }
    }
    return r;
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_DestroyInstance(XrInstance inst) {
    {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        logf("xrDestroyInstance");
        g_own_set = XR_NULL_HANDLE;
        g_app_actions.clear();
        g_app_by_name.clear();
        g_dbg_prev.clear();
        g_dbg_stick.clear();
        for (auto& i : g_inj) i = InjectState{};
        g_instance = XR_NULL_HANDLE;
        g_frame_enabled = false;
        g_app_suggests_frame = false;
    }
    return next_DestroyInstance(inst);
}

static XRAPI_ATTR XrResult XRAPI_CALL layer_gipa(XrInstance inst, const char* name, PFN_xrVoidFunction* fn) {
    if (!next_gipa) return XR_ERROR_HANDLE_INVALID;
    XrResult r = next_gipa(inst, name, fn);
    if (r != XR_SUCCESS || !g_active || !name) return r;

#define HOOK(n, h) \
    if (strcmp(name, n) == 0) { *fn = (PFN_xrVoidFunction)h; return XR_SUCCESS; }
    HOOK("xrGetInstanceProcAddr", layer_gipa)
    HOOK("xrDestroyInstance", hk_DestroyInstance)
    HOOK("xrCreateAction", hk_CreateAction)
    HOOK("xrSuggestInteractionProfileBindings", hk_Suggest)
    HOOK("xrAttachSessionActionSets", hk_Attach)
    HOOK("xrSyncActions", hk_Sync)
    HOOK("xrGetActionStateBoolean", hk_GetBool)
    HOOK("xrGetActionStateVector2f", hk_GetVec2)
    HOOK("xrPollEvent", hk_PollEvent)
    HOOK("xrGetCurrentInteractionProfile", hk_GetCurProfile)
    HOOK("xrCreateActionSpace", hk_CreateActionSpace)
    HOOK("xrLocateSpace", hk_LocateSpace)
    HOOK("xrDestroySpace", hk_DestroySpace)
#undef HOOK
    return r;
}

static bool runtime_has_ext(PFN_xrGetInstanceProcAddr gipa, const char* ext) {
    PFN_xrEnumerateInstanceExtensionProperties en = nullptr;
    if (gipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction*)&en) != XR_SUCCESS || !en)
        return false;
    uint32_t n = 0;
    if (en(nullptr, 0, &n, nullptr) != XR_SUCCESS) return false;
    std::vector<XrExtensionProperties> props(n, {XR_TYPE_EXTENSION_PROPERTIES});
    if (en(nullptr, n, &n, props.data()) != XR_SUCCESS) return false;
    for (auto& p : props)
        if (strcmp(p.extensionName, ext) == 0) return true;
    return false;
}

#define LOAD(var, n) next_gipa(*instance, n, (PFN_xrVoidFunction*)&var)

static XRAPI_ATTR XrResult XRAPI_CALL layer_create_instance(const XrInstanceCreateInfo* info,
                                                           const XrApiLayerCreateInfo* li, XrInstance* instance) {
    if (!li || li->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO || !li->nextInfo ||
        li->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
        !li->nextInfo->nextGetInstanceProcAddr || !li->nextInfo->nextCreateApiLayerInstance)
        return XR_ERROR_INITIALIZATION_FAILED;

    next_gipa = li->nextInfo->nextGetInstanceProcAddr;
    XrApiLayerCreateInfo next_li = *li;
    next_li.nextInfo = li->nextInfo->next;

    g_active = process_allowed();
    if (!g_active) return li->nextInfo->nextCreateApiLayerInstance(info, &next_li, instance);

    logf("[OpenXR] %s loaded, app '%s', api 0x%llx", LAYER_NAME, info ? info->applicationInfo.applicationName : "?",
         info ? (unsigned long long)info->applicationInfo.apiVersion : 0ULL);

    bool app_requested = false;
    std::vector<const char*> exts;
    if (info)
        for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
            exts.push_back(info->enabledExtensionNames[i]);
            if (strcmp(info->enabledExtensionNames[i], FRAME_EXT_NAME) == 0) app_requested = true;
        }

    bool supported = runtime_has_ext(next_gipa, FRAME_EXT_NAME);
    logf("[OpenXR] Runtime offers %s: %s", FRAME_EXT_NAME, supported ? "yes" : "NO");

    XrInstanceCreateInfo ci = *info;
    bool added = false;
    if (supported && !app_requested) {
        exts.push_back(FRAME_EXT_NAME);
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.enabledExtensionNames = exts.data();
        added = true;
    }

    XrResult r = li->nextInfo->nextCreateApiLayerInstance(&ci, &next_li, instance);
    if (r != XR_SUCCESS && added) {
        logf("[OpenXR] xrCreateInstance with the Frame extension failed (%d), retrying without it", (int)r);
        added = false;
        r = li->nextInfo->nextCreateApiLayerInstance(info, &next_li, instance);
    }
    if (r != XR_SUCCESS) {
        logf("[OpenXR] xrCreateInstance failed: %d", (int)r);
        return r;
    }

    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    g_instance = *instance;
    g_frame_enabled = supported && (added || app_requested);
    g_app_suggests_frame = false;

    LOAD(next_DestroyInstance, "xrDestroyInstance");
    LOAD(next_StringToPath, "xrStringToPath");
    LOAD(next_PathToString, "xrPathToString");
    LOAD(next_CreateActionSet, "xrCreateActionSet");
    LOAD(next_CreateAction, "xrCreateAction");
    LOAD(next_Suggest, "xrSuggestInteractionProfileBindings");
    LOAD(next_Attach, "xrAttachSessionActionSets");
    LOAD(next_Sync, "xrSyncActions");
    LOAD(next_GetBool, "xrGetActionStateBoolean");
    LOAD(next_PollEvent, "xrPollEvent");
    LOAD(next_GetCurProfile, "xrGetCurrentInteractionProfile");
    LOAD(next_GetVec2, "xrGetActionStateVector2f");
    LOAD(next_CreateActionSpace, "xrCreateActionSpace");
    LOAD(next_LocateSpace, "xrLocateSpace");
    LOAD(next_DestroySpace, "xrDestroySpace");

    g_left = to_path("/user/hand/left");
    g_right = to_path("/user/hand/right");
    g_frame_profile = g_frame_enabled ? to_path(FRAME_PROFILE) : XR_NULL_PATH;
    g_spoof_profile = g_frame_enabled ? to_path(SPOOF_PROFILE) : XR_NULL_PATH;
    g_spoof_logged = false;
    logf("[OpenXR] Frame bindings: %s", g_frame_enabled ? "ON" : "OFF (pass-through)");
    if (g_frame_enabled) build_binds();
    g_hand_spaces.clear();
    g_ini_check_ms = 0;
    g_ini_mtime = FILETIME{};

    if (g_frame_enabled) {
        XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy(asci.actionSetName, "re2framelayer");
        strcpy(asci.localizedActionSetName, "RE2 Frame layer");
        asci.priority = 0;
        if (next_CreateActionSet(g_instance, &asci, &g_own_set) == XR_SUCCESS) {
            for (int i = 0; i < kInjectCount; ++i) {
                XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
                strcpy(aci.actionName, kInjects[i].own_name);
                strcpy(aci.localizedActionName, kInjects[i].own_name);
                aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                if (next_CreateAction(g_own_set, &aci, &g_own_actions[i]) != XR_SUCCESS) {
                    logf("[OpenXR] Could not create layer action %s", kInjects[i].own_name);
                    g_own_actions[i] = XR_NULL_HANDLE;
                }
            }
        } else {
            logf("[OpenXR] Could not create layer action set (B/Y injection off)");
            g_own_set = XR_NULL_HANDLE;
        }
    }
    return XR_SUCCESS;
}

}  // namespace xr

extern "C" __declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo, const char* /*layerName*/,
                                   XrNegotiateApiLayerRequest* req) {
    if (!loaderInfo || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION || !req ||
        req->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)
        return XR_ERROR_INITIALIZATION_FAILED;

    // Older loaders cap the API version at 1.0.x. Only core 1.0 functions are
    // used, so report the loader's max.
    XrVersion v = loaderInfo->maxApiVersion;
    if (v > XR_CURRENT_API_VERSION) v = XR_CURRENT_API_VERSION;

    req->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    req->layerApiVersion = v;
    req->getInstanceProcAddr = xr::layer_gipa;
    req->createApiLayerInstance = xr::layer_create_instance;
    logf("[OpenXR] loader negotiated with the layer (loader api max 0x%llx)", (unsigned long long)loaderInfo->maxApiVersion);
    return XR_SUCCESS;
}

// ===========================================================================
// OpenVR: hook IVRInput_010 (vtable) via VR_GetGenericInterface (MinHook)
// ===========================================================================
namespace ovr {

typedef void* (*PFN_GetGenericInterface)(const char*, vr::EVRInitError*);
typedef vr::EVRInputError (*PFN_SetManifest)(void*, const char*);
typedef vr::EVRInputError (*PFN_GetHandle)(void*, const char*, uint64_t*);
typedef vr::EVRInputError (*PFN_Update)(void*, vr::VRActiveActionSet_t*, uint32_t, uint32_t);
typedef vr::EVRInputError (*PFN_GetDigital)(void*, uint64_t, vr::InputDigitalActionData_t*, uint32_t, uint64_t);
typedef vr::EVRInputError (*PFN_GetAnalog)(void*, uint64_t, vr::InputAnalogActionData_t*, uint32_t, uint64_t);
typedef vr::EVRInputError (*PFN_GetOriginName)(void*, uint64_t, char*, uint32_t, int32_t);

static PFN_GetGenericInterface orig_GetGenericInterface = nullptr;
static PFN_SetManifest orig_SetManifest = nullptr;
static PFN_GetHandle orig_GetActionHandle = nullptr;
static PFN_GetHandle orig_GetSourceHandle = nullptr;
static PFN_Update orig_Update = nullptr;
static PFN_GetDigital orig_GetDigital = nullptr;
static PFN_GetAnalog orig_GetAnalog = nullptr;
static PFN_GetOriginName orig_GetOriginName = nullptr;

static void* g_input = nullptr;  // IVRInput_010 instance
static std::map<uint64_t, std::string> g_action_names;  // handle -> lower-case short name
static std::map<std::string, uint64_t> g_action_by_name;
static std::map<uint64_t, std::string> g_source_names;
static uint64_t g_left_src = 0, g_right_src = 0;

struct InjectState {
    uint64_t app = 0, carrier = 0;
    bool own = false, merged = false, prev = false;
};
static InjectState g_inj[kInjectCount];
static bool g_injection_ready = false;
static uint64_t g_grip_h = 0, g_trig_h = 0, g_joy_h = 0, g_dpad_h[4] = {}, g_heal_h = 0;
static bool g_pointer_frame = false;  // menu pointer: set once per frame in UpdateActionState
static bool g_ptr_hit = false;        // menu pointer: right controller ray is on the REFramework menu
// Pointer position in ImGui coordinates (top-left origin), read by the
// GetCursorPos hook. Written on the render thread, read on the same thread.
static volatile LONG g_ptr_imgui_x = 0, g_ptr_imgui_y = 0;
static volatile LONG g_ptr_cursor_on = 0;   // 1 = report the pointer as the cursor to REFramework
static HMODULE g_ref_module = nullptr;      // REFramework (dinput8.dll); only its calls are redirected

static std::map<std::pair<uint64_t, uint64_t>, bool> g_dbg_prev;
static std::map<std::pair<uint64_t, uint64_t>, std::string> g_dbg_stick;

static std::string short_name(const char* full) {
    std::string s = full ? full : "";
    size_t p = s.find_last_of('/');
    if (p != std::string::npos) s = s.substr(p + 1);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static const char* src_name(uint64_t h) {
    if (h == 0) return "any";
    auto it = g_source_names.find(h);
    return it == g_source_names.end() ? "?" : it->second.c_str();
}

static const char* act_name(uint64_t h) {
    auto it = g_action_names.find(h);
    return it == g_action_names.end() ? "?" : it->second.c_str();
}

// ---- Frame binding file for SteamVR, generated from g_binds ---------------
static std::string ovr_action_for(const std::string& bind_name) {
    for (int i = 0; i < kInjectCount; ++i)
        if (bind_name == kInjects[i].own_name) return kInjects[i].ovr_carrier;
    return bind_name;
}

static std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

static std::string build_frame_binding_json() {
    std::vector<std::string> sources, poses, haptics;
    for (const auto& b : g_binds) {
        const std::string action = ovr_action_for(b.first);
        const std::string& p = b.second;  // /user/hand/<h>/input/<comp>[/<sub>] or /user/hand/<h>/output/haptic
        std::string hand_prefix;
        if (p.rfind("/user/hand/left/", 0) == 0) hand_prefix = "/user/hand/left";
        else if (p.rfind("/user/hand/right/", 0) == 0) hand_prefix = "/user/hand/right";
        else { logf("[OpenVR] binding skipped (no hand): %s", p.c_str()); continue; }

        if (p == hand_prefix + "/output/haptic") {
            haptics.push_back("{ \"output\": \"/actions/default/out/" + action + "\", \"path\": \"" + p + "\" }");
            continue;
        }
        const std::string in_prefix = hand_prefix + "/input/";
        if (p.rfind(in_prefix, 0) != 0) { logf("[OpenVR] binding skipped: %s", p.c_str()); continue; }
        std::string comp = p.substr(in_prefix.size()), sub;
        size_t sl = comp.find('/');
        if (sl != std::string::npos) { sub = comp.substr(sl + 1); comp = comp.substr(0, sl); }

        if (sub == "pose") {
            poses.push_back("{ \"output\": \"/actions/default/in/" + action + "\", \"path\": \"" + hand_prefix + "/pose/raw\" }");
            continue;
        }
        if (comp == "squeeze") comp = "grip";  // OpenVR name of the Frame grip
        const std::string ovr_path = in_prefix + comp;
        std::string mode, input;
        if (comp == "thumbstick" && sub.empty()) { mode = "joystick"; input = "position"; }
        else if (comp == "thumbstick") { mode = "joystick"; input = sub == "touch" ? "touch" : "click"; }
        else { mode = "button"; input = sub == "touch" ? "touch" : "click"; }
        sources.push_back("{ \"inputs\": { \"" + input + "\": { \"output\": \"/actions/default/in/" + action +
                          "\" } }, \"mode\": \"" + mode + "\", \"path\": \"" + json_escape(ovr_path) + "\" }");
    }
    auto join = [](const std::vector<std::string>& v) {
        std::string o;
        for (size_t i = 0; i < v.size(); ++i) o += (i ? ",\n            " : "\n            ") + v[i];
        return o + "\n         ";
    };
    std::string j;
    j += "{\n   \"alias_info\": {},\n   \"bindings\": {\n      \"/actions/default\": {\n";
    j += "         \"chords\": [],\n";
    j += "         \"haptics\": [" + join(haptics) + "],\n";
    j += "         \"poses\": [" + join(poses) + "],\n";
    j += "         \"skeleton\": [\n"
         "            { \"output\": \"/actions/default/in/skeletonlefthand\", \"path\": \"/user/hand/left/input/skeleton/left\" },\n"
         "            { \"output\": \"/actions/default/in/skeletonrighthand\", \"path\": \"/user/hand/right/input/skeleton/right\" }\n"
         "         ],\n";
    j += "         \"sources\": [" + join(sources) + "]\n";
    j += "      }\n   },\n";
    j += "   \"category\": \"steamvr_input\",\n   \"controller_type\": \"frame_controller\",\n";
    j += "   \"description\": \"Steam Frame, Xbox-like layout (" BASE_NAME " " PLUGIN_VERSION ")\",\n";
    j += "   \"name\": \"frame_controller_" BASE_NAME "\",\n   \"options\": {},\n   \"simulated_actions\": []\n}\n";
    return j;
}

static bool read_file(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static bool write_file(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

static void patch_manifest(const char* manifest_path) {
    build_binds();
    std::string mp = manifest_path ? manifest_path : "";
    size_t sl = mp.find_last_of("\\/");
    std::string dir = sl == std::string::npos ? std::string(".") : mp.substr(0, sl);
    const std::string binding_file = "bindings_frame_controller.json";

    std::string bj = build_frame_binding_json();
    if (!write_file(dir + "\\" + binding_file, bj)) {
        logf("[OpenVR] could not write %s\\%s", dir.c_str(), binding_file.c_str());
        return;
    }
    logf("[OpenVR] wrote %s (%u bytes)", binding_file.c_str(), (unsigned)bj.size());

    std::string m;
    if (!read_file(mp, m)) {
        logf("[OpenVR] could not read manifest %s", mp.c_str());
        return;
    }
    if (m.find("\"frame_controller\"") != std::string::npos) {
        logf("[OpenVR] manifest already has a frame_controller binding, not changed");
        return;
    }
    size_t k = m.find("\"default_bindings\"");
    size_t br = k == std::string::npos ? std::string::npos : m.find('[', k);
    if (br == std::string::npos) {
        logf("[OpenVR] manifest has no default_bindings array, not changed");
        return;
    }
    m.insert(br + 1, "\n    {\n      \"controller_type\": \"frame_controller\",\n      \"binding_url\": \"" + binding_file +
                         "\"\n    },");
    if (write_file(mp, m))
        logf("[OpenVR] added frame_controller default binding to %s", mp.c_str());
    else
        logf("[OpenVR] could not write manifest %s", mp.c_str());
}

// ---- IVRInput hooks -------------------------------------------------------
static vr::EVRInputError hk_SetManifest(void* self, const char* path) {
    {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        logf("[OpenVR] SetActionManifestPath %s", path ? path : "<null>");
        patch_manifest(path);
    }
    vr::EVRInputError e = orig_SetManifest(self, path);
    logf("[OpenVR] SetActionManifestPath result %d", (int)e);
    return e;
}

static vr::EVRInputError hk_GetActionHandle(void* self, const char* name, uint64_t* h) {
    vr::EVRInputError e = orig_GetActionHandle(self, name, h);
    if (e == vr::VRInputError_None && h && name) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        std::string s = short_name(name);
        g_action_names[*h] = s;
        g_action_by_name[s] = *h;
    }
    return e;
}

static vr::EVRInputError hk_GetSourceHandle(void* self, const char* name, uint64_t* h) {
    vr::EVRInputError e = orig_GetSourceHandle(self, name, h);
    if (e == vr::VRInputError_None && h && name) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        std::string s = name;
        g_source_names[*h] = s == "/user/hand/left" ? "left" : (s == "/user/hand/right" ? "right" : s);
        if (s == "/user/hand/left") g_left_src = *h;
        if (s == "/user/hand/right") g_right_src = *h;
    }
    return e;
}

static uint64_t ensure_handle(void* self, const std::string& short_n) {
    auto it = g_action_by_name.find(short_n);
    if (it != g_action_by_name.end()) return it->second;
    uint64_t h = 0;
    std::string full = "/actions/default/in/" + short_n;
    if (orig_GetActionHandle(self, full.c_str(), &h) == vr::VRInputError_None && h) {
        g_action_names[h] = short_n;
        g_action_by_name[short_n] = h;
        return h;
    }
    return 0;
}

static bool digital(void* self, uint64_t action, uint64_t restrict_dev, uint64_t* origin = nullptr) {
    vr::InputDigitalActionData_t d{};
    if (orig_GetDigital(self, action, &d, sizeof(d), restrict_dev) != vr::VRInputError_None) return false;
    if (origin) *origin = d.activeOrigin;
    return d.bActive && d.bState;
}

static std::string origin_name(void* self, uint64_t origin) {
    if (!origin || !orig_GetOriginName) return "-";
    char buf[256] = {};
    // VRInputString_All = -1 (hand + controller type + input source)
    if (orig_GetOriginName(self, origin, buf, sizeof(buf), -1) != vr::VRInputError_None) return "?";
    return buf;
}

static vr::EVRInputError hk_Update(void* self, vr::VRActiveActionSet_t* sets, uint32_t sz, uint32_t count) {
    vr::EVRInputError e = orig_Update(self, sets, sz, count);
    if (e != vr::VRInputError_None) return e;
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    g_pointer_frame = true;

    if (!g_injection_ready && g_left_src) {
        bool ok = true;
        for (int i = 0; i < kInjectCount; ++i) {
            g_inj[i].app = ensure_handle(self, kInjects[i].app_action);
            g_inj[i].carrier = ensure_handle(self, kInjects[i].ovr_carrier);
            if (!g_inj[i].app || !g_inj[i].carrier) ok = false;
            logf("[OpenVR] inject %s: carrier %s (0x%llx) -> %s/left (0x%llx)", kInjects[i].own_name, kInjects[i].ovr_carrier,
                 (unsigned long long)g_inj[i].carrier, kInjects[i].app_action, (unsigned long long)g_inj[i].app);
        }
        logf("[OpenVR] B/Y injection %s", ok ? "ON" : "partly OFF (missing action handles)");
        g_grip_h = ensure_handle(self, "grip");
        g_joy_h = ensure_handle(self, "joystick");
        g_heal_h = ensure_handle(self, "heal");
        g_dpad_h[0] = ensure_handle(self, "dpad_up");
        g_dpad_h[1] = ensure_handle(self, "dpad_down");
        g_dpad_h[2] = ensure_handle(self, "dpad_left");
        g_dpad_h[3] = ensure_handle(self, "dpad_right");
        g_trig_h = ensure_handle(self, "trigger");
        g_injection_ready = true;  // resolve once; entries without handles are skipped below
    }

    if (g_left_src) {
        auto rd = [&](uint64_t h) { return h && digital(self, h, g_left_src); };
        g_dpad = {rd(g_dpad_h[0]), rd(g_dpad_h[1]), rd(g_dpad_h[2]), rd(g_dpad_h[3])};
    }
    if (g_grip_h && g_trig_h && g_right_src)
        g_aim.update(digital(self, g_trig_h, g_right_src), digital(self, g_grip_h, g_right_src));

    for (int i = 0; i < kInjectCount; ++i) {
        InjectState& st = g_inj[i];
        if (!st.app || !st.carrier) continue;
        uint64_t org = 0;
        bool own = digital(self, st.carrier, vr::k_ulInvalidInputValueHandle, &org);
        bool app = digital(self, st.app, g_left_src);
#if DEBUG_LOG
        if (own != st.own)
            logf("ovr raw %s (%s) %s -> %s/left  origin: %s", kInjects[i].own_name, kInjects[i].ovr_carrier,
                 own ? "DOWN" : "UP", kInjects[i].app_action, own ? origin_name(self, org).c_str() : "-");
#endif
        st.prev = st.merged;
        st.own = own;
        st.merged = own || app;
    }
    return e;
}

static vr::EVRInputError hk_GetDigital(void* self, uint64_t action, vr::InputDigitalActionData_t* d, uint32_t sz,
                                       uint64_t restrict_dev) {
    vr::EVRInputError e = orig_GetDigital(self, action, d, sz, restrict_dev);
    if (e != vr::VRInputError_None || !d || sz < sizeof(vr::InputDigitalActionData_t)) return e;
    std::lock_guard<std::recursive_mutex> lk(g_mtx);

#if DEBUG_LOG
    {
        bool now = d->bActive && d->bState;
        bool& prev = g_dbg_prev[{action, restrict_dev}];
        if (now != prev) {
            const char* an = act_name(action);
            if (strcmp(an, "block") != 0 && strcmp(an, "heal") != 0)  // carriers are logged in hk_Update
                logf("ovr raw %s/%s %s  origin: %s", an, src_name(restrict_dev), now ? "DOWN" : "UP",
                     now ? origin_name(self, d->activeOrigin).c_str() : "-");
        }
        prev = now;
    }
#endif

    // REFramework closes its menu unless BOTH the controller tip and the head
    // gaze hit it, but skips that check while any action is down
    // (OverlayComponent.cpp: is_any_action_down). While the pointer is on the
    // menu, report "heal" (not used by RE2) as down on the right hand.
    if (g_ptr_hit && g_opt.overlay_pointer && g_opt.pointer_hold_menu && action == g_heal_h && g_heal_h &&
        restrict_dev == g_right_src) {
        d->bActive = true;
        d->bState = true;
        d->bChanged = false;
        return e;
    }
    if (g_ptr_hit && g_opt.overlay_pointer && action == g_trig_h && g_trig_h && restrict_dev == g_right_src) {
        d->bState = false;  // trigger clicks the menu, it must not also attack
        d->bChanged = false;
        return e;
    }
    if (g_opt.trigger_aims && restrict_dev != 0 && restrict_dev == g_right_src && (action == g_grip_h || action == g_trig_h) &&
        action != 0) {
        const bool grip = action == g_grip_h;
        const bool out = grip ? g_aim.grip_out : g_aim.trig_out, prev = grip ? g_aim.grip_prev : g_aim.trig_prev;
        d->bActive = true;
        d->bState = out;
        d->bChanged = out != prev;
        return e;
    }
    if (restrict_dev == 0 || restrict_dev != g_left_src) return e;
    for (int i = 0; i < kInjectCount; ++i) {
        const InjectState& in = g_inj[i];
        if (action != in.app || !in.carrier) continue;
        if (!in.own && in.merged == in.prev) continue;  // also override on the release frame
        d->bActive = true;
        d->bState = in.merged;
        d->bChanged = in.merged != in.prev;
    }
    return e;
}

static vr::EVRInputError hk_GetAnalog(void* self, uint64_t action, vr::InputAnalogActionData_t* d, uint32_t sz,
                                      uint64_t restrict_dev) {
    vr::EVRInputError e = orig_GetAnalog(self, action, d, sz, restrict_dev);
    if (e == vr::VRInputError_None && d && sz >= sizeof(vr::InputAnalogActionData_t) && action == g_joy_h && g_joy_h &&
        restrict_dev == g_right_src && g_right_src && g_ptr_hit && g_opt.overlay_pointer && g_opt.pointer_scroll > 0.0f) {
        d->x = d->y = 0.0f;  // right stick scrolls the menu, it must not also turn the camera
    }
    if (e == vr::VRInputError_None && d && sz >= sizeof(vr::InputAnalogActionData_t) && action == g_joy_h && g_joy_h &&
        restrict_dev == g_left_src && g_left_src) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        if (menu_stick_active() && !(d->bActive && d->x * d->x + d->y * d->y > 0.25f)) {
            float x, y;
            g_dpad.vec(x, y);
            d->bActive = true;
            d->x = x;
            d->y = y;
        }
    }
#if DEBUG_LOG
    if (e == vr::VRInputError_None && d && sz >= sizeof(vr::InputAnalogActionData_t)) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        const char* an = act_name(action);
        if (strcmp(an, "joystick") == 0) {
            float x = d->bActive ? d->x : 0.0f, y = d->bActive ? d->y : 0.0f;
            const char* dir = dir8(x, y);
            std::string& prev = g_dbg_stick[{action, restrict_dev}];
            if (prev != dir && !(prev.empty() && strcmp(dir, "center") == 0))
                logf("ovr stick %s/%s %s (x=%.2f y=%.2f active=%d)", an, src_name(restrict_dev), dir, x, y, (int)d->bActive);
            prev = dir;
        }
    }
#endif
    return e;
}

static bool patch_slot(void** vt, int idx, void* hook, void** orig) {
    if (vt[idx] == hook) return true;
    DWORD old = 0;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    *orig = vt[idx];
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
    return true;
}

static void hook_input(void* iface) {
    void** vt = *(void***)iface;
    bool ok = true;
    ok &= patch_slot(vt, VT_SetActionManifestPath, (void*)hk_SetManifest, (void**)&orig_SetManifest);
    ok &= patch_slot(vt, VT_GetActionHandle, (void*)hk_GetActionHandle, (void**)&orig_GetActionHandle);
    ok &= patch_slot(vt, VT_GetInputSourceHandle, (void*)hk_GetSourceHandle, (void**)&orig_GetSourceHandle);
    ok &= patch_slot(vt, VT_UpdateActionState, (void*)hk_Update, (void**)&orig_Update);
    ok &= patch_slot(vt, VT_GetDigitalActionData, (void*)hk_GetDigital, (void**)&orig_GetDigital);
    ok &= patch_slot(vt, VT_GetAnalogActionData, (void*)hk_GetAnalog, (void**)&orig_GetAnalog);
    orig_GetOriginName = (PFN_GetOriginName)vt[VT_GetOriginLocalizedName];
    g_input = iface;
    logf("[OpenVR] IVRInput_010 vtable hooks %s", ok ? "installed" : "FAILED");
}

// ---- REFramework VR menu pointer (OpenVR) ------------------------------------
// REFramework's in-headset menu is a SteamVR overlay with
// VROverlayInputMethod_Mouse; it only reacts to SteamVR mouse events
// (OverlayComponent.cpp). With Steam Frame controllers SteamVR sends none, so
// the plugin generates them: a ray from the right controller's "tip"
// component (the same one REFramework uses to open the menu), the hit point
// from IVROverlay::ComputeOverlayIntersection, the right trigger as the left
// mouse button and the right stick as the wheel.
typedef bool (*PFN_Poll)(void*, uint64_t, vr::VREvent_t*, uint32_t);
typedef vr::EVROverlayError (*PFN_GetFlag)(void*, uint64_t, vr::VROverlayFlags, bool*);
typedef vr::EVROverlayError (*PFN_SetFlag)(void*, uint64_t, vr::VROverlayFlags, bool);
typedef vr::EVROverlayError (*PFN_GetMouseScale)(void*, uint64_t, vr::HmdVector2_t*);
typedef vr::EVROverlayError (*PFN_GetBounds)(void*, uint64_t, vr::VRTextureBounds_t*);
typedef bool (*PFN_Intersect)(void*, uint64_t, const vr::VROverlayIntersectionParams_t*, vr::VROverlayIntersectionResults_t*);
typedef void (*PFN_GetPoses)(void*, vr::ETrackingUniverseOrigin, float, vr::TrackedDevicePose_t*, uint32_t);
typedef vr::TrackedDeviceIndex_t (*PFN_RoleIndex)(void*, vr::ETrackedControllerRole);
typedef uint32_t (*PFN_GetStrProp)(void*, vr::TrackedDeviceIndex_t, vr::ETrackedDeviceProperty, char*, uint32_t, vr::ETrackedPropertyError*);
typedef bool (*PFN_CompState)(void*, const char*, const char*, uint64_t, const vr::RenderModel_ControllerMode_State_t*,
                              vr::RenderModel_ComponentState_t*);

static void* g_overlay = nullptr;
static void* g_system = nullptr;
static void* g_rendermodels = nullptr;
static PFN_Poll orig_Poll = nullptr;
static std::map<uint64_t, std::deque<vr::VREvent_t>> g_ptr_queue;
static bool g_ptr_down = false;
static PFN_SetFlag orig_SetFlag = nullptr;
// What REFramework asked for (MakeOverlaysInteractiveIfVisible). With
// pointer_keep_input SteamVR gets "false": an interactive overlay makes
// SteamVR take the controllers (all app actions inactive) for a laser that
// it does not show with Steam Frame controllers.
static std::map<uint64_t, bool> g_menu_interactive;
static int g_dbg_trig = -1;

static vr::EVROverlayError hk_SetFlag(void* self, uint64_t handle, vr::VROverlayFlags flag, bool on) {
    if (flag == vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        bool& cur = g_menu_interactive[handle];
        if (cur != on) logf("pointer: REFramework menu %s%s", on ? "OPEN" : "closed",
                            (on && g_opt.overlay_pointer && g_opt.pointer_keep_input) ? " (interactive flag kept off for SteamVR)" : "");
        cur = on;
        if (g_opt.overlay_pointer && g_opt.pointer_keep_input) on = false;
    }
    return orig_SetFlag(self, handle, flag, on);
}

template <class F> static F vfn(void* obj, int idx) { return (F)(*(void***)obj)[idx]; }

static vr::HmdMatrix34_t mul34(const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b) {
    vr::HmdMatrix34_t r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) {
            float v = j == 3 ? a.m[i][3] : 0.0f;
            for (int k = 0; k < 3; ++k) v += a.m[i][k] * b.m[k][j];
            r.m[i][j] = v;
        }
    return r;
}

static void push_event(uint64_t handle, uint32_t type, float x, float y, uint32_t button) {
    vr::VREvent_t ev{};
    ev.eventType = type;
    ev.trackedDeviceIndex = vr::k_unTrackedDeviceIndexInvalid;
    if (type == vr::VREvent_ScrollSmooth) {
        ev.data.scroll.xdelta = x;
        ev.data.scroll.ydelta = y;
        ev.data.scroll.viewportscale = 1.0f;
    } else {
        ev.data.mouse.x = x;
        ev.data.mouse.y = y;
        ev.data.mouse.button = button;
    }
    g_ptr_queue[handle].push_back(ev);
}

static void build_pointer_events(void* ov, uint64_t handle) {
    auto release = [&](float x, float y) {
        if (g_ptr_down) {
            push_event(handle, vr::VREvent_MouseButtonUp, x, y, vr::VRMouseButton_Left);
            g_ptr_down = false;
            logf("pointer: click UP");
        }
    };
    if (!g_opt.overlay_pointer || !g_system || !g_input || !g_right_src || !g_trig_h) return;

    bool interactive = false;
    if (g_opt.pointer_keep_input && orig_SetFlag)
        interactive = g_menu_interactive[handle];
    else
        vfn<PFN_GetFlag>(ov, VT_OV_GetOverlayFlag)(ov, handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, &interactive);
    if (!interactive) {
        release(0, 0);
        if (g_ptr_hit) logf("pointer: menu closed");
        g_ptr_hit = false;
        g_ptr_cursor_on = 0;
        return;
    }

    const vr::TrackedDeviceIndex_t idx =
        vfn<PFN_RoleIndex>(g_system, VT_SYS_GetTrackedDeviceIndexForControllerRole)(g_system, vr::TrackedControllerRole_RightHand);
    if (idx == vr::k_unTrackedDeviceIndexInvalid || idx >= vr::k_unMaxTrackedDeviceCount) return;
    static vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
    vfn<PFN_GetPoses>(g_system, VT_SYS_GetDeviceToAbsoluteTrackingPose)(g_system, vr::TrackingUniverseStanding, 0.0f, poses,
                                                                          vr::k_unMaxTrackedDeviceCount);
    if (!poses[idx].bPoseIsValid) return;

    vr::HmdMatrix34_t tip{};
    tip.m[0][0] = tip.m[1][1] = tip.m[2][2] = 1.0f;
    if (g_rendermodels) {
        char name[vr::k_unMaxPropertyStringSize] = {};
        vr::ETrackedPropertyError perr{};
        vfn<PFN_GetStrProp>(g_system, VT_SYS_GetStringTrackedDeviceProperty)(g_system, idx, vr::Prop_RenderModelName_String, name,
                                                                             sizeof(name), &perr);
        vr::RenderModel_ControllerMode_State_t mode{};
        vr::RenderModel_ComponentState_t cs{};
        if (vfn<PFN_CompState>(g_rendermodels, VT_RM_GetComponentStateForDevicePath)(g_rendermodels, name, vr::k_pch_Controller_Component_Tip,
                                                                                   g_right_src, &mode, &cs))
            tip = cs.mTrackingToComponentLocal;
    }
    const vr::HmdMatrix34_t w = mul34(poses[idx].mDeviceToAbsoluteTracking, tip);

    vr::VROverlayIntersectionParams_t ip{};
    ip.eOrigin = vr::TrackingUniverseStanding;
    ip.vSource = {{w.m[0][3], w.m[1][3], w.m[2][3]}};
    ip.vDirection = {{-w.m[0][2], -w.m[1][2], -w.m[2][2]}};
    vr::VROverlayIntersectionResults_t res{};
    const bool hit = vfn<PFN_Intersect>(ov, VT_OV_ComputeOverlayIntersection)(ov, handle, &ip, &res);

    vr::HmdVector2_t scale{1.0f, 1.0f};
    vfn<PFN_GetMouseScale>(ov, VT_OV_GetOverlayMouseScale)(ov, handle, &scale);
    vr::VRTextureBounds_t tb{0.0f, 0.0f, 1.0f, 1.0f};
    vfn<PFN_GetBounds>(ov, VT_OV_GetOverlayTextureBounds)(ov, handle, &tb);
    const float u = res.vUVs.v[0], v = res.vUVs.v[1];  // GL convention: v = 0 at the bottom
    // mode 0: UV is over the whole texture. mode 1: UV is over the shown part
    // (REFramework shows only its window via SetOverlayTextureBounds; bounds
    // are top-left based, mouse coordinates GL / bottom-left based).
    float x, y;
    if (g_opt.pointer_uv_mode == 1) {
        x = (tb.uMin + u * (tb.uMax - tb.uMin)) * scale.v[0];
        y = (1.0f - (tb.vMin + (1.0f - v) * (tb.vMax - tb.vMin))) * scale.v[1];
    } else {
        x = u * scale.v[0];
        y = v * scale.v[1];
    }
    {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (hit && now - last > 1000) {
            last = now;
            logf("pointer: uv %.3f %.3f | bounds u %.3f-%.3f v %.3f-%.3f | scale %.0f x %.0f | mode %d -> mouse %.0f %.0f (imgui y %.0f)", u,
                 v, tb.uMin, tb.uMax, tb.vMin, tb.vMax, scale.v[0], scale.v[1], g_opt.pointer_uv_mode, x, y, scale.v[1] - y);
        }
    }

    if (hit != g_ptr_hit) logf("pointer: %s", hit ? "on menu" : "off menu");
    g_ptr_hit = hit;
    if (hit) {
        g_ptr_imgui_x = (LONG)lroundf(x);
        g_ptr_imgui_y = (LONG)lroundf(scale.v[1] - y);  // same flip REFramework applies to VREvent_MouseMove
    }
    g_ptr_cursor_on = (hit && g_opt.pointer_fix_cursor) ? 1 : 0;
    if (!hit) {
        release(x, y);
        return;
    }
    push_event(handle, vr::VREvent_MouseMove, x, y, 0);

    vr::InputDigitalActionData_t d{};
    const bool trig = orig_GetDigital(g_input, g_trig_h, &d, sizeof(d), g_right_src) == vr::VRInputError_None && d.bActive && d.bState;
    {
        const int code = (d.bActive ? 2 : 0) | (d.bState ? 1 : 0);
        if (code != g_dbg_trig) logf("pointer: right trigger active=%d state=%d", (int)d.bActive, (int)d.bState);
        g_dbg_trig = code;
    }
    if (trig && !g_ptr_down) {
        push_event(handle, vr::VREvent_MouseButtonDown, x, y, vr::VRMouseButton_Left);
        g_ptr_down = true;
        logf("pointer: click DOWN at %.0f %.0f", x, y);
    } else if (!trig) {
        release(x, y);
    }

    if (g_opt.pointer_scroll > 0.0f && g_joy_h) {
        vr::InputAnalogActionData_t a{};
        if (orig_GetAnalog(g_input, g_joy_h, &a, sizeof(a), g_right_src) == vr::VRInputError_None && a.bActive &&
            fabsf(a.y) > 0.3f)
            push_event(handle, vr::VREvent_ScrollSmooth, 0.0f, a.y * g_opt.pointer_scroll, 0);
    }
}

static bool hk_Poll(void* self, uint64_t handle, vr::VREvent_t* ev, uint32_t size) {
    if (orig_Poll(self, handle, ev, size)) return true;  // real SteamVR events first
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (g_pointer_frame) {
        g_pointer_frame = false;
        build_pointer_events(self, handle);
    }
    auto& q = g_ptr_queue[handle];
    if (q.empty() || !ev || size < sizeof(vr::VREvent_t)) return false;
    *ev = q.front();
    q.pop_front();
    return true;
}

static void* hk_GetGenericInterface(const char* ver, vr::EVRInitError* err) {
    void* p = orig_GetGenericInterface(ver, err);
    if (p && ver && strcmp(ver, "IVRInput_010") == 0) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        void** vt = *(void***)p;
        if (vt[VT_GetDigitalActionData] != (void*)hk_GetDigital) hook_input(p);
    } else if (p && ver && strcmp(ver, "IVROverlay_025") == 0) {
        std::lock_guard<std::recursive_mutex> lk(g_mtx);
        g_overlay = p;
        void** vt = *(void***)p;
        if (vt[VT_OV_PollNextOverlayEvent] != (void*)hk_Poll) {
            bool ok = patch_slot(vt, VT_OV_PollNextOverlayEvent, (void*)hk_Poll, (void**)&orig_Poll);
            ok &= patch_slot(vt, VT_OV_SetOverlayFlag, (void*)hk_SetFlag, (void**)&orig_SetFlag);
            logf("[OpenVR] IVROverlay_025 PollNextOverlayEvent hook %s (menu pointer)", ok ? "installed" : "FAILED");
        }
    } else if (p && ver && strcmp(ver, "IVRSystem_022") == 0) {
        g_system = p;
    } else if (p && ver && strcmp(ver, "IVRRenderModels_006") == 0) {
        g_rendermodels = p;
    } else if (p && ver && strncmp(ver, "IVRInput_", 9) == 0) {
        logf("[OpenVR] %s requested, only IVRInput_010 is supported: not hooked", ver);
    }
    return p;
}

static std::recursive_mutex g_install_mtx;
static bool g_hook_done = false;

static void install(const char* who) {
    std::lock_guard<std::recursive_mutex> lk(g_install_mtx);
    if (g_hook_done) return;
    HMODULE m = GetModuleHandleW(L"openvr_api.dll");
    if (!m) {
        std::wstring p = exe_dir() + L"\\openvr_api.dll";
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
            logf("[OpenVR] openvr_api.dll not in the game folder: OpenVR hook not needed (REFramework uses OpenXR)");
            return;
        }
        m = LoadLibraryW(p.c_str());
    }
    if (!m) {
        logf("[OpenVR] could not load openvr_api.dll (%lu)", GetLastError());
        return;
    }
    void* target = (void*)GetProcAddress(m, "VR_GetGenericInterface");
    if (!target) {
        logf("[OpenVR] VR_GetGenericInterface export not found");
        return;
    }
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        logf("[OpenVR] MH_Initialize failed (%d)", (int)s);
        return;
    }
    s = MH_CreateHook(target, (void*)hk_GetGenericInterface, (void**)&orig_GetGenericInterface);
    if (s == MH_OK) s = MH_EnableHook(target);
    g_hook_done = true;
    logf("[OpenVR] VR_GetGenericInterface hook %s (%d) from %s, SteamVR client %s", s == MH_OK ? "installed" : "FAILED", (int)s,
         who, GetModuleHandleW(L"vrclient_x64.dll") ? "ALREADY loaded (may be late)" : "not loaded yet (good)");
}

// ImGui's Win32 backend (used by REFramework) queues the DESKTOP mouse
// position every frame while the game window is focused and the desktop mouse
// is not over it (ImGui_ImplWin32_UpdateMouseData: GetCursorPos +
// ScreenToClient + AddMousePosEvent). That queued event is applied in
// ImGui::NewFrame, AFTER REFramework wrote the VR overlay position directly
// to io.MousePos, so the menu cursor jumps to the desktop mouse. While the
// pointer is on the menu, GetCursorPos called from REFramework returns the
// pointer position instead (in screen space, so ScreenToClient gives it back).
// Calls from the game or any other module are not changed.
typedef BOOL(WINAPI* PFN_GetCursorPos)(LPPOINT);
static PFN_GetCursorPos orig_GetCursorPos = nullptr;
static volatile LONG g_cursor_log = 0;

static BOOL WINAPI hk_GetCursorPos(LPPOINT pt) {
    if (g_ptr_cursor_on && pt && g_ref_module) {
        HMODULE caller = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)__builtin_return_address(0), &caller) &&
            caller == g_ref_module) {
            POINT p{g_ptr_imgui_x, g_ptr_imgui_y};
            HWND w = GetForegroundWindow();  // the backend only asks when the game window is foreground
            if (w && ClientToScreen(w, &p)) {
                *pt = p;
                if (InterlockedExchange(&g_cursor_log, 1) == 0)
                    logf("pointer: GetCursorPos from REFramework redirected to the pointer (client %ld %ld)", g_ptr_imgui_x, g_ptr_imgui_y);
                return TRUE;
            }
        }
    }
    return orig_GetCursorPos(pt);
}

static void install_cursor_hook(const void* ref_addr) {
    static bool done = false;
    if (done || !g_opt.overlay_pointer || !g_opt.pointer_fix_cursor) return;
    done = true;
    if (ref_addr)
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ref_addr,
                           &g_ref_module);
    if (!g_ref_module) g_ref_module = GetModuleHandleW(L"dinput8.dll");
    if (!g_ref_module && getenv("RE2_FRAME_TEST_REF_IS_EXE")) g_ref_module = GetModuleHandleW(nullptr);  // mock test only
    if (!g_ref_module) {
        logf("pointer: REFramework module not found, cursor fix off");
        return;
    }
    void* target = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetCursorPos");
    MH_STATUS s = MH_Initialize();
    if (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED) s = target ? MH_CreateHook(target, (void*)hk_GetCursorPos, (void**)&orig_GetCursorPos) : MH_ERROR_FUNCTION_NOT_FOUND;
    if (s == MH_OK) s = MH_EnableHook(target);
    logf("pointer: cursor fix (GetCursorPos hook for REFramework) %s (%d)", s == MH_OK ? "installed" : "FAILED", (int)s);
}

// REFramework may initialize SteamVR before reframework_plugin_initialize, so
// the hook is installed from a thread started at DLL load. It runs as soon as
// the loader lock is released (right after REFramework's LoadLibrary returns).
static DWORD WINAPI early_thread(LPVOID) {
    install("DLL load thread");
    return 0;
}

// If the hook still came too late (IVRInput already obtained), patch the
// vtable now. B/Y, the D-pad menu stick and logging then work, but the
// frame_controller binding was not injected for this start.
static void late_fallback() {
    std::lock_guard<std::recursive_mutex> lk(g_mtx);
    if (g_input || !GetModuleHandleW(L"vrclient_x64.dll")) return;
    PFN_GetGenericInterface ggi = orig_GetGenericInterface;
    HMODULE m = GetModuleHandleW(L"openvr_api.dll");
    if (!ggi && m) ggi = (PFN_GetGenericInterface)GetProcAddress(m, "VR_GetGenericInterface");
    if (!ggi) return;
    vr::EVRInitError err = vr::VRInitError_None;
    void* p = ggi("IVRInput_010", &err);
    if (p) {
        hook_input(p);
        logf("[OpenVR] LATE: IVRInput hooked after REFramework set its manifest; frame_controller binding NOT injected this run");
    } else {
        logf("[OpenVR] late IVRInput lookup failed (%d)", (int)err);
    }
}

}  // namespace ovr

// ===========================================================================
// OpenXR diagnostics: is the layer visible to the loader, and does the app
// create an instance at all?
// ===========================================================================
namespace diag {

static PFN_xrCreateInstance orig_CreateInstance = nullptr;

static void log_elevation() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        logf("[diag] OpenProcessToken failed (%lu)", GetLastError());
        return;
    }
    TOKEN_ELEVATION el{};
    DWORD n = 0;
    bool elevated = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &n) && el.TokenIsElevated;
    DWORD level = 0;
    uint8_t buf[SECURITY_MAX_SID_SIZE + sizeof(DWORD)] = {};
    if (GetTokenInformation(tok, TokenIntegrityLevel, buf, sizeof(buf), &n)) {
        auto* ml = (TOKEN_MANDATORY_LABEL*)buf;
        level = *GetSidSubAuthority(ml->Label.Sid, *GetSidSubAuthorityCount(ml->Label.Sid) - 1);
    }
    CloseHandle(tok);
    logf("[diag] process elevated: %s, integrity level 0x%lx (0x2000 medium = OK, 0x3000 high = loader IGNORES env vars)",
         elevated ? "YES (admin)" : "no", level);
}

static std::string file_version(const std::wstring& path) {
    DWORD h = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &h);
    if (!sz) return "unknown";
    std::vector<uint8_t> data(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, data.data())) return "unknown";
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", (void**)&fi, &len) || !fi) return "unknown";
    char b[64];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
             HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    return b;
}

static void log_env(const wchar_t* name) {
    wchar_t buf[4096] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, 4096);
    std::string s;
    for (DWORD i = 0; i < n && i < 4096; ++i) s += buf[i] < 128 ? (char)buf[i] : '?';
    logf("[diag] %ls = %s", name, n ? s.c_str() : "<not set>");
}

static XRAPI_ATTR XrResult XRAPI_CALL hk_xrCreateInstance(const XrInstanceCreateInfo* ci, XrInstance* out) {
    logf("[diag] app calls xrCreateInstance (app '%s', %u extensions)", ci ? ci->applicationInfo.applicationName : "?",
         ci ? ci->enabledExtensionCount : 0u);
    XrResult r = orig_CreateInstance(ci, out);
    logf("[diag] xrCreateInstance result %d, layer %s", (int)r,
         xr::g_instance != XR_NULL_HANDLE ? "ACTIVE in this instance" : "NOT loaded by the loader");
    return r;
}

static void log_vr_modules() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    int n = 0;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        std::string name;
        for (wchar_t* c = me.szModule; *c; ++c) name += *c < 128 ? (char)tolower(*c) : '?';
        if (name.find("openxr") != std::string::npos || name.find("vrclient") != std::string::npos ||
            name.find("openvr") != std::string::npos || name.find("steamxr") != std::string::npos ||
            name.find("libovr") != std::string::npos || name.find("virtualdesktop") != std::string::npos) {
            logf("[diag] already loaded: %s", name.c_str());
            ++n;
        }
    }
    CloseHandle(snap);
    if (!n) logf("[diag] no VR runtime module loaded yet (good: no OpenXR instance exists yet)");
}

static void openxr_checks() {
    log_elevation();
    log_vr_modules();
    log_env(L"XR_API_LAYER_PATH");
    log_env(L"XR_ENABLE_API_LAYERS");

    std::wstring lp = exe_dir() + L"\\openxr_loader.dll";
    bool exists = GetFileAttributesW(lp.c_str()) != INVALID_FILE_ATTRIBUTES;
    HMODULE m = GetModuleHandleW(L"openxr_loader.dll");
    logf("[diag] openxr_loader.dll in game folder: %s, version %s, already loaded: %s", exists ? "yes" : "NO",
         exists ? file_version(lp).c_str() : "-", m ? "yes" : "no");
    if (!m && exists) m = LoadLibraryW(lp.c_str());  // same module REFramework loads later
    if (!m) return;

    // Ask the loader which API layers it can see with the current env vars.
    auto enumLayers = (PFN_xrEnumerateApiLayerProperties)GetProcAddress(m, "xrEnumerateApiLayerProperties");
    if (enumLayers) {
        uint32_t n = 0;
        XrResult r = enumLayers(0, &n, nullptr);
        std::vector<XrApiLayerProperties> props(n, {XR_TYPE_API_LAYER_PROPERTIES});
        if (r == XR_SUCCESS && n) r = enumLayers(n, &n, props.data());
        bool found = false;
        logf("[diag] loader sees %u API layer(s) (result %d)", n, (int)r);
        for (uint32_t i = 0; i < n && r == XR_SUCCESS; ++i) {
            logf("[diag]   layer: %s", props[i].layerName);
            if (strcmp(props[i].layerName, LAYER_NAME) == 0) found = true;
        }
        logf("[diag] our layer %s visible to the loader", found ? "IS" : "is NOT");
    }

    // Log when the app creates its instance (and whether our layer was in it).
    void* target = (void*)GetProcAddress(m, "xrCreateInstance");
    MH_STATUS s = MH_Initialize();
    if (target && (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED)) {
        s = MH_CreateHook(target, (void*)hk_xrCreateInstance, (void**)&orig_CreateInstance);
        if (s == MH_OK) s = MH_EnableHook(target);
        logf("[diag] xrCreateInstance watch %s (%d)", s == MH_OK ? "installed" : "FAILED", (int)s);
    }
}

}  // namespace diag

// ===========================================================================
// REFramework plugin entry points
// ===========================================================================
static void append_env(const wchar_t* name, const std::wstring& value) {
    wchar_t buf[4096] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, 4096);
    std::wstring cur = (n > 0 && n < 4096) ? std::wstring(buf) : std::wstring();
    if (cur.find(value) != std::wstring::npos) return;
    std::wstring v = cur.empty() ? value : cur + L";" + value;
    SetEnvironmentVariableW(name, v.c_str());
}

// ===========================================================================
// Game probe: runs on the game thread (REFramework on_pre_application_entry)
// and records whether a player exists. Same check as REFramework's RE2.lua:
// app.ropeway.PlayerManager.get_CurrentPlayer().
// ===========================================================================
namespace game_probe {
static const REFrameworkSDKData* g_sdk = nullptr;
static REFrameworkMethodHandle g_get_player = nullptr;
// Found with the 0.3.1 probe (RE2 RT): true in inventory, files, map and pause.
// isBusyFile: found with the probe in 1.0 - true while a newly picked-up document is shown
// (get_IsGuiOpen stays false there), and only inside the inventory files otherwise.
static REFrameworkMethodHandle g_is_gui_open = nullptr, g_is_open_map = nullptr, g_is_busy_file = nullptr;
static int g_last_menu = -1;

static bool call_bool(REFrameworkMethodHandle m, REFrameworkManagedObjectHandle obj, bool* out) {
    if (!m || !obj) return false;
    alignas(16) unsigned char ret[256] = {};
    if (g_sdk->method->invoke(m, obj, nullptr, 0, ret, sizeof(ret)) != REFRAMEWORK_ERROR_NONE || ret[128]) return false;
    *out = ret[0] != 0;
    return true;
}
static bool g_last_logged = false, g_logged_once = false;

// ---- Diagnostic: find the game's "pause/menu open" flag -------------------
// Lists managed singletons once, then polls their no-argument bool getters
// (get_* / is*) on GUI / pause / menu / flow singletons and logs every change.
// Observe-only: nothing is written to the game.
struct Getter { std::string singleton; REFrameworkMethodHandle m; std::string name; int last; };
static std::vector<Getter> g_getters;
static bool g_probe_built = false;
static unsigned g_frame = 0;

static std::string tname(REFrameworkTypeDefinitionHandle t) {
    if (!t) return "?";
    char buf[512] = {};
    unsigned n = 0;
    if (g_sdk->type_definition->get_full_name(t, buf, sizeof(buf), &n) != REFRAMEWORK_ERROR_NONE) return "?";
    return buf;
}
static std::string lower(std::string x) { std::transform(x.begin(), x.end(), x.begin(), ::tolower); return x; }

static void build_probe() {
    g_probe_built = true;
    std::vector<REFrameworkManagedSingleton> v(4096);
    unsigned count = 0;
    if (g_sdk->functions->get_managed_singletons(v.data(), (unsigned)(v.size() * sizeof(v[0])), &count) != REFRAMEWORK_ERROR_NONE) {
        logf("probe: get_managed_singletons failed");
        return;
    }
    logf("probe: %u managed singletons", count);
    static const char* keys[] = {"pause", "gui", "menu", "flow", "gamemaster", "timescale", "scenario", "option"};
    for (unsigned i = 0; i < count && i < v.size(); ++i) {
        std::string tn = tname(v[i].t);
        std::string l = lower(tn);
        bool cand = false;
        for (auto* k : keys) if (l.find(k) != std::string::npos) cand = true;
        logf("probe: singleton %s%s", tn.c_str(), cand ? "  [polled]" : "");
        if (!cand || !v[i].instance) continue;
        unsigned nm = g_sdk->type_definition->get_num_methods(v[i].t);
        std::vector<REFrameworkMethodHandle> ms(nm + 1);
        unsigned got = 0;
        if (g_sdk->type_definition->get_methods(v[i].t, ms.data(), (unsigned)(ms.size() * sizeof(ms[0])), &got) != REFRAMEWORK_ERROR_NONE) continue;
        for (unsigned j = 0; j < got; ++j) {
            auto m = ms[j];
            if (!m || g_sdk->method->get_num_params(m) != 0 || g_sdk->method->is_static(m)) continue;
            const char* mn = g_sdk->method->get_name(m);
            if (!mn) continue;
            std::string mname = mn;
            if (!(mname.rfind("get_", 0) == 0 || mname.rfind("is", 0) == 0 || mname.rfind("Is", 0) == 0)) continue;
            if (tname(g_sdk->method->get_return_type(m)) != "System.Boolean") continue;
            if (g_getters.size() >= 400) break;
            g_getters.push_back({tn, m, tn + "." + mname, -1});
        }
    }
    logf("probe: polling %u bool getters (open/close PAUSE and the inventory, then quit)", (unsigned)g_getters.size());
}

static void poll_probe() {
    if (!g_opt.probe_pause) return;
    if (!g_probe_built) build_probe();
    if (++g_frame % 6) return;
    std::map<std::string, REFrameworkManagedObjectHandle> inst;  // re-fetched every poll: never keep stale pointers
    for (auto& gt : g_getters) {
        auto it = inst.find(gt.singleton);
        if (it == inst.end()) it = inst.emplace(gt.singleton, g_sdk->functions->get_managed_singleton(gt.singleton.c_str())).first;
        if (!it->second) continue;
        alignas(16) unsigned char ret[256] = {};
        if (g_sdk->method->invoke(gt.m, it->second, nullptr, 0, ret, sizeof(ret)) != REFRAMEWORK_ERROR_NONE || ret[128]) continue;
        int val = ret[0] ? 1 : 0;
        if (val != gt.last) {
            logf("probe: %s = %s%s", gt.name.c_str(), val ? "true" : "false", gt.last < 0 ? "  (initial)" : "");
            gt.last = val;
        }
    }
}

static void on_update_behavior() {
    if (!g_sdk || !g_get_player) return;
    auto pm = g_sdk->functions->get_managed_singleton("app.ropeway.PlayerManager");
    bool has_player = false;
    if (pm) {
        alignas(16) unsigned char ret[256] = {};
        if (g_sdk->method->invoke(g_get_player, pm, nullptr, 0, ret, sizeof(ret)) == REFRAMEWORK_ERROR_NONE && !ret[128])
            has_player = *(void**)ret != nullptr;
    }
    g_no_player.store(!has_player);

    bool menu = false;
    if (has_player) {
        auto gm = g_sdk->functions->get_managed_singleton("app.ropeway.gui.GUIMaster");
        bool open = false, map = false, file = false;
        if (call_bool(g_is_gui_open, gm, &open) && open) {
            call_bool(g_is_open_map, gm, &map);
            menu = !map;  // map: the D-pad changes floors, the stick would pan the map
        }
        if (!menu && call_bool(g_is_busy_file, gm, &file) && file) {
            menu = true;  // document viewer after picking up a new document
            open = true;
        }
        if ((int)menu != g_last_menu) {
            if (g_last_menu >= 0 || menu)
                logf("game: GUI %s -> menu D-pad stick %s", open ? (map ? "open (map)" : "open") : "closed",
                     menu && g_opt.menu_dpad_stick ? "ON" : "off");
            g_last_menu = menu;
        }
        poll_probe();
    }
    g_gui_menu.store(menu);
    if (!g_logged_once || has_player == g_last_logged) {
        g_logged_once = true;
        g_last_logged = !has_player;
        logf("game: player %s -> menu D-pad stick %s", has_player ? "present" : "NONE (title/menu)",
             (!has_player && g_opt.menu_dpad_stick) ? "ON" : "off");
    }
}

static void install(const REFrameworkPluginInitializeParam* param) {
    char env[8] = {};
    if (GetEnvironmentVariableA("RE2_FRAME_TEST_NO_PLAYER", env, 8) > 0 && env[0] == '1') {
        g_no_player.store(true);
        logf("game: TEST no-player forced by env");
        return;
    }
    if (!param || !param->sdk || !param->functions || !param->functions->on_pre_application_entry) {
        logf("game: no REFramework SDK access, menu D-pad stick unavailable");
        return;
    }
    g_sdk = param->sdk;
    auto tdb = g_sdk->functions->get_tdb();
    g_get_player = tdb ? g_sdk->tdb->find_method(tdb, "app.ropeway.PlayerManager", "get_CurrentPlayer") : nullptr;
    if (!g_get_player) {
        logf("game: app.ropeway.PlayerManager.get_CurrentPlayer not found, menu D-pad stick unavailable");
        return;
    }
    g_is_gui_open = g_sdk->tdb->find_method(tdb, "app.ropeway.gui.GUIMaster", "get_IsGuiOpen");
    g_is_open_map = g_sdk->tdb->find_method(tdb, "app.ropeway.gui.GUIMaster", "get_IsOpenMap");
    g_is_busy_file = g_sdk->tdb->find_method(tdb, "app.ropeway.gui.GUIMaster", "isBusyFile");
    logf("game: GUIMaster.get_IsGuiOpen %s, get_IsOpenMap %s, isBusyFile %s", g_is_gui_open ? "found" : "NOT found",
         g_is_open_map ? "found" : "NOT found", g_is_busy_file ? "found" : "NOT found");
    bool ok = param->functions->on_pre_application_entry("UpdateBehavior", on_update_behavior);
    g_probe_ok.store(ok);
    logf("game: player probe on UpdateBehavior %s", ok ? "installed" : "FAILED");
}
}  // namespace game_probe

static bool g_env_set_in_dllmain = false;

// REFramework LoadLibrary()s plugins very early (PluginLoader::early_init,
// right after the exe is unpacked) but calls reframework_plugin_initialize
// much later. Setting the loader env vars here makes the layer independent of
// when REFramework creates its OpenXR instance. Only kernel32 calls here.
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && process_allowed()) {
        append_env(L"XR_API_LAYER_PATH", module_dir() + L"\\" BASE_NAME);
        append_env(L"XR_ENABLE_API_LAYERS", L"" LAYER_NAME);
        g_env_set_in_dllmain = true;
        HANDLE t = CreateThread(nullptr, 0, ovr::early_thread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* v) {
    // Only version and log_info are used, so the oldest plugin API works.
    // game_name is not written: very old REFramework builds lack that field.
    v->major = 1;
    v->minor = 0;
    v->patch = 0;
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    g_process_allowed = process_allowed();
    logf("%s %s, DEBUG_LOG=%d, process %s", BASE_NAME, PLUGIN_VERSION, DEBUG_LOG,
         g_process_allowed ? "allowed" : "NOT allowed (pass-through)");
    logf("loader env vars set at DLL load (early): %s", g_env_set_in_dllmain ? "yes" : "no");

    const std::wstring dir = module_dir() + L"\\" BASE_NAME;
    const std::wstring manifest = dir + L"\\" BASE_NAME ".json";
    const bool have_manifest = GetFileAttributesW(manifest.c_str()) != INVALID_FILE_ATTRIBUTES;

    if (g_process_allowed) {
        append_env(L"XR_API_LAYER_PATH", dir);
        append_env(L"XR_ENABLE_API_LAYERS", L"" LAYER_NAME);
        logf("[OpenXR] layer path set, manifest %s", have_manifest ? "found" : "MISSING (" BASE_NAME "\\" BASE_NAME ".json)");
#if DEBUG_LOG
        diag::openxr_checks();  // diagnostics only (loader visibility, elevation, xrCreateInstance watch)
#endif
        ovr::install("plugin init");
        ovr::late_fallback();
        load_options();
        ovr::install_cursor_hook((param && param->functions) ? (const void*)param->functions : nullptr);
        game_probe::install(param);
    }

    if (param && param->functions && param->functions->log_info) {
        param->functions->log_info("[%s] %s loaded, OpenXR layer manifest %s", BASE_NAME, PLUGIN_VERSION,
                                   have_manifest ? "found" : "MISSING");
    }
    return true;  // never block other plugins or the game
}
