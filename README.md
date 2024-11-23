# imgui-borderless-win32
Combines win32 [BorderlessWindows](https://github.com/melak47/BorderlessWindow) with a [transparent opengl rendering context](https://stackoverflow.com/questions/4052940/how-to-make-an-opengl-rendering-context-with-transparent-background) to achieve advanced Windows DWM behavior for ImGui viewports.

| DWM Snapping | DWM Composition | DWM Accent Policy | Continuous Draw |
| :---: | :---: | :---: | :---: |
| ![](res/dwm_drag_snap.gif)  | ![](res/dwm_composition_attributes.gif)  | ![](res/dwm_accent_policy.gif) | ![](res/continuous_draw.gif) |
| drag-snap from client area | alpha separate from glClearColor | aero, mica, acrylic | drag, resize, hold |

## Demo
To try out the example you can clone the repository and its submodule:
```bash
git clone --recursive https://github.com/Stehfyn/imgui-borderless-win32
```
Or do the following if you cloned the repo non-recursively already:
```bash
cd imgui-borderless-win32
git submodule update --init --recursive
```
Then open `imgui-borderless-win32.sln` with `Visual Studio` and build, or with the `Developer Command Prompt for VS` just do
```bash
cd imgui-borderless-win32
msbuild /m /p:Configuration=Release .
```
## Concept
To enable dragging from some custom client area, our `WndProc` needs to return `HTCAPTION` when we know we're not over an imgui window. Therefore our `WndProc` needs to do those hittests with knowledge of those window rects:
```cpp
// ...
// (In Render Loop) Update imgui window rects for hit testing
{
    ImVec2 origin = { 0, 0 };
    if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) // Only apply offset if Multi-viewports are not enabled
    {
        RECT r;
        GetWindowRect(hWnd, &r); // Get ScreenPos offset
        origin = { (float)r.left, (float)r.top };
    }

    // EXAMPLE:
    // Add imgui windows that aren't default rects/dockspaces/windows over viewports,
    // etc. to client area whitelist, but explicitly include imgui demo
    std::vector<RECT> WindowRects;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
    {
        if(window->Active)
        { 
            if ((!(std::string(window->Name).find("Default") != std::string::npos)  &&
                (!(std::string(window->Name).find("Dock")    != std::string::npos)) &&
                (!(std::string(window->Name).find("Menu")    != std::string::npos)) &&
                (!(std::string(window->Name).find("WindowOverViewport") != std::string::npos))) ||
                (std::string(window->Name).find("Dear ImGui Demo") != std::string::npos))
            {
                ImVec2 pos  = window->Pos;
                ImVec2 size = window->Size;
                RECT   rect = { (LONG)(origin.x + pos.x),
                                (LONG)(origin.y + pos.y),
                                (LONG)(origin.x + (pos.x + size.x)),
                                (LONG)(origin.y + (pos.y + size.y)) };

                WindowRects.push_back(rect);
            }
        }
    }
    g_ClientCustomClientArea = std::move(WindowRects);
}

// ... (In the WndProc) Update imgui window rects for hit testing
case WM_NCHITTEST: {
    switch (result) {
    case left:           return HTLEFT;
    case right:          return HTRIGHT;
    case top:            return HTTOP;
    case bottom:         return HTBOTTOM;
    case top | left:     return HTTOPLEFT;
    case top | right:    return HTTOPRIGHT;
    case bottom | left:  return HTBOTTOMLEFT;
    case bottom | right: return HTBOTTOMRIGHT;
    case client: {
        for (RECT rect : g_ClientCustomClientArea)
            if (PtInRect(&rect, cursor)) return HTCLIENT;
        return HTCAPTION;
    }
    default: return HTNOWHERE;
    }
}
```

## Notes on SetWindowCompositionAttribute API
It's undocumented, and has varying behavior and performance bugs across different Windows 10 Builds and Windows 11. This demo has default flag values that work great for **Windows 10 Build 19044**, but some research and testing is required for feature parity across different versions. The following links may be helpful in understanding more about the `SetWindowCompositionAttribute` API:

- https://github.com/sylveon/windows-swca
- https://github.com/Maplespe/ExplorerBlurMica
- https://github.com/TranslucentTB/TranslucentTB
- https://gist.github.com/sylveon/9c199bb6684fe7dffcba1e3d383fb609
