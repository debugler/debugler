WINGDIAPI BOOL WINAPI wglCopyContext (HGLRC hglrcSrc, HGLRC hglrcDst, UINT mask);
WINGDIAPI HGLRC WINAPI wglCreateContext (HDC hdc);
WINGDIAPI HGLRC WINAPI wglCreateLayerContext (HDC hdc, int iLayerPlane);
WINGDIAPI BOOL WINAPI wglDeleteContext (HGLRC hglrc);
WINGDIAPI HGLRC WINAPI wglGetCurrentContext (VOID);
WINGDIAPI HDC WINAPI wglGetCurrentDC (VOID);
WINGDIAPI PROC WINAPI wglGetProcAddress (LPCSTR lpszProc);
WINGDIAPI BOOL WINAPI wglMakeCurrent (HDC hdc, HGLRC hglrc);
WINGDIAPI BOOL WINAPI wglShareLists (HGLRC hglrc1, HGLRC hglrc2);
WINGDIAPI int  WINAPI wglChoosePixelFormat (HDC a, CONST PIXELFORMATDESCRIPTOR *b);
WINGDIAPI BOOL WINAPI wglDescribeLayerPlane (HDC a, int b, int c, UINT d, LPLAYERPLANEDESCRIPTOR e);
WINGDIAPI int  WINAPI wglDescribePixelFormat (HDC a, int b, UINT c, LPPIXELFORMATDESCRIPTOR d);
WINGDIAPI PROC WINAPI wglGetDefaultProcAddress (LPCSTR a);
WINGDIAPI int  WINAPI wglGetLayerPaletteEntries (HDC a, int b, int c, int d, COLORREF *e);
WINGDIAPI int  WINAPI wglGetPixelFormat (HDC a);
WINGDIAPI BOOL WINAPI wglRealizeLayerPalette (HDC a, int b, BOOL c);
WINGDIAPI int  WINAPI wglSetLayerPaletteEntries (HDC a, int b, int c, int d, CONST COLORREF *e);
WINGDIAPI BOOL WINAPI wglSetPixelFormat (HDC a, int b, CONST PIXELFORMATDESCRIPTOR *c);
WINGDIAPI BOOL WINAPI wglSwapBuffers (HDC a);
WINGDIAPI BOOL WINAPI wglSwapLayerBuffers (HDC a, UINT b);
WINGDIAPI long WINAPI wglSwapMultipleBuffers (UINT, const WGLSWAP *);
WINGDIAPI BOOL WINAPI wglUseFontBitmapsA (HDC a, DWORD b, DWORD c, DWORD d);
WINGDIAPI BOOL WINAPI wglUseFontBitmapsW (HDC a, DWORD b, DWORD c, DWORD d);
WINGDIAPI BOOL WINAPI wglUseFontOutlinesA (HDC a, DWORD b, DWORD c, DWORD d, FLOAT e, FLOAT f, int g, LPGLYPHMETRICSFLOAT h);
WINGDIAPI BOOL WINAPI wglUseFontOutlinesW (HDC a, DWORD b, DWORD c, DWORD d, FLOAT e, FLOAT f, int g, LPGLYPHMETRICSFLOAT h);