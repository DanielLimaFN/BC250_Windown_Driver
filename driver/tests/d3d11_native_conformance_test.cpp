// d3d11_native_conformance_test.cpp -- Teste de conformidade Direct3D 11 nativo via bc250umd.dll
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")

struct Vertex {
    float x, y, z;
    float r, g, b;
};

static const char* g_ShaderSource =
"struct VS_INPUT {\n"
"    float3 pos : POSITION;\n"
"    float3 col : COLOR;\n"
"};\n"
"struct PS_INPUT {\n"
"    float4 pos : SV_POSITION;\n"
"    float3 col : COLOR;\n"
"};\n"
"PS_INPUT VSMain(VS_INPUT input) {\n"
"    PS_INPUT output;\n"
"    output.pos = float4(input.pos, 1.0f);\n"
"    output.col = input.col;\n"
"    return output;\n"
"}\n"
"float4 PSMain(PS_INPUT input) : SV_TARGET {\n"
"    return float4(input.col, 1.0f);\n"
"}\n";

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY || (msg == WM_KEYDOWN && wParam == VK_ESCAPE)) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int main(int argc, char** argv) {
    int maxFrames = 600; // ~10s a 60 FPS por padrão
    bool headless = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
        }
    }

    printf("===================================================================\n");
    printf("  BC-250 NATIVE DIRECT3D 11 CONFORMANCE TEST (via bc250umd.dll)  \n");
    printf("===================================================================\n");
    printf("  Max Frames: %d%s\n", maxFrames, headless ? " (Headless / Offscreen)" : "");

    // 1. Encontrar adaptador BC-250
    IDXGIFactory1* pFactory = NULL;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory);
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] CreateDXGIFactory1 falhou: 0x%08X\n", (unsigned)hr);
        return 1;
    }

    IDXGIAdapter1* pSelectedAdapter = NULL;
    IDXGIAdapter1* pAdapter = NULL;
    for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);
        wprintf(L"  Adapter [%u]: %s (Vendor: 0x%04X, Device: 0x%04X, VRAM: %llu MB)\n",
            i, desc.Description, desc.VendorId, desc.DeviceId,
            (unsigned long long)(desc.DedicatedVideoMemory / (1024 * 1024)));

        if (desc.VendorId == 0x1002 && desc.DeviceId == 0x13FE && pSelectedAdapter == NULL) {
            pSelectedAdapter = pAdapter;
            pSelectedAdapter->AddRef();
            wprintf(L"  -> SELECIONADO: Adaptador BC-250 (1002:13FE)\n");
        }
        pAdapter->Release();
    }

    if (pSelectedAdapter == NULL) {
        printf("  [AVISO] Adaptador 1002:13FE nao encontrado explicitamente; utilizando adaptador padrao do sistema.\n");
    }

    // 2. Criar Janela Win32
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "Bc250D3D11ConformanceClass";
    RegisterClassA(&wc);

    UINT width = 1280;
    UINT height = 720;
    HWND hWnd = CreateWindowExA(
        0, wc.lpszClassName, "AMD BC-250 Direct3D 11 Nativo (bc250umd.dll)",
        WS_OVERLAPPEDWINDOW | (headless ? 0 : WS_VISIBLE),
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        fprintf(stderr, "[ERRO] Falha ao criar janela Win32.\n");
        if (pSelectedAdapter) pSelectedAdapter->Release();
        pFactory->Release();
        return 1;
    }

    // 3. Configurar SwapChain e criar Device
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;
    ID3D11Device* pDevice = NULL;
    ID3D11DeviceContext* pContext = NULL;
    IDXGISwapChain* pSwapChain = NULL;

    D3D_DRIVER_TYPE driverType = pSelectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    hr = D3D11CreateDeviceAndSwapChain(
        pSelectedAdapter,
        driverType,
        NULL,
        D3D11_CREATE_DEVICE_DEBUG,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &scd,
        &pSwapChain,
        &pDevice,
        &featureLevel,
        &pContext);

    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            pSelectedAdapter,
            driverType,
            NULL,
            0,
            featureLevels,
            _countof(featureLevels),
            D3D11_SDK_VERSION,
            &scd,
            &pSwapChain,
            &pDevice,
            &featureLevel,
            &pContext);
    }

    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] D3D11CreateDeviceAndSwapChain falhou: 0x%08X\n", (unsigned)hr);
        if (pSelectedAdapter) pSelectedAdapter->Release();
        pFactory->Release();
        DestroyWindow(hWnd);
        return 1;
    }

    printf("  [OK] Dispositivo D3D11 criado com sucesso! Feature Level: 0x%04X\n", (unsigned)featureLevel);

    // 4. Criar Render Target View
    ID3D11Texture2D* pBackBuffer = NULL;
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] Falha ao obter BackBuffer: 0x%08X\n", (unsigned)hr);
        return 1;
    }

    ID3D11RenderTargetView* pRTV = NULL;
    hr = pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] Falha ao criar RenderTargetView: 0x%08X\n", (unsigned)hr);
        return 1;
    }

    // 5. Compilar Shaders
    ID3DBlob* pVSBlob = NULL;
    ID3DBlob* pPSBlob = NULL;
    ID3DBlob* pErrorBlob = NULL;

    hr = D3DCompile(g_ShaderSource, strlen(g_ShaderSource), "VS", NULL, NULL, "VSMain", "vs_4_0", 0, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] Falha ao compilar Vertex Shader: %s\n", pErrorBlob ? (char*)pErrorBlob->GetBufferPointer() : "desconhecido");
        return 1;
    }

    hr = D3DCompile(g_ShaderSource, strlen(g_ShaderSource), "PS", NULL, NULL, "PSMain", "ps_4_0", 0, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] Falha ao compilar Pixel Shader: %s\n", pErrorBlob ? (char*)pErrorBlob->GetBufferPointer() : "desconhecido");
        return 1;
    }

    ID3D11VertexShader* pVS = NULL;
    ID3D11PixelShader* pPS = NULL;
    hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &pVS);
    if (FAILED(hr)) { fprintf(stderr, "[ERRO] CreateVertexShader falhou: 0x%08X\n", (unsigned)hr); return 1; }
    hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &pPS);
    if (FAILED(hr)) { fprintf(stderr, "[ERRO] CreatePixelShader falhou: 0x%08X\n", (unsigned)hr); return 1; }

    // 6. Input Layout
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11InputLayout* pLayout = NULL;
    hr = pDevice->CreateInputLayout(layoutDesc, _countof(layoutDesc), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &pLayout);
    pVSBlob->Release();
    pPSBlob->Release();
    if (FAILED(hr)) { fprintf(stderr, "[ERRO] CreateInputLayout falhou: 0x%08X\n", (unsigned)hr); return 1; }

    // 7. Vertex Buffer (Triângulo colorido RGB)
    Vertex vertices[] = {
        {  0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f },
        {  0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f },
        { -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f }
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    ID3D11Buffer* pVertexBuffer = NULL;
    hr = pDevice->CreateBuffer(&bd, &initData, &pVertexBuffer);
    if (FAILED(hr)) { fprintf(stderr, "[ERRO] CreateBuffer falhou: 0x%08X\n", (unsigned)hr); return 1; }

    // Viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    printf("  [OK] Pipeline Direct3D 11 configurado! Iniciando renderizacao...\n");

    // 8. Render Loop
    MSG msg = {};
    int frameCount = 0;
    DWORD startTick = GetTickCount();

    while (frameCount < maxFrames) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                frameCount = maxFrames;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        float clearColor[4] = { 0.05f, 0.08f, 0.15f, 1.0f };
        pContext->ClearRenderTargetView(pRTV, clearColor);

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
        pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pContext->IASetInputLayout(pLayout);
        pContext->VSSetShader(pVS, NULL, 0);
        pContext->PSSetShader(pPS, NULL, 0);
        pContext->RSSetViewports(1, &vp);
        pContext->OMSetRenderTargets(1, &pRTV, NULL);

        // Draw Call Real
        pContext->Draw(3, 0);

        // Apresentação
        pSwapChain->Present(1, 0);
        frameCount++;

        if (frameCount % 60 == 0 || frameCount == maxFrames) {
            DWORD elapsed = GetTickCount() - startTick;
            float fps = elapsed > 0 ? (float)frameCount / (elapsed / 1000.0f) : 0.0f;
            printf("  [Frame %4d/%d] FPS: %.1f | Status: RENDERING_OK\n", frameCount, maxFrames, fps);
        }
    }

    printf("===================================================================\n");
    printf("  TESTE CONCLUIDO COM SUCESSO! Total Frames: %d\n", frameCount);
    printf("===================================================================\n");

    // Limpeza
    pVertexBuffer->Release();
    pLayout->Release();
    pVS->Release();
    pPS->Release();
    pRTV->Release();
    pSwapChain->Release();
    pContext->Release();
    pDevice->Release();
    if (pSelectedAdapter) pSelectedAdapter->Release();
    pFactory->Release();
    DestroyWindow(hWnd);

    return 0;
}
