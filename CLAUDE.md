# Opposing Force 2 (op8)

Half-Life 2: Episode Two singleplayer mod on **Mapbase** (Source SDK 2013 SP fork), built with VS2022.
Steam lists it as "Opposing Force 2" (`gameinfo.txt` `game` key). The player's suit is a **PCV**
(HECU marine), not Gordon's HEV, so keep suit text brand-neutral or PCV-flavored.

Work only on the **Episodic** side:
- Game files: `sp\game\mod_episodic\` (junctioned into Steam sourcemods as `op8`). Files here override VPK content.
- Client code: `sp\src\game\client\` (HL2 HUD elements in `client\hl2\`). Server: `sp\src\game\server\`.
- Don't edit Valve's files inside VPKs; copy them into `mod_episodic` and override.

## Building (read this first; several traps)

- **`MSBuild games.sln` silently builds nothing.** VPC writes the .sln without a configuration section,
  so it reports "Build succeeded" in under a second. Build the project files directly:
  ```
  "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" sp\src\game\client\client_episodic.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
  ```
  (Same for `sp\src\game\server\server_episodic.vcxproj`.) Incremental Release only; never Clean/Rebuild unless asked.
- **Check the result**: the post-build step copies `client.dll` into `sp\game\mod_episodic\bin\`. Verify that file's timestamp.
- **"PostBuildStep FAILED ... EXE or DLL is probably running"** = the game (`hl2.exe`) has the DLL locked. The
  compile succeeded; ask the user to quit the game, then rebuild. Never kill the game yourself.
- **Changing `sp\src\public\vgui_controls\Panel.h` or `vgui2\vgui_controls\*.cpp`**: build
  `sp\src\vgui2\vgui_controls\vgui_controls.vcxproj` first (outputs `sp\src\lib\public\vgui_controls.lib`), then the client.
  Panel.h changes recompile most of the client.
- **New source files**: add them to `client_episodic.vpc` / `server_episodic.vpc`, then run `sp\src\creategameprojects.bat`
  **from cmd or PowerShell, not Git Bash**. Under Git Bash, vpc.exe silently stops partway and skips the client project.
  Explain build-script changes to the user before making them.
- Warnings are errors (e.g. C4189 unused local variable).
- All projects use the **v143** toolset (VS2022). The "Generating for Visual Studio 2013" message from VPC is meaningless.
  The VS2013 registry fix (`vpc_scripts\newer_vs_toolsets_regkey_fix.reg`) is already applied.
  `materialsystem\stdshaders\buildsdkshaders.bat` still points at VS2013 tools (only matters for custom shaders).

## Testing in game

- `hud_reloadscheme` reloads `ClientScheme.res` / `HudLayout.res`. New font files, `hudanimations*.txt` and DLLs need a full restart.
- **Archived convars are saved to `sp\game\mod_episodic\cfg\config.cfg` on quit**, so changing a ConVar's default in code
  does nothing for this user. Either have them set it in the console before quitting, or edit `config.cfg` while the game is closed.
- Screenshots from the user are unscaled game captures. Measure them pixel by pixel (e.g. PowerShell + System.Drawing)
  instead of eyeballing; this has caught real bugs.

## Game config

- The engine runs from Source SDK Base 2013 Singleplayer, which has no `hl2\cfg\skill.cfg`. The mod ships its own
  `mod_episodic\cfg\skill.cfg` and `skill_episodic.cfg`. If an entity or weapon "does nothing" (suit charger, battery,
  RPG with 0 max ammo), check the matching `sk_*` value there first; a missing skill.cfg made them all 0.

## Mapping

- Hammer: `Source SDK Base 2013 Singleplayer\bin\hammer.exe`, game config "Op8" in `bin\GameConfig.txt` (backup `.bak`).
- The user's VMFs live in the retail install, `Steam\steamapps\common\Half-Life 2\ep2\maps\`, not in this repo.
- Valve EP2 maps loaded from `ep2_pak.vpk` lose their BSP-embedded (cubemap-patched) materials. Fix: extract the .bsp
  loose into `mod_episodic\maps\`.

## HUD ("jet HUD") file map

| What | Where |
|---|---|
| Colors, fonts, font files | `mod_episodic\resource\ClientScheme.res` (backup: `*.pre-jethud.bak`) |
| Panel layout and per-panel settings | `mod_episodic\scripts\HudLayout.res` (backup: `*.pre-jethud.bak`) |
| HUD animations (recolored copies of Valve's) | `mod_episodic\scripts\hudanimations.txt`, `hudanimations_ep2.txt` |
| Numbers + plus/shield icons (`PaintIcon`) | `client\hud_numericdisplay.cpp/.h`, `client\hl2\hud_health.cpp`, `hud_battery.cpp` |
| Segmented crosshair brackets (quick info) | `client\hl2\hud_quickinfo.cpp` (`OF2_*` constants at the top of the bracket code) |
| CS:S-style crosshair, dynamic spread | `client\hud_crosshair.cpp` (`of2_crosshair*` convars) |
| Suit boot sequence | `client\hl2\hud_bootsequence.cpp` + `mod_episodic\scripts\hud_bootsequence.txt` (format documented in its header) |
| Rounded/soft backdrop drawing | `vgui2\vgui_controls\Panel.cpp` `DrawBox` + `BgFeather` / `BgCornerRadius` keys in `Panel.h` |

- Palette (scheme): green `10 204 88` (`FgColor`/`Normal`), bright `96 240 144`, amber caution `230 150 0` (`DamagedFg`/`Caution`),
  red critical `225 40 25` (`CriticalFg`, low health only). Code derives darker shades from `gHUD.m_clrNormal`.
  When changing the palette, update all of: scheme, layout literals, both animation files, boot script.
- Font: **Share Tech Mono** (OFL, license in `resource\ShareTechMono-OFL.txt`). Text fonts use `"scanlines" "2"`.
  Rejected: RomanS (Autodesk-copyrighted), Hershey (looked wrong), B612.
- Backdrops: HL2's rounded panel boxes (`PaintBackgroundType 2`), `BgColor 0 0 0 152`, `BgCornerRadius 1.3`, `BgFeather 0`.
- `suit_bootsequence` plays the boot text; auto-play on suit pickup is off (`suit_bootsequence_auto 0`).
- Code changes are marked with `OF2:` comments. Match that.

## Source gotchas we hit

- `YRES(x)` / `XRES(x)` don't parenthesize their argument: `YRES( a + b )` is wrong; write `YRES( ( a + b ) )`.
- Procedural textures from `DrawSetTextureRGBA` must be **power-of-two sized**, or the engine squashes the image into
  part of the texture (shows as a shrunken image plus the purple/black missing-texture checker). Pad and draw 1:1.
- KeyValues files don't process `\"` escapes by default. Don't put escaped quotes in `.res`/`.txt` data.
- `"PaintBackgroundType"` is a panel animation var: 0 = square fill, 2 = rounded box. `hud_ammo.cpp` re-enables backgrounds at runtime.
- `CHudElement::ShouldDraw()` runs every frame even while hidden (good place for watchers); vgui `OnThink` doesn't.
- Fonts with a `"yres"` filter don't scale proportionally; fonts without one scale from the 480-line grid.
- Share Tech Mono digit height = 0.62 of the font's line height (`GetFontTall`); the baseline is `GetFontAscent`.
- Mapbase/SDK `vpk.exe` crashes on this machine. Read VPKs with a small directory-tree parser instead.
  `hudanimations.txt` is a loose file in `Source SDK Base 2013 Singleplayer\hl2\scripts\`, not in a VPK.
- Weapon crosshairs are defined in weapon scripts (`Source SDK Base 2013 Singleplayer\hl2\scripts\weapon_*.txt`,
  `TextureData` > `"crosshair"`, the `Crosshairs` font character "Q"); ours replaces them in code.

## Working with the user

- They have a sharp eye for pixel-level detail (uneven spacing, stacked translucency in corners, 1 px misalignment).
  Pixel-exact geometry matters: whole-pixel sizes, nothing drawn twice with translucent colors.
- Explain what you'll change before larger edits; back up `.res` files before restructuring them.
- Nothing is committed unless they ask.
