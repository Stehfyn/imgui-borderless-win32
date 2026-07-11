# Dear Bindings Migration Notes

This project was switched from the missing `dcimgui` project/library dependency
to generated bindings from `dearimgui/dear_bindings`.

Changes made:

- Updated `external/dear_bindings` to `v0.21` / `c9ff649`.
- Set `.gitmodules` so `external/dear_bindings` tracks `main`.
- Generated Dear Bindings outputs under `generated/dear_bindings/`.
- Updated `main.c` to include generated `dcimgui.h` and `dcimgui_internal.h`
  directly.
- Removed the missing `dcimgui` project and `dcimgui.lib` dependency from the
  solution/project.
- Added generated binding sources, Dear ImGui core sources, and the Win32 /
  OpenGL3 backend sources to the Visual Studio project.
- Kept the local WGL extension subset in `oglwindow.h` and removed direct
  `gl/wglext.h` includes.
- Updated the nested `imgui` checkout to current `origin/docking` core/backend
  files while preserving the pre-existing dirty example-project edits.

Verification:

- `Debug|x64` solution build succeeds.
- Normal `Release|x64` output was blocked because
  `x64\Release\imgui-borderless-win32.exe` was running as PID `57000`.
- `Release|x64` succeeds when built to a temporary `OutDir`, so the code
  compiles and links.

Useful paths:

- `generated/dear_bindings/`
- `external/dear_bindings/`
- `imgui-borderless-win32.vcxproj`
- `imgui-borderless-win32.sln`
- `oglwindow.h`
