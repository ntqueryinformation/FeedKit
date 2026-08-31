#ifndef UNICODE
#define UNICODE
#endif

// d3d9probe - 32-bit D3D9 test app.
// Creates a D3D9 device and shows the FULL PATH of the d3d9.dll that was
// actually loaded in the window title, so proxy placement can be verified.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"d3d9probe";
    RegisterClassW(&wc);

    HWND wnd = CreateWindowW(L"d3d9probe", L"D3D9 probe",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 640, 480,
                             nullptr, nullptr, hi, nullptr);

    wchar_t title[1100] = L"D3D9 probe";
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    IDirect3DDevice9* dev = nullptr;
    HRESULT create_hr = E_FAIL;

    if (d3d) {
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.hDeviceWindow = wnd;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

        create_hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
        if (FAILED(create_hr))
            create_hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                          D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    }

    wchar_t dll_path[MAX_PATH] = L"<d3d9.dll not loaded>";
    HMODULE m = GetModuleHandleW(L"d3d9.dll");
    if (m) GetModuleFileNameW(m, dll_path, MAX_PATH);
    swprintf_s(title, L"D3D9 probe | d3d9.dll = %s | hr=0x%08X", dll_path, (unsigned)create_hr);
    SetWindowTextW(wnd, title);
    ShowWindow(wnd, nCmdShow);

    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (dev) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(25, 55, 110), 1.0f, 0);
            dev->Present(nullptr, nullptr, nullptr, nullptr);
        }
        Sleep(10);
    }
}
