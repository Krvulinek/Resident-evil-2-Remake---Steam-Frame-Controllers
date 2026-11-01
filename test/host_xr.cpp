// host_xr.cpp - imitates REFramework's OpenXR input setup (OpenXR.cpp, VR.cpp,
// Oct 2026 master) and checks what RE2's openvr_input_to_re2_re3 would see.
// Built as re2.exe so the plugin's allow-list accepts it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <reframework/API.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static PFN_xrGetInstanceProcAddr gipa;
static XrInstance inst;
template <class T> static T F(const char* n) { PFN_xrVoidFunction f = nullptr; gipa(inst, n, &f); return (T)f; }
typedef void (*PFN_mock_set)(const char*, float);
typedef void (*PFN_mock_clear)();

// REFramework actions.json (Bindings.cpp): name, type
static const std::pair<const char*, const char*> kActions[] = {
    {"HeadsetOnHead", "boolean"}, {"Pose", "pose"}, {"Trigger", "boolean"}, {"Grip", "boolean"},
    {"Touchpad", "vector2"}, {"TouchpadClick", "boolean"}, {"Joystick", "vector2"}, {"JoystickClick", "boolean"},
    {"AButton", "boolean"}, {"BButton", "boolean"}, {"SystemButton", "boolean"}, {"Squeeze", "vector1"},
    {"Teleport", "boolean"}, {"DPad_Up", "boolean"}, {"DPad_Right", "boolean"}, {"DPad_Down", "boolean"},
    {"DPad_Left", "boolean"}, {"WeaponDial_Start", "boolean"}, {"RE3_Dodge", "boolean"}, {"RE2_Quickturn", "boolean"},
    {"RE2_FirstPerson_Toggle", "boolean"}, {"RE2_Reset_View", "boolean"}, {"RE2_Change_Ammo", "boolean"},
    {"RE2_Toggle_Flashlight", "boolean"}, {"MiniMap", "boolean"}, {"Block", "boolean"}, {"Heal", "boolean"},
    {"Haptic", "vibration"}};

// OpenXR.hpp s_bindings_map
static const std::pair<const char*, const char*> kMap[] = {
    {"/user/hand/*/input/grip/pose", "pose"}, {"/user/hand/*/input/trigger", "trigger"},
    {"/user/hand/*/input/squeeze", "grip"}, {"/user/hand/*/input/x/click", "abutton"},
    {"/user/hand/*/input/y/click", "bbutton"}, {"/user/hand/*/input/a/click", "abutton"},
    {"/user/hand/*/input/b/click", "bbutton"}, {"/user/hand/*/input/thumbstick", "joystick"},
    {"/user/hand/*/input/thumbstick/click", "joystickclick"}, {"/user/hand/*/input/system/click", "systembutton"},
    {"/user/hand/*/input/menu/click", "systembutton"}, {"/user/hand/*/input/trackpad", "touchpad"},
    {"/user/hand/*/input/trackpad/click", "touchpadclick"}, {"/user/hand/*/output/haptic", "haptic"},
    {"/user/hand/right/input/a/click", "re3_dodge"}, {"/user/hand/left/input/trigger", "weapondial_start"}};
static const char* kControllers[] = {"/interaction_profiles/khr/simple_controller", "/interaction_profiles/oculus/touch_controller",
                                     "/interaction_profiles/valve/index_controller"};

