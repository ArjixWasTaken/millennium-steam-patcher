#include <stdafx.h>
#include <Windows.h>
#include <TlHelp32.h>
#include <Shlwapi.h>

#include <window/core/window.hpp>

#include <window/imgui/imgui.h>
#include <window/imgui/imgui_internal.h>
#include <vector>

#include <strsafe.h>
#include <window/core/memory.h>

#include <d3d9.h>
#include <d3dx9tex.h>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

#include <stdexcept>
#include <system_error>

#include <Windows.h>
#include <windowsx.h>
#include <utils/config/config.hpp>

#include <filesystem>
#include <iostream>

#include <locale>
#include <codecvt>

#include <window/interface/globals.h>
#include <window/api/installer.hpp>

static bool mousedown = false;
bool app_open = true;

bool g_headerHovered = false; 
bool g_headerHovered_1 = false;

bool g_windowOpen = false;
bool g_initReady = false;

icons icons_struct = {};
icons Window::iconsObj() { return icons_struct; }

void set_proc_theme_colors();

struct dx9_interface  { LPDIRECT3D9 ID3D9; LPDIRECT3DDEVICE9 device; D3DPRESENT_PARAMETERS params; MSG msg; } directx9;
struct overlay_window { WNDCLASSEXW wndex; HWND hwnd; LPCSTR name; } overlay;
struct app_ctx        { std::string Title; uint32_t width = 1215, height = 850; } appinfo;

void Window::setTitle(char* name) { appinfo.Title = name; }
void Window::setDimensions(ImVec2 dimensions) { appinfo.width = (int)dimensions.x; appinfo.height = (int)dimensions.y; }

HWND Window::getHWND()    { return overlay.hwnd; }

void Window::bringToFront()
{
    DWORD threadId  = ::GetCurrentThreadId();
    DWORD processId = ::GetWindowThreadProcessId(overlay.hwnd, NULL);

    ::AttachThreadInput(processId, threadId, TRUE);
    ::SetWindowPos(overlay.hwnd, HWND_TOPMOST  , 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    ::SetWindowPos(overlay.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
    ::SetForegroundWindow(overlay.hwnd);
    ::SetFocus(overlay.hwnd);
    ::SetActiveWindow(overlay.hwnd);
    ::AttachThreadInput(processId, threadId, FALSE);
}

LPDIRECT3DDEVICE9* Window::getDevice() { return &directx9.device; }

bool create_device_d3d(HWND hWnd) 
{
    if ((directx9.ID3D9 = Direct3DCreate9(D3D_SDK_VERSION)) == NULL) 
    {
        return false;
    }
    ZeroMemory(&directx9.params, sizeof(directx9.params));

    directx9.params.Windowed               = TRUE;
    directx9.params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    directx9.params.BackBufferFormat       = D3DFMT_UNKNOWN;
    directx9.params.EnableAutoDepthStencil = TRUE;
    directx9.params.AutoDepthStencilFormat = D3DFMT_D16;
    directx9.params.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;
    
    if (directx9.ID3D9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &directx9.params, &directx9.device) < 0) 
    {
        return false;
    }
    return true;
}
 
void ClearAll()
{
    if (directx9.device) { directx9.device->Release(); directx9.device = nullptr; }
    if (directx9.ID3D9)  { directx9.ID3D9->Release();  directx9.ID3D9 = nullptr;  }

    UnregisterClassW(overlay.wndex.lpszClassName, overlay.wndex.hInstance);
}

void reset_device() 
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if (directx9.device->Reset(&directx9.params) == D3DERR_INVALIDCALL) IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

static ImVec2 ScreenRes, WindowPos;

bool create_texture(PDIRECT3DTEXTURE9* out_texture, LPCVOID data_src, size_t data_size)
{
    PDIRECT3DTEXTURE9 texture;
    HRESULT hr = D3DXCreateTextureFromFileInMemory(*Window::getDevice(), data_src, data_size, &texture);
    if (hr != S_OK) return false;

    D3DSURFACE_DESC my_image_desc;
    texture->GetLevelDesc(0, &my_image_desc);
    *out_texture = texture;

    return true;
}

void wnd_center()
{
    RECT rectClient, rectWindow;
    HWND hWnd = overlay.hwnd;
    GetClientRect(hWnd, &rectClient);
    GetWindowRect(hWnd, &rectWindow);

    int width = rectWindow.right - rectWindow.left;
    int height = rectWindow.bottom - rectWindow.top;

    MoveWindow(
        hWnd,
        GetSystemMetrics(SM_CXSCREEN) / 2 - (width) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 - (height) / 2,
        rectClient.right - rectClient.left, rectClient.bottom - rectClient.top, TRUE
    );
}

HICON GetParentProcessIcon() 
{
    DWORD parentProcessId = {};
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (Process32First(hSnapshot, &pe32)) 
    {
        do {
            if (pe32.th32ProcessID == GetCurrentProcessId()) 
            {
                CloseHandle(hSnapshot);
                parentProcessId = pe32.th32ParentProcessID;
            }
        } 
        while (Process32Next(hSnapshot, &pe32));
    }

    HANDLE hParentProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, parentProcessId);

    if (hParentProcess == nullptr) 
    {
        return nullptr;
    }

    WCHAR parentProcessExePath[MAX_PATH] = { 0 };
    DWORD bufferSize                     = sizeof(parentProcessExePath) / sizeof(WCHAR);

    if (GetModuleFileName(nullptr, (LPSTR)parentProcessExePath, bufferSize) == 0) 
    {
        CloseHandle(hParentProcess);
        return nullptr;
    }

    HICON hIcon;
    ExtractIconEx((LPSTR)parentProcessExePath, 0, nullptr, &hIcon, 1);

    CloseHandle(hParentProcess);
    return hIcon;
}

