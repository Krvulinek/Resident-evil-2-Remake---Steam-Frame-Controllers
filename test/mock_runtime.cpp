// mock_runtime.cpp - minimal OpenXR runtime for testing the layer under Wine.
// Simulates a Steam Frame: offers XR_VALVE_frame_controller_interaction, validates
// suggested paths per profile, and derives action states from "physical" inputs
// set by the test host via the exported mock_set().
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static const char* FRAME = "/interaction_profiles/valve/frame_controller_valve";
static const char* TOUCH = "/interaction_profiles/oculus/touch_controller";

static std::vector<std::string> g_paths;
static bool g_frame_ext = false;
static bool g_offer_frame = true;
static std::map<std::string, float> g_phys;  // physical input path -> value

struct Action { std::string name; XrActionType type; uint64_t set; };
static std::map<uint64_t, Action> g_actions;
static std::set<uint64_t> g_sets;
static std::set<uint64_t> g_attached;
static std::set<uint64_t> g_synced;
static std::map<XrPath, std::vector<XrActionSuggestedBinding>> g_suggest;
static XrPath g_active_profile = XR_NULL_PATH;
static bool g_attached_done = false, g_event_sent = false;
static uint64_t g_next = 100;
static std::map<std::pair<uint64_t, int>, float> g_cur, g_prev;  // (action, hand) -> value
static std::map<std::pair<uint64_t, int>, XrVector2f> g_cur2;
static std::map<std::pair<uint64_t, int>, bool> g_bound;

static XrPath P(const std::string& s) {
    for (size_t i = 0; i < g_paths.size(); ++i) if (g_paths[i] == s) return i + 1;
    g_paths.push_back(s);
    return g_paths.size();
}
static std::string S(XrPath p) { return p && p <= g_paths.size() ? g_paths[p - 1] : ""; }

extern "C" __declspec(dllexport) void mock_set(const char* path, float v) { g_phys[path] = v; }
extern "C" __declspec(dllexport) void mock_clear() { g_phys.clear(); }

static bool valid_path(const std::string& prof, const std::string& p) {
    static const std::set<std::string> frame_r = {"a/click","a/touch","b/click","b/touch","x/click","x/touch","y/click","y/touch",
        "menu/click","system/click","bumper/click","bumper/touch","squeeze/value","squeeze/click","trigger/value","trigger/click",
        "trigger/touch","thumbstick","thumbstick/x","thumbstick/y","thumbstick/click","thumbstick/touch","grip/pose","aim/pose"};
    static const std::set<std::string> frame_l = {"dpad_up/click","dpad_down/click","dpad_left/click","dpad_right/click",
        "dpad_up/touch","view/click","system/click","bumper/click","bumper/touch","squeeze/value","squeeze/click","trigger/value",
        "trigger/click","trigger/touch","thumbstick","thumbstick/x","thumbstick/y","thumbstick/click","thumbstick/touch","grip/pose","aim/pose"};
    static const std::set<std::string> touch_l = {"x/click","x/touch","y/click","y/touch","menu/click","squeeze/value","trigger/value",
        "trigger/touch","thumbstick","thumbstick/click","thumbstick/touch","grip/pose","aim/pose"};
    static const std::set<std::string> touch_r = {"a/click","a/touch","b/click","b/touch","system/click","squeeze/value","trigger/value",
        "trigger/touch","thumbstick","thumbstick/click","thumbstick/touch","grip/pose","aim/pose"};
    bool left = p.rfind("/user/hand/left/", 0) == 0, right = p.rfind("/user/hand/right/", 0) == 0;
    if (!left && !right) return false;
    std::string rest = p.substr(left ? 16 : 17);
    if (rest == "output/haptic") return true;
    if (rest.rfind("input/", 0) != 0) return false;
    rest = rest.substr(6);
    if (prof == FRAME) return left ? frame_l.count(rest) : frame_r.count(rest);
    if (prof == TOUCH) return left ? touch_l.count(rest) : touch_r.count(rest);
    return false;
}

