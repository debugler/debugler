#include "DGLWrapper.h"
#include <Windows.h>
#include <WinGDI.h>
#undef WINGDIAPI
#define WINGDIAPI DGLWRAPPER_API
#include <GL/gl.h>

#define APIENTRYP APIENTRY *

typedef BOOL (WINAPI *PFNWGLCOPYCONTEXT)(HGLRC, HGLRC, UINT);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXT)(HDC);
typedef HGLRC (WINAPI *PFNWGLCREATELAYERCONTEXT)(HDC, int);
typedef BOOL (WINAPI *PFNWGLDELETECONTEXT)(HGLRC);
typedef HGLRC (WINAPI *PFNWGLGETCURRENTCONTEXT)(VOID);
typedef HDC (WINAPI *PFNWGLGETCURRENTDC)(VOID);
typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)(LPCSTR);
typedef BOOL (WINAPI *PFNWGLMAKECURRENT)(HDC, HGLRC);
typedef BOOL (WINAPI *PFNWGLSHARELISTS)(HGLRC, HGLRC);
typedef int  (WINAPI *PFNWGLCHOOSEPIXELFORMAT) (HDC a, CONST PIXELFORMATDESCRIPTOR *b);
typedef BOOL (WINAPI *PFNWGLDESCRIBELAYERPLANE) (HDC a, int b, int c, UINT d, LPLAYERPLANEDESCRIPTOR e);
typedef int  (WINAPI *PFNWGLDESCRIBEPIXELFORMAT) (HDC a, int b, UINT c, LPPIXELFORMATDESCRIPTOR d);
typedef PROC (WINAPI *PFNWGLGETDEFAULTPROCADDRESS) (LPCSTR a);
typedef int  (WINAPI *PFNWGLGETLAYERPALETTEENTRIES) (HDC a, int b, int c, int d, COLORREF *e);
typedef int  (WINAPI *PFNWGLGETPIXELFORMAT) (HDC a);
typedef BOOL (WINAPI *PFNWGLREALIZELAYERPALETTE) (HDC a, int b, BOOL c);
typedef int  (WINAPI *PFNWGLSETLAYERPALETTEENTRIES) (HDC a, int b, int c, int d, CONST COLORREF *e);
typedef BOOL (WINAPI *PFNWGLSETPIXELFORMAT) (HDC a, int b, CONST PIXELFORMATDESCRIPTOR *c);
typedef BOOL (WINAPI *PFNWGLSWAPBUFFERS) (HDC a);
typedef BOOL (WINAPI *PFNWGLSWAPLAYERBUFFERS) (HDC a, UINT b);
typedef long (WINAPI *PFNWGLSWAPMULTIPLEBUFFERS) (UINT, const WGLSWAP *);
typedef BOOL (WINAPI *PFNWGLUSEFONTBITMAPSA) (HDC a, DWORD b, DWORD c, DWORD d);
typedef BOOL (WINAPI *PFNWGLUSEFONTBITMAPSW) (HDC a, DWORD b, DWORD c, DWORD d);
typedef BOOL (WINAPI *PFNWGLUSEFONTOUTLINESA) (HDC a, DWORD b, DWORD c, DWORD d, FLOAT e, FLOAT f, int g, LPGLYPHMETRICSFLOAT h);
typedef BOOL (WINAPI *PFNWGLUSEFONTOUTLINESW) (HDC a, DWORD b, DWORD c, DWORD d, FLOAT e, FLOAT f, int g, LPGLYPHMETRICSFLOAT h);

#include "../../dump/codegen/nonExtTypedefs.inl"

#define POINTER(X) X##_Ptr
#define DIRECT_CALL(X) (*POINTER(X))
#define PTR_PREFIX extern
#include "../../dump/codegen/pointers.inl"
#undef PTR_PREFIX