void initFontAtlas(ImGuiIO* IO)
{
    auto inter = Memory::Inter;
    auto poppins = Memory::Poppins;
    auto aptos = Memory::aptosFont;

    IO->Fonts->AddFontFromMemoryTTF(aptos, sizeof aptos, 17);
    IO->Fonts->AddFontFromMemoryTTF(aptos, sizeof aptos, 13);
    IO->Fonts->AddFontFromMemoryTTF(aptos, sizeof aptos, 20);
}

void initTextures()
{
    create_texture(&icons_struct.skin_icon, Memory::themeIcon, sizeof Memory::themeIcon);
    create_texture(&icons_struct.reload_icon, Memory::icon_reload, sizeof Memory::icon_reload);
    create_texture(&icons_struct.icon_no_results, Memory::icon_no_results, sizeof Memory::icon_no_results);
    create_texture(&icons_struct.foldericon, Memory::foldericon, sizeof Memory::foldericon);
    create_texture(&icons_struct.editIcon, Memory::editIcon, sizeof Memory::editIcon);
    create_texture(&icons_struct.deleteIcon, Memory::deleteIcon, sizeof Memory::deleteIcon);
    create_texture(&icons_struct.xbtn, Memory::xbtn, sizeof Memory::xbtn);
}

bool Application::Destroy()
{
    PostQuitMessage(0);

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    //ImGui::DestroyContext();

    ClearAll();
    ::DestroyWindow(overlay.hwnd);
    ::UnregisterClassW(overlay.wndex.lpszClassName, overlay.wndex.hInstance);

    return true;
}

enum class Style : DWORD {
    windowed = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
    aero_borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
    basic_borderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
};

auto composition_enabled() -> bool {
    BOOL composition_enabled = FALSE;
    bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
    return composition_enabled && success;
}

auto select_borderless_style() -> Style {
    return composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
}

auto set_shadow(HWND handle, bool enabled) -> void {
    if (composition_enabled()) {
        static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
        ::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
    }
}