#define RT XRAPI_ATTR XrResult XRAPI_CALL
static RT m_EnumExt(const char*, uint32_t cap, uint32_t* n, XrExtensionProperties* p) {
    std::vector<const char*> e = {"XR_KHR_D3D11_enable"};
    if (g_offer_frame) e.push_back("XR_VALVE_frame_controller_interaction");
    *n = (uint32_t)e.size();
    if (cap == 0) return XR_SUCCESS;
    if (cap < e.size()) return XR_ERROR_SIZE_INSUFFICIENT;
    for (size_t i = 0; i < e.size(); ++i) { strcpy(p[i].extensionName, e[i]); p[i].extensionVersion = 1; }
    return XR_SUCCESS;
}
static RT m_CreateInstance(const XrInstanceCreateInfo* ci, XrInstance* out) {
    g_frame_ext = false;
    for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i) {
        std::string e = ci->enabledExtensionNames[i];
        if (e == "XR_VALVE_frame_controller_interaction") {
            if (!g_offer_frame) return XR_ERROR_EXTENSION_NOT_PRESENT;
            g_frame_ext = true;
        } else if (e != "XR_KHR_D3D11_enable") return XR_ERROR_EXTENSION_NOT_PRESENT;
    }
    printf("[mock] xrCreateInstance, frame ext %s\n", g_frame_ext ? "ENABLED" : "off");
    *out = (XrInstance)(uintptr_t)1;
    return XR_SUCCESS;
}
static RT m_DestroyInstance(XrInstance) { return XR_SUCCESS; }
static RT m_StringToPath(XrInstance, const char* s, XrPath* p) { *p = P(s); return XR_SUCCESS; }
static RT m_PathToString(XrInstance, XrPath p, uint32_t cap, uint32_t* n, char* buf) {
    std::string s = S(p);
    if (s.empty()) return XR_ERROR_PATH_INVALID;
    *n = (uint32_t)s.size() + 1;
    if (cap == 0) return XR_SUCCESS;
    if (cap < *n) return XR_ERROR_SIZE_INSUFFICIENT;
    strcpy(buf, s.c_str());
    return XR_SUCCESS;
}
static RT m_GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* id) { *id = 1; return XR_SUCCESS; }
static RT m_CreateActionSet(XrInstance, const XrActionSetCreateInfo* ci, XrActionSet* out) {
    uint64_t h = g_next++;
    g_sets.insert(h);
    *out = (XrActionSet)(uintptr_t)h;
    printf("[mock] action set %s\n", ci->actionSetName);
    return XR_SUCCESS;
}
static RT m_CreateAction(XrActionSet set, const XrActionCreateInfo* ci, XrAction* out) {
    if (g_attached.count((uint64_t)(uintptr_t)set)) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    uint64_t h = g_next++;
    g_actions[h] = {ci->actionName, ci->actionType, (uint64_t)(uintptr_t)set};
    *out = (XrAction)(uintptr_t)h;
    return XR_SUCCESS;
}
static RT m_Suggest(XrInstance, const XrInteractionProfileSuggestedBinding* sb) {
    if (g_attached_done) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    std::string prof = S(sb->interactionProfile);
    if (prof == FRAME && !g_frame_ext) return XR_ERROR_PATH_UNSUPPORTED;
    if (prof != FRAME && prof != TOUCH) return XR_ERROR_PATH_UNSUPPORTED;
    for (uint32_t i = 0; i < sb->countSuggestedBindings; ++i)
        if (!valid_path(prof, S(sb->suggestedBindings[i].binding))) return XR_ERROR_PATH_UNSUPPORTED;
    g_suggest[sb->interactionProfile].assign(sb->suggestedBindings, sb->suggestedBindings + sb->countSuggestedBindings);
    return XR_SUCCESS;
}
static RT m_CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* s) { *s = (XrSession)(uintptr_t)7; return XR_SUCCESS; }
static RT m_Attach(XrSession, const XrSessionActionSetsAttachInfo* ai) {
    if (g_attached_done) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    for (uint32_t i = 0; i < ai->countActionSets; ++i) g_attached.insert((uint64_t)(uintptr_t)ai->actionSets[i]);
    g_attached_done = true;
    XrPath f = P(FRAME), t = P(TOUCH);
    g_active_profile = g_suggest.count(f) ? f : (g_suggest.count(t) ? t : XR_NULL_PATH);
    printf("[mock] attach %u sets, active profile %s (%u bindings)\n", ai->countActionSets, S(g_active_profile).c_str(),
           g_active_profile ? (unsigned)g_suggest[g_active_profile].size() : 0u);
    return XR_SUCCESS;
}
static int hand_of(const std::string& p) { return p.rfind("/user/hand/left/", 0) == 0 ? 0 : 1; }
static RT m_Sync(XrSession, const XrActionsSyncInfo* si) {
    g_synced.clear();
    for (uint32_t i = 0; i < si->countActiveActionSets; ++i) {
        uint64_t s = (uint64_t)(uintptr_t)si->activeActionSets[i].actionSet;
        if (!g_attached.count(s)) return XR_ERROR_ACTIONSET_NOT_ATTACHED;
        g_synced.insert(s);
    }
    g_prev = g_cur;
    g_cur.clear();
    g_cur2.clear();
    g_bound.clear();
    if (!g_active_profile) return XR_SUCCESS;
    for (auto& b : g_suggest[g_active_profile]) {
        uint64_t a = (uint64_t)(uintptr_t)b.action;
        if (!g_synced.count(g_actions[a].set)) continue;
        std::string p = S(b.binding);
        int h = hand_of(p);
        g_bound[{a, h}] = true;
        if (g_actions[a].type == XR_ACTION_TYPE_VECTOR2F_INPUT) {
            XrVector2f& v = g_cur2[{a, h}];
            float x = g_phys[p + "/x"], y = g_phys[p + "/y"];
            if (x * x + y * y > v.x * v.x + v.y * v.y) v = {x, y};
        } else {
            float v = g_phys.count(p) ? g_phys[p] : 0.0f;
            float& c = g_cur[{a, h}];
            if (v > c) c = v;
        }
    }
    return XR_SUCCESS;
}
static int sub_hand(XrPath sub) {
    std::string s = S(sub);
    if (s == "/user/hand/left") return 0;
    if (s == "/user/hand/right") return 1;
    return -1;
}
static RT m_GetBool(XrSession, const XrActionStateGetInfo* gi, XrActionStateBoolean* st) {
    uint64_t a = (uint64_t)(uintptr_t)gi->action;
    int h = sub_hand(gi->subactionPath);
    float now = 0, was = 0;
    bool active = false;
    for (int i = 0; i < 2; ++i) {
        if (h >= 0 && i != h) continue;
        active |= g_bound.count({a, i}) > 0;
        now = std::max(now, g_cur[{a, i}]);
        was = std::max(was, g_prev[{a, i}]);
    }
    st->isActive = active;
    st->currentState = now > 0.5f;
    st->changedSinceLastSync = (now > 0.5f) != (was > 0.5f);
    st->lastChangeTime = 0;
    return XR_SUCCESS;
}
static RT m_GetFloat(XrSession, const XrActionStateGetInfo* gi, XrActionStateFloat* st) {
    uint64_t a = (uint64_t)(uintptr_t)gi->action;
    int h = sub_hand(gi->subactionPath);
    float now = 0;
    bool active = false;
    for (int i = 0; i < 2; ++i) {
        if (h >= 0 && i != h) continue;
        active |= g_bound.count({a, i}) > 0;
        now = std::max(now, g_cur[{a, i}]);
    }
    st->isActive = active;
    st->currentState = now;
    st->changedSinceLastSync = XR_FALSE;
    return XR_SUCCESS;
}
static RT m_GetVec2(XrSession, const XrActionStateGetInfo* gi, XrActionStateVector2f* st) {
    uint64_t a = (uint64_t)(uintptr_t)gi->action;
    int h = sub_hand(gi->subactionPath);
    st->isActive = XR_FALSE;
    st->currentState = {0, 0};
    for (int i = 0; i < 2; ++i) {
        if (h >= 0 && i != h) continue;
        if (g_bound.count({a, i})) { st->isActive = XR_TRUE; st->currentState = g_cur2[{a, i}]; }
    }
    return XR_SUCCESS;
}
static RT m_GetCurProfile(XrSession, XrPath, XrInteractionProfileState* st) { st->interactionProfile = g_active_profile; return XR_SUCCESS; }
static RT m_PollEvent(XrInstance, XrEventDataBuffer* ev) {
    if (g_attached_done && !g_event_sent) {
        g_event_sent = true;
        auto* e = (XrEventDataInteractionProfileChanged*)ev;
        e->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
        e->next = nullptr;
        e->session = (XrSession)(uintptr_t)7;
        return XR_SUCCESS;
    }
    return XR_EVENT_UNAVAILABLE;
}
static RT m_CreateActionSpace(XrSession, const XrActionSpaceCreateInfo*, XrSpace* sp) { *sp = (XrSpace)(uintptr_t)(g_next++); return XR_SUCCESS; }
static RT m_LocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* loc) {
    loc->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    loc->pose = {{0, 0, 0, 1}, {0, 1, 0}};
    return XR_SUCCESS;
}
static RT m_DestroySpace(XrSpace) { return XR_SUCCESS; }

