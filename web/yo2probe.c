/* yo2probe: does a yo2.exe-shaped program work under Boxedwine-in-browser?
   Same stack as the 2008 client: Win32 window + Direct3D 8 (d3d8.dll) + WinSock 1.1 (wsock32.dll).
   Renders a spinning triangle; the clear colour reports the YO2 gate handshake:
   blue = connecting, green = got a gate reply from the real emulator, red = failed. Log in C:\yo2probe.log */
#include <windows.h>
#include <winsock.h>
#include <wincrypt.h>
#include <d3d8.h>
#include <math.h>
#include <stdio.h>

static volatile LONG g_net = 0;           /* 0 connecting, 1 ok, 2 fail */
static char g_msg[128] = "connecting";
static FILE *g_log;

static void logf_(const char *s) { if (g_log) { fprintf(g_log, "%s\n", s); fflush(g_log); } }

static void md5(const BYTE *p, DWORD n, BYTE out[16]) {
    HCRYPTPROV prov; HCRYPTHASH h; DWORD len = 16;
    CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(prov, CALG_MD5, 0, 0, &h);
    CryptHashData(h, p, n, 0);
    CryptGetHashParam(h, HP_HASHVAL, out, &len, 0);
    CryptDestroyHash(h); CryptReleaseContext(prov, 0);
}

static const BYTE KEY[4] = {0x5a, 0xa5, 0x5a, 0x55};

/* yo2srv/red/proto.py pack(): 24-byte header (id, len, param, md5) + data, XORed with KEY */
static int pack(BYTE *out, WORD id, const BYTE *data, WORD n, DWORD param) {
    BYTE tmp[8 + 1024];
    out[0] = id >> 8; out[1] = (BYTE)id; out[2] = n >> 8; out[3] = (BYTE)n;
    out[4] = param >> 24; out[5] = param >> 16; out[6] = param >> 8; out[7] = (BYTE)param;
    memcpy(tmp, out, 8); memcpy(tmp + 8, data, n);
    md5(tmp, 8 + n, out + 8);
    memcpy(out + 24, data, n);
    for (int i = 0; i < 24 + n; i++) out[i] ^= KEY[i % 4];
    return 24 + n;
}

static DWORD WINAPI net(LPVOID host) {
    WSADATA w; SOCKET s; struct sockaddr_in a; BYTE buf[2048]; int got = 0, r;
    WSAStartup(MAKEWORD(1, 1), &w);
    s = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(5730);
    a.sin_addr.s_addr = inet_addr((const char *)host);
    DWORD t0 = GetTickCount();
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) {
        sprintf(g_msg, "connect failed %d", WSAGetLastError()); logf_(g_msg); g_net = 2; return 0;
    }
    int n = pack(buf, 0x2005, (const BYTE *)"Ver.080912.00", 13, 1);
    send(s, (const char *)buf, n, 0);
    while (got < 24 && (r = recv(s, (char *)buf + got, sizeof buf - got, 0)) > 0) got += r;
    if (got < 24) { sprintf(g_msg, "no reply (%d bytes)", got); logf_(g_msg); g_net = 2; return 0; }
    for (int i = 0; i < 24; i++) buf[i] ^= KEY[i % 4];
    WORD id = buf[0] << 8 | buf[1], len = buf[2] << 8 | buf[3];
    sprintf(g_msg, "gate reply 0x%04x len %u in %lu ms", id, len, GetTickCount() - t0);
    logf_(g_msg);
    g_net = (id == 0x2002 || id == 0x2004) ? 1 : 2;   /* 0x2002 = gate hello, then 0x2004 */
    closesocket(s);
    return 0;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

struct V { float x, y, z, rhw; DWORD c; };

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    g_log = fopen("C:\\yo2probe.log", "w");
    const char *host = (cmd && *cmd) ? cmd : "127.0.0.1";
    WNDCLASSA wc = {0}; wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "yo2probe";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    RECT rc = {0, 0, 800, 600}; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hw = CreateWindowA("yo2probe", "yo2probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                            rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, inst, NULL);

    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d) { logf_("Direct3DCreate8 failed"); return 1; }
    D3DDISPLAYMODE dm; IDirect3D8_GetAdapterDisplayMode(d3d, D3DADAPTER_DEFAULT, &dm);
    D3DPRESENT_PARAMETERS pp = {0};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferFormat = dm.Format;
    pp.BackBufferWidth = 800; pp.BackBufferHeight = 600;
    IDirect3DDevice8 *dev = NULL;
    HRESULT hr = IDirect3D8_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hw,
                                         D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    char line[160]; sprintf(line, "CreateDevice hr=0x%08lx fmt=%d", hr, dm.Format); logf_(line);
    if (FAILED(hr)) return 1;
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetVertexShader(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    CreateThread(NULL, 0, net, (LPVOID)host, 0, NULL);

    MSG msg; DWORD start = GetTickCount(), frames = 0, last = start;
    for (;;) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        float t = (GetTickCount() - start) / 1000.0f;
        D3DCOLOR bg = g_net == 1 ? D3DCOLOR_XRGB(0, 140, 60) : g_net == 2 ? D3DCOLOR_XRGB(160, 20, 40)
                                                              : D3DCOLOR_XRGB(20, 40, 140);
        IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET, bg, 1.0f, 0);
        IDirect3DDevice8_BeginScene(dev);
        struct V v[3];
        for (int i = 0; i < 3; i++) {
            float a = t + i * 2.0944f;
            v[i].x = 400 + 200 * cosf(a); v[i].y = 300 + 200 * sinf(a); v[i].z = 0.5f; v[i].rhw = 1;
            v[i].c = i == 0 ? 0xffff4040 : i == 1 ? 0xff40ff40 : 0xff4040ff;
        }
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, 1, v, sizeof(struct V));
        IDirect3DDevice8_EndScene(dev);
        IDirect3DDevice8_Present(dev, NULL, NULL, NULL, NULL);
        frames++;
        DWORD now = GetTickCount();
        if (now - last >= 1000) {
            sprintf(line, "yo2probe %.1f fps | %s", frames * 1000.0f / (now - last), g_msg);
            SetWindowTextA(hw, line); logf_(line);
            /* fps report for the host: UDP 127.0.0.1:5999 through the gateway (page fps is throttled when hidden) */
            static SOCKET us = INVALID_SOCKET;
            if (us == INVALID_SOCKET) us = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in ua = {0}; ua.sin_family = AF_INET; ua.sin_port = htons(5999);
            ua.sin_addr.s_addr = inet_addr(host);
            sendto(us, line, (int)strlen(line), 0, (struct sockaddr *)&ua, sizeof ua);
            frames = 0; last = now;
        }
    }
}
