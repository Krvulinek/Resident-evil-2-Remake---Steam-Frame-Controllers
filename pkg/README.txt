re2_frame_controllers 1.1
=========================

Steam Frame controller support for Resident Evil 2 (2019, RT build) in VR
through REFramework VR (praydog). Works with BOTH runtimes:

  - OpenXR (REFramework uses OpenXR when openxr_loader.dll is in the game
    folder and openvr_api.dll is NOT)
  - OpenVR / SteamVR (REFramework uses OpenVR when openvr_api.dll is in the
    game folder)

Tested in the headset (Steam Frame) on both runtimes.


Requirements
------------
- Resident Evil 2 (2019), RT build (re2.exe)
- REFramework with VR (dinput8.dll in the game folder)
- Steam Frame + SteamVR
- The game must NOT run as administrator (OpenXR part)


Install
-------
Copy the "reframework" folder from this archive into the game folder
(next to re2.exe). You should get:

  reframework/plugins/re2_frame_controllers.dll
  reframework/plugins/re2_frame_controllers.ini
  reframework/plugins/re2_frame_controllers/re2_frame_controllers.json

Nothing else to do. No registry changes, no installer.


Controls (Xbox-like layout)
---------------------------
  Frame button        Game action
  ------------------  -----------------------------------------
  A                   Interact / confirm
  B                   Back / cancel
  X                   Reload / skip cutscene
  Y                   Inventory
  LB (left bumper)    Sub weapon (hold), menu tab left
  RB (right bumper)   Change ammo, menu tab right (aims briefly in game)
  D-pad               Weapon shortcuts; in menus also works as the left stick
  LB + D-pad          Swap sub weapon
  View                Map
  Menu                Pause
  L3                  Run
  R3                  Reset camera
  Right trigger       Attack (aim with the right grip; see trigger_aims)
  Right grip          Aim, menu tab right
  Left trigger        Defense item / QTE
  Left grip           Support hand (two-handed grip), sub weapon
  Left stick          Move
  Right stick         Turn / camera

Why RB aims for a moment: REFramework sends "menu tab right" only together
with "aim" (both come from the right grip), so RB drives both, also in game.
Change it in the INI if you prefer.

Menus: REFramework sends only up/down from the D-pad (no left/right, no page
buttons). While a menu is open (title menu, pause, inventory, files, the
viewer of a newly picked-up document; not the map, where the D-pad changes
floors) the D-pad also moves the left stick,
which the menus follow (menu_dpad_stick). The plugin reads this from the game
(GUIMaster.get_IsGuiOpen, get_IsOpenMap, isBusyFile). In gameplay it is
never active.

Optional: RE2 only fires while aiming. With trigger_aims = 1 the right
trigger also raises the weapon (off by default).


Customization
-------------
Edit re2_frame_controllers.ini, [bindings] section:

  action = path[, path ...]

The same line changes BOTH runtimes. An action listed there replaces all of
its built-in bindings; an empty value unbinds it. The file lists all action
names and Frame paths. Examples:

  re2_toggle_flashlight = /user/hand/left/input/thumbstick/click
  re2_quickturn = /user/hand/right/input/b/click

[options]: menu_dpad_stick, trigger_aims, fire_delay_ms, aim_hold_ms
(both runtimes), overlay_pointer, pointer_scroll, pointer_keep_input,
pointer_uv_mode, pointer_hold_menu, pointer_fix_cursor (OpenVR menu).

[pose] (OpenXR only) moves/rotates the virtual hands relative to the
controller grip. It is re-read once a second, so you can tune it in game.


How it works
------------
OpenXR: the DLL is also an OpenXR API layer. The plugin enables it for the
game process only. It turns on XR_VALVE_frame_controller_interaction, adds a
Frame binding set, reports the Touch profile to REFramework (otherwise the
sticks read 0), and feeds Frame B/Y (right controller) into the left-hand
actions RE2 expects.

OpenVR: the DLL hooks SteamVR's IVRInput. When REFramework writes its
actions.json, the plugin adds a "frame_controller" default binding and writes
bindings_frame_controller.json next to it. Frame B/Y are bound to two actions
RE2 does not use (block, heal) and fed into the left-hand actions.


Troubleshooting
---------------
The release DLL writes no log. A debug build (same name, built with
DEBUG_LOG=1) writes reframework/plugins/re2_frame_controllers_log.txt
(rewritten on each start); replace the release DLL with it to get a log.
Also see re2_framework_log.txt in the game folder.

  OpenXR, look for:
    "Runtime offers XR_VALVE_frame_controller_interaction: yes"
    "Frame suggestion OK"            (any "rejected:" line = a bad path)
    "INTERACTION_PROFILE_CHANGED ... frame_controller_valve"
    "xr raw <action>/<hand> DOWN"    one line per button press
  OpenVR, look for:
    "IVRInput_010 vtable hooks installed"
    "added frame_controller default binding"
    "B/Y injection ON"
    "ovr raw <action>/<hand> DOWN  origin: ..."   origin = the physical input

Nothing in the log: the plugin did not load. Check the folder layout and
re2_framework_log.txt for [PluginLoader].

OpenVR, buttons still wrong: SteamVR may keep a binding you chose before.
Open SteamVR > Settings > Controllers > Manage Controller Bindings, pick
Resident Evil 2, and select the default binding ("frame_controller_
re2_frame_controllers").

OpenXR, layer not loaded: do not run the game or Steam as administrator.


Limits
------
- Tested: RE2 RT build, Steam Frame, SteamVR, OpenXR loader 1.0.22 and
  OpenVR (IVRInput_010), with the REFramework build current on 2026-11-01.
- The menu detection uses RE2's GUIMaster (get_IsGuiOpen, get_IsOpenMap,
  isBusyFile)
  through the REFramework plugin SDK. If a REFramework update breaks it, the
  D-pad in menus falls back to up/down only; everything else keeps working.
- OpenXR part does not work when the game runs as administrator.
- OpenVR hook supports IVRInput_010 (the version REFramework uses).
- Quick turn (B + stick back) depends on the game; it is not a separate
  binding by default.
- [pose] correction is OpenXR only.
- REFramework's in-headset menu exists on OpenVR only (on OpenXR use the
  desktop: Insert key + mouse). SteamVR shows no laser for it with Steam
  Frame controllers, so the plugin provides the pointer (overlay_pointer):
  aim the right controller at the menu, right trigger = click, right stick =
  scroll. No laser line is drawn; enable "Draw Cursor With Menu Open" in
  REFramework to see the cursor. While the pointer is on the menu, the
  desktop mouse position is not used by REFramework (pointer_fix_cursor),
  otherwise ImGui would keep jumping to the desktop mouse.


Credits
-------
- praydog: REFramework (https://github.com/praydog/REFramework)
- Khronos Group: OpenXR SDK headers
- Valve: OpenVR headers, Steam Frame input documentation
- Tsuda Kageyu: MinHook (BSD 2-Clause)
