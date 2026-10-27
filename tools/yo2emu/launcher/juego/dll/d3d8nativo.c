/*
 * d3d8nativo.c - d3d8.dll fina para yo2.exe en el navegador (Boxedwine con web/boxedwine-d3d8native.patch).
 *
 * En el navegador cada llamada D3D8 del juego pasaba por el d3d8.dll de Wine -> wined3d -> opengl32, todo emulado
 * instruccion a instruccion: un sprite (un DrawPrimitiveUP de 4 vertices) costaba miles de instrucciones de Wine.
 * Esta DLL no implementa nada de D3D8: reserva una zona de memoria ("arena") y le pide a Boxedwine que escriba en ella
 * las vtables. Cada hueco de la vtable es un trampolin de 9 bytes `int 0x99; .long 0x0D3D0000|interfaz<<8|hueco; ret N`
 * que entra en codigo nativo (source/opengl/d3d8native.cpp del parche), con la misma ABI que el opengl32 de Boxedwine:
 * los argumentos se leen de la pila del juego y el resultado vuelve en eax. La arena tambien guarda los objetos COM
 * (8 bytes: vtable + numero) y los pixeles de las texturas, para que LockRect le de al juego un puntero suyo.
 *
 * Solo dos huecos son codigo de esta DLL, porque necesitan Wine:
 *   - IDirect3D8::CreateDevice: crea el contexto GL en la ventana del juego (WGL normal) antes de llamar al nativo,
 *     que dibuja en ese contexto WebGL2 desde el mismo hilo.
 *   - IDirect3DDevice8::Present: el nativo vuelca el back buffer al framebuffer de la ventana y aqui SwapBuffers
 *     lo presenta por el camino de siempre.
 *
 * Uso (solo navegador; sin el parche de Boxedwine, int 0x99 con estos numeros detiene el emulador):
 *   copiar como d3d8.dll junto a yo2.exe, o como C:\webgl\d3d8.dll en un overlay (web/bootoverlay.py --d3d8): la web de
 *   Boxedwine enlaza ahi el d3d8.dll de system32 y ya pone WINEDLLOVERRIDES d3d8=n,b.
 *   YO2ONLINE_D3DARENA=<MB> (opcional) tamano de la arena; por defecto el mayor de 1024/768/512/384/256 que quepa.
 *
 * Sin CRT: kernel32, user32, gdi32 y opengl32. Compilar (i686):
 *   i686-w64-mingw32-gcc -O2 -s -shared -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
 *     -fno-asynchronous-unwind-tables -Wall -Wl,--entry,_DllMain@12 -Wl,--kill-at d3d8nativo.c \
 *     -lkernel32 -luser32 -lgdi32 -lopengl32 -o d3d8.dll
 */
#include <windows.h>

#define IF_D3D 0
#define IF_DEV 1
#define HUECO_CREATEDEVICE 15
#define HUECO_PRESENT 15

typedef LONG (WINAPI *fn_crear)(void*, UINT, DWORD, HWND, DWORD, DWORD*, void**);
typedef LONG (WINAPI *fn_presentar)(void*, const RECT*, const RECT*, HWND, const void*);

static BYTE* arena;
static HDC dc;

/* d3dInit del parche: escribe trampolines y vtables en la arena y devuelve un IDirect3D8* */
__attribute__((naked)) static void* WINAPI d3d_iniciar(void* base, DWORD tam, void* crear, void* presentar)
{
    __asm__ volatile("int $0x99\n\t.long 0x0D3D0700\n\tret $16");
}

/* trampolin nativo de un hueco (el mismo calculo que thunkAddr en d3d8native.cpp) */
static void* trampolin(int interfaz, int hueco) { return arena + (interfaz * 128 + hueco) * 16; }

static void escribir(const char* s)
{
    DWORD n = 0, escritos;
    while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &escritos, NULL);
}

/* contexto GL en la ventana del juego: Boxedwine lo convierte en un contexto WebGL2 del hilo que lo hace actual */
static BOOL contexto_gl(HWND ventana)
{
    PIXELFORMATDESCRIPTOR pfd = {0};
    HGLRC rc;
    int formato;
    dc = GetDC(ventana);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    formato = ChoosePixelFormat(dc, &pfd);
    if (!formato || !SetPixelFormat(dc, formato, &pfd)) { escribir("[d3d8nativo] SetPixelFormat fallo\n"); return FALSE; }
    rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) { escribir("[d3d8nativo] wglCreateContext/wglMakeCurrent fallo\n"); return FALSE; }
    return TRUE;
}

static LONG WINAPI crear_dispositivo(void* yo, UINT adaptador, DWORD tipo, HWND foco, DWORD flags, DWORD* pp, void** out)
{
    HWND ventana = pp && pp[6] ? (HWND)pp[6] : foco;   /* D3DPRESENT_PARAMETERS::hDeviceWindow (+0x18) */
    if (!dc && !contexto_gl(ventana)) return (LONG)0x8876086A;   /* D3DERR_NOTAVAILABLE */
    return ((fn_crear)trampolin(IF_D3D, HUECO_CREATEDEVICE))(yo, adaptador, tipo, foco, flags, pp, out);
}

static LONG WINAPI presentar(void* yo, const RECT* origen, const RECT* destino, HWND ventana, const void* sucio)
{
    LONG r = ((fn_presentar)trampolin(IF_DEV, HUECO_PRESENT))(yo, origen, destino, ventana, sucio);
    SwapBuffers(dc);
    return r;
}

static DWORD numero_env(const char* nombre, DWORD defecto)
{
    char v[16];
    DWORD n = GetEnvironmentVariableA(nombre, v, sizeof v), r = 0, i;
    if (!n || n >= sizeof v) return defecto;
    for (i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++) r = r * 10 + (v[i] - '0');
    return i ? r : defecto;
}

__declspec(dllexport) void* WINAPI Direct3DCreate8(UINT version)
{
    static const DWORD tamanos[] = {1024, 768, 512, 384, 256};
    DWORD tam = 0, i, pedido;
    (void)version;
    if (!arena) {
        pedido = numero_env("YO2ONLINE_D3DARENA", 0);
        for (i = 0; i < sizeof tamanos / sizeof tamanos[0] && !arena; i++) {
            tam = (pedido ? pedido : tamanos[i]) << 20;
            arena = VirtualAlloc(NULL, tam, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (pedido) break;
        }
        if (!arena) { escribir("[d3d8nativo] no hay sitio para la arena\n"); return NULL; }
    }
    return d3d_iniciar(arena, tam, (void*)crear_dispositivo, (void*)presentar);
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD motivo, LPVOID reservado)
{
    (void)dll; (void)motivo; (void)reservado;
    return TRUE;
}
