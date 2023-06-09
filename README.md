# imgui-borderless-win32
Combines win32 [BorderlessWindows](https://github.com/melak47/BorderlessWindow) with a [transparent opengl rendering context](https://stackoverflow.com/questions/4052940/how-to-make-an-opengl-rendering-context-with-transparent-background) to achieve advanced Windows DWM behavior for ImGui viewports.

| DWM Snapping | DWM Composition | DWM Accent Policy |
| :---: | :---: | :---: |
| ![](res/dwm_drag_snap.gif)  | ![](res/dwm_composition_attributes.gif)  | ![](res/dwm_accent_policy.gif) |
| drag-snap from client area | alpha composition separate from glClearColor | aero, mica, acrylic |

## Concept
To enable dragging from some custom client area, our `WndProc` needs to return `HTCAPTION` when we know we're not over an imgui window. Therefore our `WndProc` needs to do those hittests with knowledge of those window rects:
```cpp
BorderlessWindow window; // Instantiate our borderless window
// ...
// (In Render Loop) Update imgui window rects for hit testing
{
    // Get ScreenPos offset
    ImGuiViewport* vp = ImGui::GetMainViewport();
    HWND handle = (HWND)vp->PlatformHandle;
    RECT r;
    GetWindowRect(handle, &r);

    // Only apply offset if Multi-viewports are not enabled
    ImVec2 origin = { (float)r.left, (float)r.top };
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        origin = { 0, 0 };
    }

    // Add imgui windows that aren't default rects/dockspaces/etc to client area whitelist, but explicitly include imgui demo
    std::vector<RECT> WindowRects;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
    {
        if ((!(std::string(window->Name).find("Default") != std::string::npos) &&
            (!(std::string(window->Name).find("Dock") != std::string::npos)) &&
            (!(std::string(window->Name).find("Menu") != std::string::npos))) ||
            (std::string(window->Name).find("Dear ImGui Demo") != std::string::npos))
        {
            ImVec2 pos = window->Pos;
            ImVec2 size = window->Size;
            RECT rect = { origin.x + pos.x, origin.y + pos.y, origin.x + (pos.x + size.x), origin.y + (pos.y + size.y) };
            WindowRects.push_back(rect);
        }
    }
    window.set_client_area(WindowRects);
}
```

## Notes on SetWindowCompositionAttribute API
It's undocumented, and has varying behavior and performance bugs across different Windows 10 Builds and Windows 11. This demo has default flag values that work great for **Windows 10 Build 19044**, but some research and testing is required for feature parity across different versions. The following links may be helpful in understanding more about the `SetWindowCompositionAttribute` API:

- https://github.com/sylveon/windows-swca
- https://github.com/TranslucentTB/TranslucentTB
- https://gist.github.com/sylveon/9c199bb6684fe7dffcba1e3d383fb609