void set_borderless(bool enabled) {
    Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
    Style old_style = static_cast<Style>(::GetWindowLongPtrW(Window::getHWND(), GWL_STYLE));

    if (new_style != old_style) {
        ::SetWindowLongPtrW(Window::getHWND(), GWL_STYLE, static_cast<LONG>(new_style));

        // when switching between borderless and windowed, restore appropriate shadow state
        set_shadow(Window::getHWND(), true && (new_style != Style::windowed));

        // redraw frame
        ::SetWindowPos(Window::getHWND(), nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
        ::ShowWindow(Window::getHWND(), SW_SHOW);
    }
}

// window prefrerences
bool borderless = true;
bool borderless_shadow = true;
bool borderless_drag = true;
bool borderless_resize = true;

void set_borderless_shadow(bool enabled) {
    if (borderless) {
        borderless_shadow = enabled;
        set_shadow(Window::getHWND(), enabled);
    }
}

bool initCreated = false;
ImVec2 g_windowPadding = {};

bool g_fileDropQueried = false;

bool g_processingFileDrop = false;
bool g_openSuccessPopup = false;
std::string g_fileDropStatus = "";


class MillenniumTarget : public IDropTarget {
public:
    MillenniumTarget(HWND hwnd) : hwnd(hwnd) {
        GetClientRect(hwnd, &clientRect);
    }

    STDMETHOD(QueryInterface)(REFIID, void**) override {
        return E_NOTIMPL;
    }
    STDMETHOD_(ULONG, AddRef)() override {
        return 1;
    }
    STDMETHOD_(ULONG, Release)() override {
        return 1;
    }
    STDMETHOD(DragEnter)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        POINT cursorPoint = { pt.x, pt.y };
        LogFilePresence(true);
        return S_OK;
    }
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        POINT cursorPoint = { pt.x, pt.y };
        LogFilePresence(true);
        return S_OK;
    }
    STDMETHOD(DragLeave)() override { 
        LogFilePresence(false); return S_OK; 
    }

    STDMETHOD(Drop)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium;

        // Try to get the dropped files in CF_HDROP format
        if (pDataObj->GetData(&fmt, &medium) == S_OK) {
            HDROP hDrop = static_cast<HDROP>(medium.hGlobal);
            UINT numFiles = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);

            for (UINT i = 0; i < numFiles; ++i) {
                TCHAR filePath[MAX_PATH];
                if (DragQueryFile(hDrop, i, filePath, MAX_PATH) > 0) {
                    // Handle the file path (e.g., log or process it)
                    Community::Themes->handleFileDrop(filePath);
                }
            }

            ReleaseStgMedium(&medium); // Release the allocated resources
        }
        else {
            // Try to get the dropped data as a URL with the DownloadURL format
            fmt = { CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            if (pDataObj->GetData(&fmt, &medium) == S_OK) {
                char* urlData = static_cast<char*>(GlobalLock(medium.hGlobal));
                if (urlData != nullptr) {
                    // Parse the DownloadURL data and handle it
                    std::string urlStr(urlData);
                    size_t colonPos = urlStr.find(':');
                    if (colonPos != std::string::npos) {
                        std::string fileName = urlStr.substr(0, colonPos);
                        std::string downloadUrl = urlStr.substr(colonPos + 1);

                        std::thread([=] {
                            Community::Themes->handleThemeInstall(fileName, downloadUrl);
                        }).detach();
                    }

                    GlobalUnlock(medium.hGlobal);
                }

                ReleaseStgMedium(&medium); // Release the allocated resources
            }
        }

        LogFilePresence(false);
        return S_OK;
    }

    void SetClientRect(RECT rect) {
        clientRect = rect;
    }

private:
    HWND hwnd; // The window handle
    RECT clientRect; // The client area of the window

    // Function to log file presence in the client area
    void LogFilePresence(bool presence) {
        g_fileDropQueried = presence;
    }
};