static RT m_gipa(XrInstance, const char* name, PFN_xrVoidFunction* fn) {
#define F(n, f) if (!strcmp(name, n)) { *fn = (PFN_xrVoidFunction)f; return XR_SUCCESS; }
    F("xrGetInstanceProcAddr", m_gipa)
    F("xrEnumerateInstanceExtensionProperties", m_EnumExt)
    F("xrCreateInstance", m_CreateInstance)
    F("xrDestroyInstance", m_DestroyInstance)
    F("xrStringToPath", m_StringToPath)
    F("xrPathToString", m_PathToString)
    F("xrGetSystem", m_GetSystem)
    F("xrCreateActionSet", m_CreateActionSet)
    F("xrCreateAction", m_CreateAction)
    F("xrSuggestInteractionProfileBindings", m_Suggest)
    F("xrCreateSession", m_CreateSession)
    F("xrAttachSessionActionSets", m_Attach)
    F("xrSyncActions", m_Sync)
    F("xrGetActionStateBoolean", m_GetBool)
    F("xrGetActionStateFloat", m_GetFloat)
    F("xrGetActionStateVector2f", m_GetVec2)
    F("xrGetCurrentInteractionProfile", m_GetCurProfile)
    F("xrPollEvent", m_PollEvent)
    F("xrCreateActionSpace", m_CreateActionSpace)
    F("xrLocateSpace", m_LocateSpace)
    F("xrDestroySpace", m_DestroySpace)
#undef F
    *fn = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* li,
                                                                                    XrNegotiateRuntimeRequest* rq) {
    char env[8] = {};
    g_offer_frame = !(GetEnvironmentVariableA("MOCK_NO_FRAME", env, 8) > 0 && env[0] == '1');
    rq->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    rq->runtimeApiVersion = li->maxApiVersion < XR_CURRENT_API_VERSION ? li->maxApiVersion : XR_CURRENT_API_VERSION;
    rq->getInstanceProcAddr = m_gipa;
    return XR_SUCCESS;
}
