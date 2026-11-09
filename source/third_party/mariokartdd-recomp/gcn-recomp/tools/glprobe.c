/* Reports what OpenGL this machine can actually give us: the legacy context the
   driver hands out by default, then each core profile the renderer might target.
   Build: clang tools/glprobe.c -o build/glprobe.exe -lopengl32 -lgdi32 -luser32 */
#include <windows.h>
#include <stdio.h>

typedef HGLRC (WINAPI *PFNCREATECTXATTRIBS)(HDC, HGLRC, const int*);
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

static HWND make_window(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "glprobe";
    RegisterClassA(&wc);
    return CreateWindowA("glprobe", "glprobe", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                         NULL, NULL, wc.hInstance, NULL);
}

static int set_pixel_format(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf) { printf("ChoosePixelFormat failed (%lu)\n", GetLastError()); return 0; }
    if (!SetPixelFormat(dc, pf, &pfd)) { printf("SetPixelFormat failed (%lu)\n", GetLastError()); return 0; }
    return 1;
}

int main(void) {
    HWND w = make_window();
    HDC dc = GetDC(w);
    if (!set_pixel_format(dc)) return 1;

    HGLRC legacy = wglCreateContext(dc);
    if (!legacy) { printf("wglCreateContext failed (%lu)\n", GetLastError()); return 1; }
    wglMakeCurrent(dc, legacy);

    const char* (WINAPI *getString)(unsigned) =
        (const char* (WINAPI *)(unsigned))GetProcAddress(GetModuleHandleA("opengl32.dll"), "glGetString");
    printf("legacy context:\n");
    printf("  GL_VERSION  = %s\n", getString(0x1F02));
    printf("  GL_RENDERER = %s\n", getString(0x1F01));
    printf("  GL_VENDOR   = %s\n", getString(0x1F00));

    PFNCREATECTXATTRIBS createAttribs =
        (PFNCREATECTXATTRIBS)wglGetProcAddress("wglCreateContextAttribsARB");
    printf("wglCreateContextAttribsARB: %s\n", createAttribs ? "present" : "MISSING");

    if (createAttribs) {
        int versions[][2] = {{4,6},{4,1},{3,3},{3,2}};
        for (int i = 0; i < 4; i++) {
            int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, versions[i][0],
                WGL_CONTEXT_MINOR_VERSION_ARB, versions[i][1],
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            HGLRC c = createAttribs(dc, NULL, attribs);
            printf("  %d.%d core: %s\n", versions[i][0], versions[i][1], c ? "OK" : "failed");
            if (c) {
                wglMakeCurrent(dc, c);
                printf("      reports GL_VERSION = %s / %s\n", getString(0x1F02), getString(0x1F01));
                wglMakeCurrent(dc, legacy);
                wglDeleteContext(c);
            }
        }
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);
    return 0;
}