bool Application::Create(std::function<void()> Handler, std::function<void()> callBack)
{
    overlay.name        = { appinfo.Title.c_str() };

    WNDCLASSEXW wcx{};
    wcx.cbSize = sizeof(wcx);
    wcx.hInstance = nullptr;
    wcx.lpfnWndProc = WndProc;
    wcx.lpszClassName = L"Millennium";
    wcx.hIcon = { /*GetParentProcessIcon()*/ };
    const ATOM result = ::RegisterClassExW(&wcx);

    overlay.wndex = wcx;

    overlay.hwnd = CreateWindowExW(
        0, overlay.wndex.lpszClassName, (LPCWSTR)"Millennium.",
        static_cast<DWORD>(Style::windowed), 0, 0,
        305, 145, nullptr, nullptr, nullptr, nullptr
    );

    ::wnd_center();
    set_borderless(false);
    set_borderless_shadow(true);

    if (!create_device_d3d(overlay.hwnd)) 
    { 
        ClearAll(); 
    }

    ::ShowWindow(overlay.hwnd, SW_SHOW);::UpdateWindow(overlay.hwnd);

    SetWindowPos(overlay.hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    Window::bringToFront();

    ImGui::CreateContext();

    if (!initCreated) {
        ImGuiIO* IO = &ImGui::GetIO();
        initFontAtlas(IO);
        IO->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        initCreated = true;
    }

    directx9.params.PresentationInterval       = D3DPRESENT_INTERVAL_ONE;
	directx9.params.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

    MillenniumTarget dropTarget(overlay.hwnd);
    RegisterDragDrop(overlay.hwnd, &dropTarget);
    DragAcceptFiles(overlay.hwnd, TRUE);

    callBack(); 
    initTextures();

    if (ImGui_ImplWin32_Init(overlay.hwnd) && ImGui_ImplDX9_Init(directx9.device))
    {
        set_proc_theme_colors();
        memset((&directx9.msg), 0, (sizeof(directx9.msg)));

        while (directx9.msg.message != WM_QUIT)
        {

            if (PeekMessageA(&directx9.msg, NULL, (UINT)0U, (UINT)0U, PM_REMOVE))
            {
                TranslateMessage(&directx9.msg);
                DispatchMessageA(&directx9.msg);
                continue;
            }

            ImGui_ImplDX9_NewFrame(); 
            ImGui_ImplWin32_NewFrame();

            ImGui::NewFrame();
            {
                static ImGuiWindowFlags flags = /*ImGuiWindowFlags_NoDecoration |*/ ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
                ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, g_windowPadding);

                ImGui::Begin(appinfo.Title.c_str(), &g_windowOpen, flags);          
                {
                    g_headerHovered = ImGui::IsItemHovered();
                    Handler();
                }  
                ImGui::End();
            }
            ImGui::EndFrame();

            directx9.device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);

            if (directx9.device->BeginScene() >= 0) 
            { 
                ImGui::Render(); 
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); 
                directx9.device->EndScene(); 
            }

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) 
            {
                ImGui::UpdatePlatformWindows();
            }
            
            ImGui::RenderPlatformWindowsDefault();

            if (directx9.device->Present(NULL, NULL, NULL, NULL) == D3DERR_DEVICELOST && directx9.device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) 
            {
                reset_device();
            }

            if (!g_windowOpen) {
                PostQuitMessage(0);
            }
        }
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();

        ClearAll();::DestroyWindow(overlay.hwnd);
    }

    return false;
}


