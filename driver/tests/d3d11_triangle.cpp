// d3d11_triangle.cpp -- Harness de teste Direct3D 11 nativo para validar DXVK -> BC-250
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
    int maxFrames = 1800; // ~30s a 60 FPS
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = atoi(argv[++i]);
        }
    }

    printf("=== D3D11 TEST HARNESS (Direct3D 11 -> DXVK -> BC-250) ===\n");
    printf("  Max Frames: %d (ou pressione ESC para sair)\n", maxFrames);

    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "D3D11TriangleClass";
    RegisterClassA(&wc);

    UINT width = 1280;
    UINT height = 720;
    HWND hWnd = CreateWindowExA(
        0, wc.lpszClassName, "BC-250 Direct3D 11 (via DXVK)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        fprintf(stderr, "[ERRO] Falha ao criar janela Win32.\n");
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &scd,
        &pSwapChain,
        &pDevice,
        &featureLevel,
        &pContext);

    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] D3D11CreateDeviceAndSwapChain falhou: 0x%08X\n", (unsigned)hr);
        return 1;
    }
    printf("[OK] Dispositivo Direct3D 11 e SwapChain criados com sucesso! FeatureLevel: 0x%04X\n", (unsigned)featureLevel);

    // Consulta DXGI Adapter Name
    IDXGIDevice* pDXGIDevice = NULL;
    if (SUCCEEDED(pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice))) {
        IDXGIAdapter* pAdapter = NULL;
        if (SUCCEEDED(pDXGIDevice->GetAdapter(&pAdapter))) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                wprintf(L"  Adaptador DXGI detectado: %s (VRAM: %u MB)\n",
                    desc.Description, (unsigned)(desc.DedicatedVideoMemory / (1024 * 1024)));
            }
            pAdapter->Release();
        }
        pDXGIDevice->Release();
    }

    // Criar Render Target View
    ID3D11Texture2D* pBackBuffer = NULL;
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] GetBuffer falhou: 0x%08X\n", (unsigned)hr);
        return 1;
    }
    ID3D11RenderTargetView* pRTV = NULL;
    hr = pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "[ERRO] CreateRenderTargetView falhou: 0x%08X\n", (unsigned)hr);
        return 1;
    }

    // Compilar Vertex Shader
    ID3DBlob* pVSBlob = NULL;
    ID3DBlob* pErrorBlob = NULL;
    hr = D3DCompile(g_ShaderSource, strlen(g_ShaderSource), "VS", NULL, NULL, "VSMain", "vs_4_0", 0, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            fprintf(stderr, "[ERRO] D3DCompile VS: %s\n", (char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return 1;
    }
    ID3D11VertexShader* pVS = NULL;
    pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &pVS);

    // Compilar Pixel Shader
    ID3DBlob* pPSBlob = NULL;
    hr = D3DCompile(g_ShaderSource, strlen(g_ShaderSource), "PS", NULL, NULL, "PSMain", "ps_4_0", 0, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            fprintf(stderr, "[ERRO] D3DCompile PS: %s\n", (char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return 1;
    }
    ID3D11PixelShader* pPS = NULL;
    pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &pPS);
    pPSBlob->Release();

    // Input Layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11InputLayout* pLayout = NULL;
    pDevice->CreateInputLayout(layout, 2, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &pLayout);
    pVSBlob->Release();

    // Vertices do triangulo colorido
    Vertex vertices[] = {
        {  0.0f,  0.55f, 0.0f,  1.0f, 0.1f, 0.1f }, // Vermelho
        {  0.55f, -0.55f, 0.0f, 0.1f, 1.0f, 0.1f }, // Verde
        { -0.55f, -0.55f, 0.0f, 0.1f, 0.3f, 1.0f }  // Azul
    };
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;
    ID3D11Buffer* pVB = NULL;
    pDevice->CreateBuffer(&bd, &initData, &pVB);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    printf("[OK] Pipeline Direct3D 11 configurado. Iniciando loop de renderizacao...\n");

    int frameCount = 0;
    bool running = true;
    while (running && frameCount < maxFrames) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            printf("[INFO] Tecla ESC detectada. Finalizando antecipadamente...\n");
            break;
        }

        // Fundo azul escuro elegante
        float clearColor[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
        pContext->ClearRenderTargetView(pRTV, clearColor);

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        pContext->IASetVertexBuffers(0, 1, &pVB, &stride, &offset);
        pContext->IASetInputLayout(pLayout);
        pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pContext->VSSetShader(pVS, NULL, 0);
        pContext->PSSetShader(pPS, NULL, 0);
        pContext->RSSetViewports(1, &vp);
        pContext->OMSetRenderTargets(1, &pRTV, NULL);

        pContext->Draw(3, 0);

        // Apresentacao via Swapchain
        hr = pSwapChain->Present(1, 0);
        if (FAILED(hr)) {
            fprintf(stderr, "[ERRO] Present falhou: 0x%08X\n", (unsigned)hr);
            break;
        }

        frameCount++;
        if (frameCount % 120 == 0) {
            printf("  [D3D11 Frame %d/%d] Apresentado via SwapChain.\n", frameCount, maxFrames);
        }
    }

    printf("[SUCESSO] Loop concluido. Total de quadros renderizados: %d.\n", frameCount);

    // Limpeza de recursos
    if (pVB) pVB->Release();
    if (pLayout) pLayout->Release();
    if (pVS) pVS->Release();
    if (pPS) pPS->Release();
    if (pRTV) pRTV->Release();
    if (pSwapChain) pSwapChain->Release();
    if (pContext) pContext->Release();
    if (pDevice) pDevice->Release();
    DestroyWindow(hWnd);

    return 0;
}
