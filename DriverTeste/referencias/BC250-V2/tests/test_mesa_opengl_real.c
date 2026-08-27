#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef int   (WINAPI *PFNWGLCHOOSEPIXELFORMATPROC)(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd);
typedef BOOL  (WINAPI *PFNWGLSETPIXELFORMATPROC)(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd);
typedef int   (WINAPI *PFNWGLDESCRIBEPIXELFORMATPROC)(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTPROC)(HDC hdc);
typedef BOOL  (WINAPI *PFNWGLMAKECURRENTPROC)(HDC hdc, HGLRC hglrc);
typedef PROC  (WINAPI *PFNWGLGETPROCADDRESSPROC)(LPCSTR lpszProc);
typedef BOOL  (WINAPI *PFNWGLDELETECONTEXTPROC)(HGLRC hglrc);
typedef const char* (WINAPI *PFNGLGETSTRINGPROC)(unsigned int name);

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(hWnd, msg, wp, lp);
}

int main(void) {
    SetEnvironmentVariableA("GALLIUM_DRIVER", "zink");
    SetEnvironmentVariableA("MESA_DEBUG", "1");
    SetEnvironmentVariableA("LIBGL_ALWAYS_SOFTWARE", "0");

    printf("Loading Mesa OpenGL32...\n");
    HMODULE hGL = LoadLibraryA("mesa_opengl32.dll");
    if (!hGL) {
        printf("Failed to load mesa_opengl32.dll! Error = %lu\n", GetLastError());
        return 1;
    }
    printf("Mesa opengl32.dll loaded at %p\n", (void*)hGL);

    PFNWGLCHOOSEPIXELFORMATPROC pfnChoosePixelFormat = (PFNWGLCHOOSEPIXELFORMATPROC)GetProcAddress(hGL, "wglChoosePixelFormat");
    PFNWGLSETPIXELFORMATPROC pfnSetPixelFormat = (PFNWGLSETPIXELFORMATPROC)GetProcAddress(hGL, "wglSetPixelFormat");
    PFNWGLDESCRIBEPIXELFORMATPROC pfnDescribePixelFormat = (PFNWGLDESCRIBEPIXELFORMATPROC)GetProcAddress(hGL, "wglDescribePixelFormat");
    PFNWGLCREATECONTEXTPROC pfnCreateContext = (PFNWGLCREATECONTEXTPROC)GetProcAddress(hGL, "wglCreateContext");
    PFNWGLMAKECURRENTPROC pfnMakeCurrent = (PFNWGLMAKECURRENTPROC)GetProcAddress(hGL, "wglMakeCurrent");
    PFNWGLGETPROCADDRESSPROC pfnGetProcAddress = (PFNWGLGETPROCADDRESSPROC)GetProcAddress(hGL, "wglGetProcAddress");
    PFNWGLDELETECONTEXTPROC pfnDeleteContext = (PFNWGLDELETECONTEXTPROC)GetProcAddress(hGL, "wglDeleteContext");

    HINSTANCE hInstance = GetModuleHandleA(NULL);
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_OWNDC, WndProc, 0, 0, hInstance, NULL, NULL, NULL, NULL, "TestMesaGL", NULL };
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA("TestMesaGL", "Mesa GL Test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, NULL, NULL, hInstance, NULL);
    HDC hDC = GetDC(hWnd);

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0
    };

    int maxPf = pfnDescribePixelFormat(hDC, 1, sizeof(pfd), &pfd);
    printf("Mesa DescribePixelFormat returned maxPf = %d\n", maxPf);

    int pf = pfnChoosePixelFormat(hDC, &pfd);
    printf("Mesa ChoosePixelFormat returned pf = %d\n", pf);
    if (pf > 0) {
        pfnSetPixelFormat(hDC, pf, &pfd);
    } else {
        SetPixelFormat(hDC, 1, &pfd);
    }

    printf("Creating context via Mesa wglCreateContext...\n");
    HGLRC hRC = pfnCreateContext(hDC);
    if (!hRC) {
        printf("wglCreateContext failed! Error = %lu\n", GetLastError());
        return 1;
    }
    printf("wglCreateContext success: %p\n", (void*)hRC);

    if (pfnMakeCurrent(hDC, hRC)) {
        printf("wglMakeCurrent success!\n");
        PFNGLGETSTRINGPROC glGetString = (PFNGLGETSTRINGPROC)GetProcAddress(hGL, "glGetString");
        if (glGetString) {
            printf("GL_VENDOR:   %s\n", glGetString(0x1F00));
            printf("GL_RENDERER: %s\n", glGetString(0x1F01));
            printf("GL_VERSION:  %s\n", glGetString(0x1F02));
        }
        pfnMakeCurrent(NULL, NULL);
    } else {
        printf("wglMakeCurrent failed! Error = %lu\n", GetLastError());
    }

    pfnDeleteContext(hRC);
    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    FreeLibrary(hGL);
    return 0;
}