auto hit_test(POINT cursor) -> LRESULT {
    // identify borders and corners to allow resizing the window.
    // Note: On Windows 10, windows behave differently and
    // allow resizing outside the visible window frame.
    // This implementation does not replicate that behavior.
    const POINT border{
        2,
        2
    };
    RECT window;
    if (!::GetWindowRect(Window::getHWND(), &window)) {
        return HTNOWHERE;
    }

    const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

    enum region_mask {
        client = 0b0000,
        left = 0b0001,
        right = 0b0010,
        top = 0b0100,
        bottom = 0b1000,
    };

    const auto result =
        left * (cursor.x < (window.left + border.x)) |
        right * (cursor.x >= (window.right - border.x)) |
        top * (cursor.y < (window.top + border.y)) |
        bottom * (cursor.y >= (window.bottom - border.y));

    switch (result) {
        //case left: return borderless_resize ? HTLEFT : drag;
        //case right: return borderless_resize ? HTRIGHT : drag;
        //case top: return borderless_resize ? HTTOP : drag;
        //case bottom: return borderless_resize ? HTBOTTOM : drag;
        //case top | left: return borderless_resize ? HTTOPLEFT : drag;
        //case top | right: return borderless_resize ? HTTOPRIGHT : drag;
        //case bottom | left: return borderless_resize ? HTBOTTOMLEFT : drag;
        //case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : drag;
        case client: {
            // account for the close button 
            if (g_headerHovered && cursor.x <= window.right - 25) {
                return drag;
            }
            else return HTCLIENT;
        }
        default: return HTNOWHERE;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        return true;
    }

    static tagPOINT last_loc;

    switch (msg) 
    {
        case WM_SIZE:
        {
            switch (wParam)
            {
                case SIZE_MAXIMIZED: {
                    g_windowPadding = ImVec2(10, 10);
                    break;
                }
                default: {
                    g_windowPadding = ImVec2(0, 0);
                    break;
                }
            }

            if (directx9.device != NULL && wParam != SIZE_MINIMIZED)
            {
                directx9.params.BackBufferWidth = LOWORD(lParam);
                directx9.params.BackBufferHeight = HIWORD(lParam);
                reset_device();
            }
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            /*((MINMAXINFO*)lParam)->ptMinTrackSize = { 750, 500 };*/
            return 0;
        }
        case WM_SYSCOMMAND:
        {
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        }
        case WM_NCCALCSIZE: {
            return 0;
        }
        case WM_NCHITTEST: {
            if (borderless) {
                return hit_test(POINT{
                    GET_X_LPARAM(lParam),
                    GET_Y_LPARAM(lParam)
                });
            }
            break;
        }
        case WM_NCACTIVATE: {
            return 1;
        }
        case WM_CLOSE: {
            g_windowOpen = false;
            ::DestroyWindow(hWnd);
            return 0;
        }
        case WM_DESTROY: {
            g_windowOpen = false;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void set_proc_theme_colors()
{
	ImGuiStyle* sty = (ImGuiStyle*)(void*) & ImGui::GetStyle();

    sty->FrameRounding     = 4.0f;
    sty->ChildRounding     = 0.0f;
    sty->GrabRounding      = 4.0f;
    sty->WindowRounding    = 4.0f;
    //sty->ItemSpacing       = ImVec2(12, 8);
    sty->ScrollbarSize     = 10.0f;
    sty->ScrollbarRounding = 4.0f;
    sty->AntiAliasedFill = true;

	sty->SetTheme[ImGuiCol_ChildBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	sty->SetTheme[ImGuiCol_Border]               = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
      
	sty->SetTheme[ImGuiCol_Text]                 = ImVec4(1.f, 1.f, 1.f, 1.00f);
	sty->SetTheme[ImGuiCol_TextDisabled]         = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
	sty->SetTheme[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	sty->SetTheme[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	sty->SetTheme[ImGuiCol_ScrollbarGrab]        = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
	sty->SetTheme[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	sty->SetTheme[ImGuiCol_FrameBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	sty->SetTheme[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
	sty->SetTheme[ImGuiCol_FrameBgActive]        = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
	sty->SetTheme[ImGuiCol_TitleBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	sty->SetTheme[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.14f, 0.14f, 0.14f, 0.75f);
	sty->SetTheme[ImGuiCol_TitleBgActive]        = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	sty->SetTheme[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

	sty->SetTheme[ImGuiCol_Button]               = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	sty->SetTheme[ImGuiCol_ButtonHovered]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	sty->SetTheme[ImGuiCol_ButtonActive]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	sty->SetTheme[ImGuiCol_Tab]                  = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	sty->SetTheme[ImGuiCol_TabHovered]           = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	sty->SetTheme[ImGuiCol_TabActive]            = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);

	sty->SetTheme[ImGuiCol_Header]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	sty->SetTheme[ImGuiCol_HeaderHovered]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	sty->SetTheme[ImGuiCol_HeaderActive]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);

	sty->SetTheme[ImGuiCol_ResizeGrip]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	sty->SetTheme[ImGuiCol_ResizeGripHovered]    = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	sty->SetTheme[ImGuiCol_ResizeGripActive]     = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	sty->SetTheme[ImGuiCol_PlotLines]            = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	sty->SetTheme[ImGuiCol_PlotLinesHovered]     = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	sty->SetTheme[ImGuiCol_PlotHistogram]        = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	sty->SetTheme[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	sty->SetTheme[ImGuiCol_TextSelectedBg]       = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
}
