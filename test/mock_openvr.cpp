// mock_openvr.cpp - fake openvr_api.dll with a real vr::IVRInput (IVRInput_010
// header from REFramework). Behaves like SteamVR with a Steam Frame: on
// SetActionManifestPath it picks the frame_controller default binding, parses
// it (strict JSON) and answers action queries from "physical" inputs set by
// the test host via mock_vr_set().
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <openvr.h>
#include <json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace vr;
using json = nlohmann::json;

static std::string low(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

struct Src { std::string path, mode, input, output; int hand; uint64_t origin; };

class MockInput : public IVRInput {
public:
    std::map<std::string, uint64_t> actions;  // lower full name -> handle
    std::map<uint64_t, std::string> names;
    std::vector<Src> srcs;
    std::map<std::string, float> phys, phys_prev, snap;
    std::map<uint64_t, std::string> origins;
    bool frame_binding = false;

#include "ivrinput_stubs.inc"
};

static MockInput g_in;
extern "C" __declspec(dllexport) void mock_vr_set(const char* p, float v) { g_in.phys[p] = v; }
extern "C" __declspec(dllexport) void mock_vr_clear() { g_in.phys.clear(); }
extern "C" __declspec(dllexport) int mock_vr_frame_binding() { return g_in.frame_binding ? 1 : 0; }

EVRInputError MockInput::SetActionManifestPath(const char* p) {
    std::string mp = p;
    std::string dir = mp.substr(0, mp.find_last_of("\\/"));
    json m;
    try { m = json::parse(std::ifstream(mp)); } catch (std::exception& e) { printf("[mockvr] manifest JSON error: %s\n", e.what()); return VRInputError_InvalidParam; }
    uint64_t h = 1000;
    for (auto& a : m["actions"]) { std::string n = low(a["name"]); actions[n] = h; names[h] = n; ++h; }
    std::string url;
    for (auto& d : m["default_bindings"]) if (d["controller_type"] == "frame_controller") url = d["binding_url"];
    printf("[mockvr] manifest: %zu actions, frame_controller binding: %s\n", m["actions"].size(), url.empty() ? "NONE (SteamVR would remap Touch)" : url.c_str());
    if (url.empty()) return VRInputError_None;
    json b;
    try { b = json::parse(std::ifstream(dir + "\\" + url)); } catch (std::exception& e) { printf("[mockvr] binding JSON error: %s\n", e.what()); return VRInputError_None; }
    if (b["controller_type"] != "frame_controller") { printf("[mockvr] binding controller_type wrong\n"); return VRInputError_None; }
    frame_binding = true;
    uint64_t o = 5000;
    for (auto& s : b["bindings"]["/actions/default"]["sources"]) {
        for (auto& [in, out] : s["inputs"].items()) {
            Src x{s["path"], s["mode"], in, low(out["output"]), 0, o++};
            x.hand = x.path.rfind("/user/hand/left/", 0) == 0 ? 1 : 2;
            if (!actions.count(x.output)) printf("[mockvr] WARNING binding to unknown action %s\n", x.output.c_str());
            origins[x.origin] = x.path + " (" + x.input + ")";
            srcs.push_back(x);
        }
    }
    printf("[mockvr] frame binding loaded: %zu sources, %zu poses, %zu haptics\n", srcs.size(),
           b["bindings"]["/actions/default"]["poses"].size(), b["bindings"]["/actions/default"]["haptics"].size());
    return VRInputError_None;
}
EVRInputError MockInput::GetActionSetHandle(const char*, VRActionSetHandle_t* h) { *h = 1; return VRInputError_None; }
EVRInputError MockInput::GetActionHandle(const char* n, VRActionHandle_t* h) {
    auto it = actions.find(low(n));
    if (it == actions.end()) { *h = 0; return VRInputError_NameNotFound; }
    *h = it->second;
    return VRInputError_None;
}
EVRInputError MockInput::GetInputSourceHandle(const char* n, VRInputValueHandle_t* h) {
    std::string s = n;
    *h = s == "/user/hand/left" ? 1 : (s == "/user/hand/right" ? 2 : 3);
    return VRInputError_None;
}
EVRInputError MockInput::UpdateActionState(VRActiveActionSet_t*, uint32_t, uint32_t) { phys_prev = snap; snap = phys; return VRInputError_None; }
EVRInputError MockInput::GetDigitalActionData(VRActionHandle_t a, InputDigitalActionData_t* d, uint32_t sz, VRInputValueHandle_t r) {
    if (sz != sizeof(InputDigitalActionData_t)) return VRInputError_WrongType;
    *d = {};
    bool now = false, was = false;
    for (auto& s : srcs) {
        if (actions[s.output] != a || s.input == "position") continue;
        if (r && r != (uint64_t)s.hand) continue;
        d->bActive = true;
        if (snap[s.path] > 0.5f) { now = true; d->activeOrigin = s.origin; }
        if (phys_prev[s.path] > 0.5f) was = true;
    }
    d->bState = now;
    d->bChanged = now != was;
    return VRInputError_None;
}
EVRInputError MockInput::GetAnalogActionData(VRActionHandle_t a, InputAnalogActionData_t* d, uint32_t sz, VRInputValueHandle_t r) {
    if (sz != sizeof(InputAnalogActionData_t)) return VRInputError_WrongType;
    *d = {};
    for (auto& s : srcs) {
        if (actions[s.output] != a || s.input != "position") continue;
        if (r && r != (uint64_t)s.hand) continue;
        d->bActive = true;
        d->x = snap[s.path + "/x"];
        d->y = snap[s.path + "/y"];
    }
    return VRInputError_None;
}
EVRInputError MockInput::GetOriginLocalizedName(VRInputValueHandle_t o, char* buf, uint32_t n, int32_t) {
    snprintf(buf, n, "Frame %s", origins.count(o) ? origins[o].c_str() : "?");
    return VRInputError_None;
}


// ---- overlay / system / render models (menu pointer test) ----
class MockOverlay : public IVROverlay {
public:
#include "ivroverlay_stubs.inc"
};
class MockSystem : public IVRSystem {
public:
#include "ivrsystem_stubs.inc"
};
class MockRM : public IVRRenderModels {
public:
#include "ivrrm_stubs.inc"
};
static MockOverlay g_ov;
static MockSystem g_sys;
static MockRM g_rm;
// Overlay = plane z = -1, x/y in [-0.5, 0.5]; mouse scale 1920 x 1080.
static bool g_flag_set = false;  // what the runtime actually received
extern "C" __declspec(dllexport) int mock_vr_interactive_flag() { return g_flag_set ? 1 : 0; }
EVROverlayError MockOverlay::SetOverlayFlag(VROverlayHandle_t, VROverlayFlags f, bool on) {
    if (f == VROverlayFlags_MakeOverlaysInteractiveIfVisible) g_flag_set = on;
    return VROverlayError_None;
}
EVROverlayError MockOverlay::GetOverlayFlag(VROverlayHandle_t, VROverlayFlags f, bool* b) {
    *b = (f == VROverlayFlags_MakeOverlaysInteractiveIfVisible) && g_flag_set;
    return VROverlayError_None;
}
bool MockOverlay::PollNextOverlayEvent(VROverlayHandle_t, VREvent_t*, uint32_t) { return false; }  // SteamVR sends nothing (as seen with Frame)
EVROverlayError MockOverlay::GetOverlayMouseScale(VROverlayHandle_t, HmdVector2_t* v) { v->v[0] = 1920; v->v[1] = 1080; return VROverlayError_None; }
bool MockOverlay::ComputeOverlayIntersection(VROverlayHandle_t, const VROverlayIntersectionParams_t* p, VROverlayIntersectionResults_t* r) {
    if (p->vDirection.v[2] >= -1e-4f) return false;
    float t = (-1.0f - p->vSource.v[2]) / p->vDirection.v[2];
    float x = p->vSource.v[0] + t * p->vDirection.v[0], y = p->vSource.v[1] + t * p->vDirection.v[1];
    if (x < -0.5f || x > 0.5f || y < -0.5f || y > 0.5f) return false;
    r->vUVs.v[0] = x + 0.5f;
    r->vUVs.v[1] = y + 0.5f;
    return true;
}
void MockSystem::GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin, float, TrackedDevicePose_t* a, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) a[i] = {};
    TrackedDevicePose_t& r = a[2];
    r.bPoseIsValid = true;
    r.mDeviceToAbsoluteTracking.m[0][0] = r.mDeviceToAbsoluteTracking.m[1][1] = r.mDeviceToAbsoluteTracking.m[2][2] = 1.0f;
    r.mDeviceToAbsoluteTracking.m[0][3] = g_in.phys["ptr/x"];
    r.mDeviceToAbsoluteTracking.m[1][3] = g_in.phys["ptr/y"];
}
TrackedDeviceIndex_t MockSystem::GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole role) {
    return role == TrackedControllerRole_RightHand ? 2 : 1;
}
uint32_t MockSystem::GetStringTrackedDeviceProperty(TrackedDeviceIndex_t, ETrackedDeviceProperty, char* b, uint32_t n, ETrackedPropertyError* e) {
    snprintf(b, n, "valve_frame_right");
    if (e) *e = TrackedProp_Success;
    return 18;
}
bool MockRM::GetComponentStateForDevicePath(const char*, const char* comp, VRInputValueHandle_t, const RenderModel_ControllerMode_State_t*,
                                            RenderModel_ComponentState_t* cs) {
    if (strcmp(comp, "tip") != 0) return false;
    *cs = {};
    cs->mTrackingToComponentLocal.m[0][0] = cs->mTrackingToComponentLocal.m[1][1] = cs->mTrackingToComponentLocal.m[2][2] = 1.0f;
    return true;
}

extern "C" __declspec(dllexport) void* VR_GetGenericInterface(const char* ver, EVRInitError* err) {
    if (err) *err = VRInitError_None;
    if (strcmp(ver, "IVRInput_010") == 0) return static_cast<IVRInput*>(&g_in);
    if (strcmp(ver, "IVROverlay_025") == 0) return static_cast<IVROverlay*>(&g_ov);
    if (strcmp(ver, "IVRSystem_022") == 0) return static_cast<IVRSystem*>(&g_sys);
    if (strcmp(ver, "IVRRenderModels_006") == 0) return static_cast<IVRRenderModels*>(&g_rm);
    if (err) *err = VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