static int failures = 0;

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // 1. REFramework loads plugins (Mods::on_initialize) before VR init
    HMODULE plug = LoadLibraryA("reframework\\plugins\\re2_frame_controllers.dll");
    if (!plug) { printf("FAIL: plugin load %lu\n", GetLastError()); return 1; }
    auto rv = (REFPluginRequiredVersionFn)GetProcAddress(plug, "reframework_plugin_required_version");
    auto ri = (REFPluginInitializeFn)GetProcAddress(plug, "reframework_plugin_initialize");
    REFrameworkPluginVersion ver{};
    rv(&ver);
    printf("plugin requires %d.%d.%d, init -> %d\n", ver.major, ver.minor, ver.patch, (int)ri(nullptr));

    // 2. VR init: openxr_loader.dll from the game folder
    HMODULE ldr = LoadLibraryA("openxr_loader.dll");
    if (!ldr) { printf("FAIL: loader %lu\n", GetLastError()); return 1; }
    gipa = (PFN_xrGetInstanceProcAddr)GetProcAddress(ldr, "xrGetInstanceProcAddr");
    HMODULE rt = GetModuleHandleA("mock_runtime.dll");
    auto mock_set = (PFN_mock_set)GetProcAddress(rt, "mock_set");
    auto mock_clear = (PFN_mock_clear)GetProcAddress(rt, "mock_clear");

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(ci.applicationInfo.applicationName, "re2");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    const char* exts[] = {"XR_KHR_D3D11_enable"};
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    auto createInstance = (PFN_xrCreateInstance)nullptr;
    gipa(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction*)&createInstance);
    XrResult r = createInstance(&ci, &inst);
    printf("xrCreateInstance %d\n", (int)r);
    if (r) return 1;
    rt = GetModuleHandleA("mock_runtime.dll");
    mock_set = (PFN_mock_set)GetProcAddress(rt, "mock_set");
    mock_clear = (PFN_mock_clear)GetProcAddress(rt, "mock_clear");

    auto StringToPath = F<PFN_xrStringToPath>("xrStringToPath");
    auto CreateActionSet = F<PFN_xrCreateActionSet>("xrCreateActionSet");
    auto CreateAction = F<PFN_xrCreateAction>("xrCreateAction");
    auto Suggest = F<PFN_xrSuggestInteractionProfileBindings>("xrSuggestInteractionProfileBindings");
    auto CreateSession = F<PFN_xrCreateSession>("xrCreateSession");
    auto Attach = F<PFN_xrAttachSessionActionSets>("xrAttachSessionActionSets");
    auto Sync = F<PFN_xrSyncActions>("xrSyncActions");
    auto GetBool = F<PFN_xrGetActionStateBoolean>("xrGetActionStateBoolean");
    auto GetFloat = F<PFN_xrGetActionStateFloat>("xrGetActionStateFloat");
    auto GetVec2 = F<PFN_xrGetActionStateVector2f>("xrGetActionStateVector2f");
    auto GetProf = F<PFN_xrGetCurrentInteractionProfile>("xrGetCurrentInteractionProfile");
    auto PollEvent = F<PFN_xrPollEvent>("xrPollEvent");
    auto CreateActionSpace = F<PFN_xrCreateActionSpace>("xrCreateActionSpace");
    auto LocateSpace = F<PFN_xrLocateSpace>("xrLocateSpace");

    XrPath hand[2];
    StringToPath(inst, "/user/hand/left", &hand[0]);
    StringToPath(inst, "/user/hand/right", &hand[1]);

    XrActionSet set;
    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(asci.actionSetName, "default");
    strcpy(asci.localizedActionSetName, "default");
    CreateActionSet(inst, &asci, &set);

    std::map<std::string, XrAction> amap;
    std::map<XrAction, XrActionType> atype;
    // profile -> hand -> action names present in path_map
    std::map<std::string, std::set<std::string>> path_map[2];
    std::map<std::string, std::vector<XrActionSuggestedBinding>> prof_b;

    for (auto& a : kActions) {
        std::string n = a.first;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::string t = a.second;
        XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
        strcpy(aci.actionName, n.c_str());
        strcpy(aci.localizedActionName, a.first);
        aci.countSubactionPaths = 2;
        aci.subactionPaths = hand;
        if (t == "boolean") aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        else if (t == "pose") aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        else if (t == "vector1") aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        else if (t == "vector2") aci.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
        else aci.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
        XrAction act;
        if (CreateAction(set, &aci, &act)) { printf("FAIL create %s\n", n.c_str()); return 1; }
        amap[n] = act;
        atype[act] = aci.actionType;
        // REFramework: suggest each binding for each controller, incrementally
        for (auto& m : kMap) {
            if (n != m.second) continue;
            for (int i = 0; i < 2; ++i) {
                std::string hs = m.first;
                size_t w = hs.find('*');
                int idx = i;
                if (w != std::string::npos) hs.replace(w, 1, i == 0 ? "left" : "right");
                else idx = hs.find("left") != std::string::npos ? 0 : 1;
                XrPath p;
                StringToPath(inst, hs.c_str(), &p);
                for (auto* c : kControllers) {
                    XrPath cp;
                    StringToPath(inst, c, &cp);
                    auto& v = prof_b[c];
                    v.push_back({act, p});
                    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
                    sb.interactionProfile = cp;
                    sb.countSuggestedBindings = (uint32_t)v.size();
                    sb.suggestedBindings = v.data();
                    if (Suggest(inst, &sb) != XR_SUCCESS) v.pop_back();
                    else path_map[idx][c].insert(n);
                }
                if (w == std::string::npos) break;
            }
        }
    }

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.systemId = 1;
    XrSession sess;
    CreateSession(inst, &sci, &sess);
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &set;
    printf("attach %d\n", (int)Attach(sess, &ai));

    XrSpace spaces[2];
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo sp{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sp.action = amap["pose"];
        sp.subactionPath = hand[i];
        sp.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
        CreateActionSpace(sess, &sp, &spaces[i]);
    }

    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (PollEvent(inst, &ev) == XR_SUCCESS) ev = {XR_TYPE_EVENT_DATA_BUFFER};
    char buf[256];
    XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
    GetProf(sess, hand[0], &ps);
    uint32_t n = 0;
    F<PFN_xrPathToString>("xrPathToString")(inst, ps.interactionProfile, sizeof(buf), &n, buf);
    std::string cur_profile = buf;
    printf("app sees current profile: %s\n", buf);

    XrActiveActionSet aas{set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &aas;

    // REFramework is_action_active: bool -> isActive && currentState, float -> > 0
    auto active = [&](const std::string& name, int h, bool* changed = nullptr) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = amap[name];
        gi.subactionPath = hand[h];
        if (atype[gi.action] == XR_ACTION_TYPE_BOOLEAN_INPUT) {
            XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
            GetBool(sess, &gi, &st);
            if (changed) *changed = st.changedSinceLastSync;
            return st.isActive && st.currentState;
        }
        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
        GetFloat(sess, &gi, &st);
        return st.isActive && st.currentState > 0.0f;
    };
    // REFramework get_left/right_stick_axis
    auto stick = [&](int h) {
        XrVector2f out{0, 0};
        auto it = path_map[h].find(cur_profile);
        if (it == path_map[h].end()) return out;
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = it->second.count("joystick") ? amap["joystick"] : amap["touchpad"];
        gi.subactionPath = hand[h];
        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
        GetVec2(sess, &gi, &st);
        return st.currentState;
    };
    auto snapshot = [&]() {
        std::set<std::string> s;
        for (auto& kv : amap) {
            if (atype[kv.second] != XR_ACTION_TYPE_BOOLEAN_INPUT && atype[kv.second] != XR_ACTION_TYPE_FLOAT_INPUT) continue;
            for (int h = 0; h < 2; ++h)
                if (active(kv.first, h)) s.insert(kv.first + (h ? "/right" : "/left"));
        }
        return s;
    };
    auto join = [](const std::set<std::string>& s) { std::string o; for (auto& x : s) o += x + " "; return o; };

    struct Case { const char* phys; const char* label; std::set<std::string> expect; };
    const Case cases[] = {
        {"/user/hand/right/input/a/click", "A (interact)", {"abutton/right", "re3_dodge/right"}},
        {"/user/hand/right/input/b/click", "B (back)", {"abutton/left"}},
        {"/user/hand/right/input/x/click", "X (reload)", {"bbutton/right"}},
        {"/user/hand/right/input/y/click", "Y (inventory)", {"bbutton/left"}},
        {"/user/hand/left/input/bumper/click", "LB (sub weapon)", {"grip/left"}},
        {"/user/hand/right/input/bumper/click", "RB (ammo, tab R)", {"re2_change_ammo/right", "grip/right"}},
        {"/user/hand/left/input/dpad_up/click", "D-up", {"dpad_up/left"}},
        {"/user/hand/left/input/dpad_down/click", "D-down", {"dpad_down/left"}},
        {"/user/hand/left/input/dpad_left/click", "D-left", {"dpad_left/left"}},
        {"/user/hand/left/input/dpad_right/click", "D-right", {"dpad_right/left"}},
        {"/user/hand/left/input/view/click", "View (map)", {"minimap/left"}},
        {"/user/hand/right/input/menu/click", "Menu (pause)", {"systembutton/right"}},
        {"/user/hand/left/input/thumbstick/click", "L3 (run)", {"joystickclick/left"}},
        {"/user/hand/right/input/thumbstick/click", "R3 (reset cam)", {"joystickclick/right", "re2_reset_view/right"}},
        {"/user/hand/left/input/trigger/value", "LT (defense)", {"trigger/left"}},
        {"/user/hand/right/input/squeeze/value", "R grip (aim)", {"grip/right"}},
        {"/user/hand/left/input/squeeze/value", "L grip (support)", {"grip/left"}},
    };

    bool frame_mode = argc < 2 || strcmp(argv[1], "noframe") != 0;
    Sync(sess, &si);
    auto idle = snapshot();
    printf("idle: %s\n", join(idle).c_str());
    for (auto& c : cases) {
        mock_clear();
        mock_set(c.phys, 1.0f);
        Sync(sess, &si);
        auto s = snapshot();
        bool ok = !frame_mode || s == c.expect;
        if (!ok) failures++;
        printf("%-5s %-18s -> %s\n", ok ? "OK" : "FAIL", c.label, join(s).c_str());
        if (!ok) printf("      expected %s\n", join(c.expect).c_str());
        // release edge check for injected buttons
        mock_clear();
        Sync(sess, &si);
        bool ch = false;
        if (frame_mode && (std::string(c.label) == "B (back)" || std::string(c.label) == "Y (inventory)")) {
            const char* a = c.label[0] == 'B' ? "abutton" : "bbutton";
            bool st = active(a, 0, &ch);
            bool ok2 = !st && ch;
            if (!ok2) failures++;
            printf("%-5s %-18s    release edge %s/left: state=%d changed=%d\n", ok2 ? "OK" : "FAIL", "", a, (int)st, (int)ch);
        }
    }
    // trigger_aims sequence (defaults: fire_delay 150 ms, aim_hold 800 ms)
    {
        auto chk = [&](const char* label, bool grip, bool trig) {
            bool g = active("grip", 1), t = active("trigger", 1);
            bool ok = g == grip && t == trig;
            if (!ok) failures++;
            printf("%-5s %-34s grip/right=%d trigger/right=%d (want %d %d)\n", ok ? "OK" : "FAIL", label, (int)g, (int)t, (int)grip, (int)trig);
        };
        const char* T = "/user/hand/right/input/trigger/value";
        mock_clear(); Sync(sess, &si); Sleep(900); Sync(sess, &si);
        mock_set(T, 1.0f); Sync(sess, &si); chk("RT press: aims, fire delayed", true, false);
        Sleep(200); Sync(sess, &si); chk("RT held 200 ms: fires", true, true);
        mock_clear(); Sync(sess, &si); chk("RT released: still aiming", true, false);
        Sleep(100); mock_set(T, 1.0f); Sync(sess, &si); chk("RT again within hold: fires at once", true, true);
        mock_clear(); Sync(sess, &si); Sleep(900); Sync(sess, &si); chk("900 ms later: aim released", false, false);
    }
    // menu D-pad -> left stick (test forces "no player" by env)
    if (getenv("RE2_FRAME_TEST_NO_PLAYER")) {
        mock_clear(); mock_set("/user/hand/left/input/dpad_up/click", 1.0f); Sync(sess, &si);
        XrVector2f v = stick(0);
        bool ok = v.x == 0.0f && v.y == 1.0f;
        if (!ok) failures++;
        printf("%-5s menu D-up -> left stick x=%.2f y=%.2f\n", ok ? "OK" : "FAIL", v.x, v.y);
        mock_clear(); Sync(sess, &si);
    }
    // sticks
    for (int h = 0; h < 2; ++h) {
        mock_clear();
        std::string p = h ? "/user/hand/right/input/thumbstick" : "/user/hand/left/input/thumbstick";
        mock_set((p + "/x").c_str(), 0.0f);
        mock_set((p + "/y").c_str(), 1.0f);
        Sync(sess, &si);
        XrVector2f v = stick(h);
        bool ok = !frame_mode || (v.x == 0.0f && v.y == 1.0f);
        if (!ok) failures++;
        printf("%-5s %s stick up -> x=%.2f y=%.2f\n", ok ? "OK" : "FAIL", h ? "right" : "left", v.x, v.y);
    }
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    LocateSpace(spaces[1], spaces[0], 0, &loc);
    printf("pose right: %.3f %.3f %.3f\n", loc.pose.position.x, loc.pose.position.y, loc.pose.position.z);
    printf("RESULT: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 2 : 0;
}
