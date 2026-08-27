#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>

#pragma comment(lib, "dxgi.lib")

int main() {
    printf("=== BC-250 Windows Video Memory Diagnostic ===\n");
    IDXGIFactory1* pFactory = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&pFactory);
    if (FAILED(hr)) {
        printf("Failed to create DXGI factory: 0x%08X\n", hr);
        return 1;
    }

    IDXGIAdapter1* pAdapter = NULL;
    for (UINT i = 0; pFactory->lpVtbl->EnumAdapters1(pFactory, i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->lpVtbl->GetDesc1(pAdapter, &desc);
        wprintf(L"\nAdapter %u: %s\n", i, desc.Description);
        wprintf(L"  VendorId: 0x%04X, DeviceId: 0x%04X\n", desc.VendorId, desc.DeviceId);
        wprintf(L"  DedicatedVideoMemory: %llu bytes (%.2f MB / %.2f GB)\n",
                desc.DedicatedVideoMemory,
                (double)desc.DedicatedVideoMemory / (1024.0 * 1024.0),
                (double)desc.DedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0));
        wprintf(L"  DedicatedSystemMemory: %llu bytes (%.2f MB)\n",
                desc.DedicatedSystemMemory,
                (double)desc.DedicatedSystemMemory / (1024.0 * 1024.0));
        wprintf(L"  SharedSystemMemory: %llu bytes (%.2f MB / %.2f GB)\n",
                desc.SharedSystemMemory,
                (double)desc.SharedSystemMemory / (1024.0 * 1024.0),
                (double)desc.SharedSystemMemory / (1024.0 * 1024.0 * 1024.0));
        wprintf(L"  Flags: 0x%08X (DXGI_ADAPTER_FLAG_SOFTWARE=%d)\n",
                desc.Flags, (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 1 : 0);
        pAdapter->lpVtbl->Release(pAdapter);
    }
    pFactory->lpVtbl->Release(pFactory);
    return 0;
}
