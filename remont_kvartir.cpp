// =====================================================================
//  РЕМОНТ КВАРТИР — купи убитую квартиру, отремонтируй и продай дороже (3D, от первого лица)
//  C++ + SDL для ccdroid / C4droid. Один файл, внешних библиотек нет.
//  Если не собирается: ниже поставь SDL_MODE 1 (SDL 1.2) или 2 (SDL2).
// =====================================================================
#ifndef SDL_MODE
#define SDL_MODE 0            // 0 = авто, 1 = SDL 1.2, 2 = SDL2
#endif
#if SDL_MODE == 0
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL.h>)
      #undef SDL_MODE
      #define SDL_MODE 2
    #endif
  #endif
#endif
#if SDL_MODE == 0
  #undef SDL_MODE
  #define SDL_MODE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  #pragma GCC optimize("O2")      // если среда собирает без оптимизации, включаем её для этого файла
#endif
#include <vector>
#include <cstddef>
#include <map>
#if defined(__unix__) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
  #include <unistd.h>
  #define HAVE_GETCWD 1
#endif
#if SDL_MODE == 2
  #include <SDL2/SDL.h>
#else
  #include <SDL/SDL.h>
#endif

// ---------- Утилиты ----------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminv(float a, float b) { return a < b ? a : b; }
static inline float fmaxv(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-8f) l = 1; return a * (1.0f / l); }

typedef uint32_t u32;
// ---- видеокарта (OpenGL ES 2): 3D рисует GPU, а 2D-интерфейс кладётся поверх прозрачной картинкой ----
static bool gGL = false;          // рисуем ли 3D видеокартой
static bool gWantGL = true;       // пробовать ли видеокарту при запуске (можно выключить в меню)
static bool gOverlay = false;     // fb - прозрачный слой интерфейса поверх 3D (альфа в старшем байте)
struct GV { float x, y, z; unsigned char r, g, b, a; };
static std::vector<GV> gVerts;    // треугольники 3D за кадр (в координатах камеры)
static unsigned gSkyCol = 0;
static float gNearZ = 0.3f;   // ближе этой дистанции полигоны обрезаются (в квартире - 0.06, чтобы стены не просвечивали)
static inline u32 RGB(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
static u32 mixc(u32 a, u32 b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return RGB((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t),
             (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static u32 shadec(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

// ---------- Экран и буферы ----------
static int SW = 800, SH = 480, IW = 400, IH = 240, fs = 2;
static int LW = 800, LH = 480;      // логический (горизонтальный) размер экрана
static bool rotMode = false;        // экран вертикальный -> картинку поворачиваем
static int rotDir = 1;              // 1 = по часовой, -1 = против
static int quality = 1;              // 0 низкая, 1 средняя, 2 высокая (разрешение картинки)
static bool aaOn = true;             // сглаживание краёв (можно выключить в меню, если телефон тормозит)
static int wheelN = 10;              // число граней у колёс и фар (растёт с уровнем графики)
static float gProf3D = 0, gProfPr = 0;   // мс за кадр: 3D-превью и вывод на экран (для счётчика FPS)
static bool soundOn = true;           // звук мотора
static bool fpsOn = true;            // счётчик FPS в углу экрана
static bool gDry = false;            // пробный проход: считаем, где фары относительно поверхности, ничего не рисуя
static bool autoPerf = true;         // автоматически упрощать картинку, если телефон не успевает
static int gPerf = 0;                // 0 - полная картинка, 1 - без сглаживания и суперсэмплинга, 2 - ещё и проще колёса, 3 - ещё и картинка на ступень мельче (только пока не хватает скорости)
static int gResDrop = 0;              // на сколько ступеней качества временно уменьшено разрешение (авто-режим, уровень 3)
static bool gApplyQ = false;
static int insL = 0, insR = 0;      // поля экрана: сколько физ. пикселей не видно с начала/конца оси X картинки
static u32 *fb = NULL;
static float *zb = NULL;
static int clipX0, clipY0, clipX1, clipY1;
static float focal = 300, vcx = 0, vcy = 0;
static u32 fogCol = 0;
static float fogA = 1e9f, fogB = 2e9f;
static float gTime = 0;

// ---------- Ввод ----------
struct Ev { int type, x, y; };   // 0 выход, 1 нажатие, 2 отпускание, 3 движение, 4 назад
struct Ptr {
  bool down, pressed, clicked;
  int x, y, sx, sy, cx, cy, dx, dy;
};
static Ptr P;

// ---------- Бэкенд SDL ----------
#if SDL_MODE == 2
static SDL_Window *gWin = NULL;
static SDL_Renderer *gRen = NULL;
static SDL_Texture *gTex = NULL;
static SDL_GLContext gCtx = NULL;

// ======================= OpenGL ES 2 =======================
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar; typedef unsigned int GLbitfield; typedef ptrdiff_t GLsizeiptr;
#define GLF(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p_##name = NULL;
GLF(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) GLF(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) GLF(void, glClear, (GLbitfield))
GLF(void, glEnable, (GLenum)) GLF(void, glDisable, (GLenum)) GLF(void, glDepthFunc, (GLenum)) GLF(void, glDepthMask, (GLboolean)) GLF(void, glBlendFunc, (GLenum, GLenum))
GLF(GLuint, glCreateShader, (GLenum)) GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) GLF(void, glCompileShader, (GLuint))
GLF(void, glGetShaderiv, (GLuint, GLenum, GLint *)) GLF(GLuint, glCreateProgram, (void)) GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) GLF(void, glLinkProgram, (GLuint)) GLF(void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLF(void, glUseProgram, (GLuint)) GLF(GLint, glGetUniformLocation, (GLuint, const GLchar *)) GLF(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void, glUniform2f, (GLint, GLfloat, GLfloat)) GLF(void, glUniform1i, (GLint, GLint)) GLF(void, glBindBuffer, (GLenum, GLuint))
GLF(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) GLF(void, glEnableVertexAttribArray, (GLuint)) GLF(void, glDisableVertexAttribArray, (GLuint))
GLF(void, glDrawArrays, (GLenum, GLint, GLsizei)) GLF(void, glGenTextures, (GLsizei, GLuint *)) GLF(void, glBindTexture, (GLenum, GLuint)) GLF(void, glDeleteTextures, (GLsizei, const GLuint *))
GLF(void, glTexParameteri, (GLenum, GLenum, GLint)) GLF(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
GLF(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) GLF(void, glActiveTexture, (GLenum)) GLF(void, glPixelStorei, (GLenum, GLint))
GLF(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
#undef GLF
static GLuint glProg3D = 0, glProg2D = 0, glTexUI = 0; static GLint uP = -1, uNF = -1, uTex = -1; static int glTexW = 0, glTexH = 0, glVW = 0, glVH = 0;
static GLuint glMakeProg(const char *vs, const char *fs2) {
  GLuint v = p_glCreateShader(0x8B31), f = p_glCreateShader(0x8B30); GLint ok = 0;
  p_glShaderSource(v, 1, &vs, NULL); p_glCompileShader(v); p_glGetShaderiv(v, 0x8B81, &ok); if (!ok) return 0;
  p_glShaderSource(f, 1, &fs2, NULL); p_glCompileShader(f); p_glGetShaderiv(f, 0x8B81, &ok); if (!ok) return 0;
  GLuint p = p_glCreateProgram(); p_glAttachShader(p, v); p_glAttachShader(p, f);
  p_glBindAttribLocation(p, 0, "aP"); p_glBindAttribLocation(p, 1, "aC"); p_glLinkProgram(p); p_glGetProgramiv(p, 0x8B82, &ok);
  return ok ? p : 0;
}
// loader - функция, которая по имени даёт адрес функции GL (SDL_GL_GetProcAddress или eglGetProcAddress)
static bool glSetup(void *(*loader)(const char *)) {
  #define LD(n) p_##n = (PFN_##n)loader(#n); if (!p_##n) return false;
  LD(glViewport) LD(glClearColor) LD(glClear) LD(glEnable) LD(glDisable) LD(glDepthFunc) LD(glDepthMask) LD(glBlendFunc) LD(glCreateShader) LD(glShaderSource)
  LD(glCompileShader) LD(glGetShaderiv) LD(glCreateProgram) LD(glAttachShader) LD(glBindAttribLocation) LD(glLinkProgram) LD(glGetProgramiv) LD(glUseProgram)
  LD(glGetUniformLocation) LD(glUniform4f) LD(glUniform2f) LD(glUniform1i) LD(glBindBuffer) LD(glVertexAttribPointer) LD(glEnableVertexAttribArray)
  LD(glDisableVertexAttribArray) LD(glDrawArrays) LD(glGenTextures) LD(glBindTexture) LD(glDeleteTextures) LD(glTexParameteri) LD(glTexImage2D) LD(glTexSubImage2D)
  LD(glActiveTexture) LD(glPixelStorei)
  #undef LD
  p_glReadPixels = (PFN_glReadPixels)loader("glReadPixels");
  // 3D: точки уже в координатах камеры; проекция как у программного движка (focal, vcx, vcy), глубина - видеокартой
  glProg3D = glMakeProg(
    "attribute vec3 aP; attribute vec4 aC; uniform vec4 uP; uniform vec2 uNF; varying vec4 vC;\n"
    "void main(){ vC = aC; gl_Position = vec4(aP.x * uP.x + uP.y * aP.z, aP.y * uP.z + uP.w * aP.z, aP.z * uNF.x + uNF.y, aP.z); }\n",
    "precision mediump float; varying vec4 vC; void main(){ gl_FragColor = vC; }\n");
  // 2D: картинка интерфейса поверх (в памяти байты B,G,R,A - переставляем)
  glProg2D = glMakeProg(
    "attribute vec2 aP; varying vec2 vT; void main(){ vT = vec2(aP.x * 0.5 + 0.5, 0.5 - aP.y * 0.5); gl_Position = vec4(aP, 0.0, 1.0); }\n",
    "precision mediump float; varying vec2 vT; uniform sampler2D uTex; void main(){ vec4 t = texture2D(uTex, vT); gl_FragColor = vec4(t.b, t.g, t.r, t.a); }\n");
  if (!glProg3D || !glProg2D) return false;
  uP = p_glGetUniformLocation(glProg3D, "uP"); uNF = p_glGetUniformLocation(glProg3D, "uNF"); uTex = p_glGetUniformLocation(glProg2D, "uTex");
  p_glGenTextures(1, &glTexUI);
  return true;
}
static void glUITex(int w, int h) {
  if (glTexW == w && glTexH == h) return;
  p_glBindTexture(0x0DE1, glTexUI);
  p_glTexParameteri(0x0DE1, 0x2801, 0x2600); p_glTexParameteri(0x0DE1, 0x2800, 0x2600);      // NEAREST: пиксельный шрифт чёткий
  p_glTexParameteri(0x0DE1, 0x2802, 0x812F); p_glTexParameteri(0x0DE1, 0x2803, 0x812F);
  p_glTexImage2D(0x0DE1, 0, 0x1908, w, h, 0, 0x1908, 0x1401, NULL); glTexW = w; glTexH = h;
}
// кадр: очистка, все 3D-треугольники одним вызовом, потом интерфейс поверх
static void glFrame(int vw, int vh) {
  p_glViewport(0, 0, vw, vh);
  p_glClearColor(((gSkyCol >> 16) & 255) / 255.0f, ((gSkyCol >> 8) & 255) / 255.0f, (gSkyCol & 255) / 255.0f, 1.0f);
  p_glClear(0x4000 | 0x100);
  if (!gVerts.empty()) {
    p_glEnable(0x0B71); p_glDepthFunc(0x0203); p_glDepthMask(1); p_glDisable(0x0BE2); p_glDisable(0x0B44);
    p_glUseProgram(glProg3D); p_glBindBuffer(0x8892, 0);
    float n = fmaxv(0.02f, gNearZ), f = 120.0f;
    p_glUniform4f(uP, 2.0f * focal / IW, 2.0f * vcx / IW - 1.0f, 2.0f * focal / IH, 1.0f - 2.0f * vcy / IH);
    p_glUniform2f(uNF, (f + n) / (f - n), -2.0f * f * n / (f - n));
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(0, 3, 0x1406, 0, sizeof(GV), &gVerts[0].x);
    p_glVertexAttribPointer(1, 4, 0x1401, 1, sizeof(GV), &gVerts[0].r);
    p_glDrawArrays(4, 0, (GLsizei)gVerts.size());
    p_glDisableVertexAttribArray(1);
  }
  p_glDisable(0x0B71); p_glEnable(0x0BE2); p_glBlendFunc(0x0302, 0x0303);
  p_glUseProgram(glProg2D); p_glActiveTexture(0x84C0); glUITex(IW, IH); p_glBindTexture(0x0DE1, glTexUI);
  p_glPixelStorei(0x0CF5, 4); p_glTexSubImage2D(0x0DE1, 0, 0, 0, IW, IH, 0x1908, 0x1401, fb); p_glUniform1i(uTex, 0);
  static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
  p_glEnableVertexAttribArray(0); p_glVertexAttribPointer(0, 2, 0x1406, 0, 0, quad); p_glDrawArrays(5, 0, 4);
  gVerts.clear();
  memset(fb, 0, sizeof(u32) * IW * IH);          // интерфейс следующего кадра - на прозрачном фоне
}

static void *sdlGLProc(const char *n) { return SDL_GL_GetProcAddress(n); }
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  if (gWantGL) {                                   // пробуем видеокарту; не вышло - обычный режим
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    gWin = SDL_CreateWindow("Remont", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (gWin) { gCtx = SDL_GL_CreateContext(gWin);
      if (!gCtx) { SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16); gCtx = SDL_GL_CreateContext(gWin); }
      if (gCtx && glSetup(sdlGLProc)) { SDL_GL_SetSwapInterval(1); SDL_GetWindowSize(gWin, &SW, &SH); gGL = true; gOverlay = true; return true; }
      if (gCtx) SDL_GL_DeleteContext(gCtx); gCtx = NULL; SDL_DestroyWindow(gWin); gWin = NULL; }
  }
  gWin = SDL_CreateWindow("Remont", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gGL) return;
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  if (gGL) { int dw = SW, dh = SH; SDL_GL_GetDrawableSize(gWin, &dw, &dh); glFrame(dw, dh); SDL_GL_SwapWindow(gWin); return; }
  SDL_UpdateTexture(gTex, NULL, fb, IW * 4);
  SDL_RenderClear(gRen);
  if (rotMode) {
    SDL_Rect d;
    int avail = SH - insL - insR;
    int cy = rotDir > 0 ? insL + avail / 2 : SH - insL - avail / 2;
    d.w = avail; d.h = SW; d.x = SW / 2 - d.w / 2; d.y = cy - d.h / 2;
    SDL_RenderCopyEx(gRen, gTex, NULL, &d, rotDir > 0 ? 90.0 : 270.0, NULL, SDL_FLIP_NONE);
  } else {
    SDL_Rect d;
    d.x = insL; d.y = 0; d.w = SW - insL - insR; d.h = SH;
    SDL_RenderCopy(gRen, gTex, NULL, &d);
  }
  SDL_RenderPresent(gRen);
}
static void gfxQuit() { SDL_Quit(); }
#else
static SDL_Surface *gScr = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  gScr = SDL_SetVideoMode(0, 0, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
  if (!gScr) gScr = SDL_SetVideoMode(800, 480, 32, SDL_SWSURFACE);
  if (!gScr) return false;
  SW = gScr->w;
  SH = gScr->h;
  return true;
}
static void gfxTex() {}
static u32 *convBuf = NULL, *rotBuf = NULL;
static int *axMap = NULL;
static void gfxResize() { free(convBuf); convBuf = NULL; free(rotBuf); rotBuf = NULL; }
static void gfxPresent() {
  if (SDL_MUSTLOCK(gScr)) SDL_LockSurface(gScr);
  SDL_PixelFormat *f = gScr->format;
  int bpp = f->BytesPerPixel;
  int n = IW * IH;
  if (!axMap) axMap = (int *)malloc(sizeof(int) * SW);
  const u32 *src = fb;
  bool native = bpp == 4 && f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0 && f->Rloss == 0 && f->Gloss == 0 && f->Bloss == 0;
  if (!native) {       // перевод в формат экрана (только если он не 0x00RRGGBB)
    if (!convBuf) convBuf = (u32 *)malloc(sizeof(u32) * n);
    for (int i = 0; i < n; i++) {
      u32 c = fb[i];
      convBuf[i] = (((c >> 16) & 255) >> f->Rloss << f->Rshift) | (((c >> 8) & 255) >> f->Gloss << f->Gshift) |
                   ((c & 255) >> f->Bloss << f->Bshift);
    }
    src = convBuf;
  }
  int PW = rotMode ? SH : SW, avail = imax(1, PW - insL - insR);
  if (rotMode) {       // транспонируем блоками: строки rotBuf = столбцы картинки (читать потом подряд)
    if (!rotBuf) rotBuf = (u32 *)malloc(sizeof(u32) * n);
    const int B = 16;
    for (int vb = 0; vb < IH; vb += B)
      for (int ub = 0; ub < IW; ub += B) {
        int ve = imin(vb + B, IH), ue = imin(ub + B, IW);
        for (int v = vb; v < ve; v++) {
          const u32 *sr = src + v * IW;
          for (int u = ub; u < ue; u++) rotBuf[u * IH + v] = sr[u];
        }
      }
    for (int x = 0; x < SW; x++) axMap[x] = imin((rotDir > 0 ? SW - 1 - x : x) * IH / SW, IH - 1);
  } else {
    for (int x = 0; x < SW; x++) axMap[x] = clampi((x - insL) * IW / avail, 0, IW - 1);
  }
  unsigned char *base = (unsigned char *)gScr->pixels;
  int prevBy = -1;
  unsigned char *prevRow = NULL;
  for (int y = 0; y < SH; y++) {
    unsigned char *row = base + y * gScr->pitch;
    int by;
    const u32 *sr;
    if (!rotMode) {
      by = imin(y * IH / SH, IH - 1);
      sr = src + by * IW;
    } else {
      int pu = rotDir > 0 ? y : SH - 1 - y;
      if (pu < insL || pu >= SH - insR) { memset(row, 0, SW * bpp); prevBy = -1; continue; }   // поле экрана
      by = clampi((pu - insL) * IW / avail, 0, IW - 1);
      sr = rotBuf + by * IH;
    }
    if (by == prevBy) { memcpy(row, prevRow, SW * bpp); continue; }
    if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < SW; x++) d[x] = sr[axMap[x]]; }
    else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < SW; x++) d[x] = (uint16_t)sr[axMap[x]]; }
    else for (int x = 0; x < SW; x++) { u32 v = sr[axMap[x]]; row[x * 3] = v & 255; row[x * 3 + 1] = (v >> 8) & 255; row[x * 3 + 2] = (v >> 16) & 255; }
    if (!rotMode) {   // поля слева и справа
      if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
      else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
    }
    prevBy = by;
    prevRow = row;
  }
  if (SDL_MUSTLOCK(gScr)) SDL_UnlockSurface(gScr);
  SDL_Flip(gScr);
}
static void gfxQuit() { SDL_Quit(); }
#endif

static bool pollEv(Ev &e) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: e.type = 0; e.x = e.y = 0; return true;
      case SDL_MOUSEBUTTONDOWN: e.type = 1; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEBUTTONUP: e.type = 2; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEMOTION: e.type = 3; e.x = ev.motion.x; e.y = ev.motion.y; return true;
      case SDL_KEYDOWN:
        if (ev.key.keysym.sym == SDLK_ESCAPE) { e.type = 4; e.x = e.y = 0; return true; }
        break;
#if SDL_MODE == 2
      case SDL_APP_WILLENTERBACKGROUND:
      case SDL_APP_TERMINATING:
      case SDL_APP_LOWMEMORY:
        e.type = 5; e.x = e.y = 0; return true;
#else
      case SDL_ACTIVEEVENT:
        if (ev.active.gain == 0) { e.type = 5; e.x = e.y = 0; return true; }
        break;
#endif
      default: break;
    }
  }
  return false;
}

// окно на Android часто сначала меньше экрана (пока прячутся системные панели), а потом вырастает:
// если не пересчитать размер, справа остаётся чёрная полоса
static bool gfxCheckResize() {
#if SDL_MODE == 2
  int w = 0, h = 0; SDL_GetWindowSize(gWin, &w, &h);
  if (w > 0 && h > 0 && (w != SW || h != SH)) { SW = w; SH = h; return true; }
#endif
  return false;
}

static void mapPt(int x, int y, int &mx, int &my) {
  int pu, pv;   // pu - физ. координата вдоль оси X картинки, pv - вдоль оси Y
  if (!rotMode) { pu = x; pv = y; }
  else if (rotDir > 0) { pu = y; pv = SW - 1 - x; }
  else { pu = SH - 1 - y; pv = x; }
  int PW = rotMode ? SH : SW, PH = rotMode ? SW : SH;
  int avail = imax(1, PW - insL - insR);
  mx = clampi((pu - insL) * IW / avail, 0, IW - 1);
  my = clampi(pv * IH / imax(1, PH), 0, IH - 1);
}
static void computeSize() {
  static const int T[3] = {360, 480, 600};
  int md = imax(1, imin(SW, SH));
  int qe = imax(0, quality - gResDrop);
  IW = imax(200, LW * T[qe] / md);
  IH = imax(120, LH * T[qe] / md);
  fs = imax(1, imin(IW, IH) / 170);
}
static bool allocBuffers() {
  free(fb); free(zb);
  fb = (u32 *)malloc(sizeof(u32) * IW * IH);
  zb = (float *)malloc(sizeof(float) * IW * IH);
  if (!fb || !zb) return false;
  memset(fb, 0, sizeof(u32) * IW * IH);
  gfxResize();
  gfxTex();
  return true;
}
// Настройки экрана (поля) хранятся отдельно от игры
// Папка для файлов игры. На Android рабочая папка недоступна для записи, поэтому пишем во внутреннюю память приложения
// (иначе деньги и прогресс не сохранялись). На других системах - рядом с игрой, как раньше.
static char gBase[400] = "";
static char gPathBuf[4][520];
static int gPathI = 0;
static const char *pth(const char *name) {
  gPathI = (gPathI + 1) & 3;
  snprintf(gPathBuf[gPathI], sizeof gPathBuf[0], "%s%s", gBase, name);
  return gPathBuf[gPathI];
}
static const char *CFGF = "remont.cfg";
static int autoInset() { return rotMode ? SH * 19 / 1000 : 0; }
static void saveCfg() {
  FILE *f = fopen(pth(CFGF), "w");
  if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %d %d 2\n", insL, insR, rotDir, quality, aaOn ? 1 : 0, fpsOn ? 1 : 0, autoPerf ? 1 : 0, soundOn ? 1 : 0);
  fclose(f);
}
static void loadCfg() {
  FILE *f = fopen(pth(CFGF), "r");
  if (!f) return;
  int a = 0, b = 0, d = 1, q = 1, aa = 1, fp = 1, ap = 1, so = 1, ver = 0;
  int got = fscanf(f, "%d %d %d %d %d %d %d %d %d", &a, &b, &d, &q, &aa, &fp, &ap, &so, &ver);
  if (got < 9 || ver < 2) { a = 0; b = 0; }          // старые поля экрана не берём: приложение теперь рисует на весь экран
  if (got >= 8) soundOn = so != 0;
  if (got >= 3) { insL = clampi(a, 0, 260); insR = clampi(b, 0, 260); rotDir = d < 0 ? -1 : 1; }
  if (got >= 4) quality = clampi(q, 0, 2);
  if (got >= 5) aaOn = aa != 0;
  if (got >= 6) fpsOn = fp != 0;
  if (got >= 7) autoPerf = ap != 0;
  fclose(f);
}

// ---------- Шрифт 5x7 (заглавные, латиница + кириллица) ----------
static const unsigned char FLAT[26][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
static const unsigned char FDIG[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
static const unsigned char FCYR[32][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},{0x06,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},{0x11,0x11,0x13,0x15,0x19,0x11,0x11},
  {0x0A,0x11,0x13,0x15,0x19,0x11,0x11},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x07,0x09,0x09,0x09,0x09,0x09,0x11},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x0F,0x01,0x11,0x0E},{0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x12,0x12,0x12,0x12,0x12,0x1F,0x01},{0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},{0x15,0x15,0x15,0x15,0x15,0x1F,0x01},{0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
  {0x11,0x11,0x11,0x19,0x15,0x15,0x19},{0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
  {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}};
static const unsigned char FYO[7] = {0x0A,0x1F,0x10,0x1E,0x10,0x10,0x1F};
struct PG { char c; unsigned char r[7]; };
static const PG FPUN[] = {
  {'.',{0,0,0,0,0,0x0C,0x0C}},{',',{0,0,0,0,0x0C,0x04,0x08}},{':',{0,0x0C,0x0C,0,0x0C,0x0C,0}},
  {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},{'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},{'-',{0,0,0,0x1F,0,0,0}},
  {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}},{'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},{'$',{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
  {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},{')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},{'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'=',{0,0,0x1F,0,0x1F,0,0}},{'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'_',{0,0,0,0,0,0,0x1F}},
  {'\'',{0x04,0x04,0x08,0,0,0,0}},{'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},{'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0}},
  {'~',{0,0,0x08,0x15,0x02,0,0}},{'"',{0x0A,0x0A,0x0A,0,0,0,0}}};
static const int NPUN = sizeof(FPUN) / sizeof(FPUN[0]);

static const unsigned char *glyph(int cp) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  if (cp == 0x451) cp = 0x401;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp >= 'A' && cp <= 'Z') return FLAT[cp - 'A'];
  if (cp >= '0' && cp <= '9') return FDIG[cp - '0'];
  if (cp >= 0x410 && cp <= 0x42F) return FCYR[cp - 0x410];
  if (cp == 0x401) return FYO;
  if (cp < 128)
    for (int i = 0; i < NPUN; i++)
      if (FPUN[i].c == (char)cp) return FPUN[i].r;
  return blank;
}
static int u8next(const char *&p) {
  unsigned char c = (unsigned char)*p;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { int r = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return r; }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { int r = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return r; }
  p++;
  return '?';
}
static int textW(const char *s, int sc) {
  int n = 0;
  while (*s) { u8next(s); n++; }
  return n * 6 * sc;
}
static inline void putpx(int x, int y, u32 c) {
  if ((unsigned)x < (unsigned)IW && (unsigned)y < (unsigned)IH) fb[y * IW + x] = c;
}
static void fillRect(int x, int y, int w, int h, u32 c) {
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    if (gOverlay) { u32 cc = c | 0xFF000000u; for (int i = x0; i < x1; i++) r[i] = cc; }
    else for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      if (gOverlay) { u32 da = d >> 24; if (!da) { r[i] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
      r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
static void fillCircle(int cx, int cy, int r, u32 c, int a) {
  int ia = 255 - a;
  for (int j = -r; j <= r; j++) {
    int y = cy + j;
    if ((unsigned)y >= (unsigned)IH) continue;
    int w = (int)sqrtf((float)(r * r - j * j));
    for (int i = -w; i <= w; i++) {
      int x = cx + i;
      if ((unsigned)x >= (unsigned)IW) continue;
      u32 d = fb[y * IW + x];
      if (gOverlay) { u32 da = d >> 24; if (!da) { fb[y * IW + x] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
      fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
// align: 0 слева, 1 по центру, 2 справа
static void drawText(int x, int y, const char *s, int sc, u32 col, int align) {
  int w = textW(s, sc);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  while (*s) {
    int cp = u8next(s);
    const unsigned char *g = glyph(cp);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fillRect(x + c * sc, y + r * sc, sc, sc, col);
    x += 6 * sc;
  }
}
static void drawTextSh(int x, int y, const char *s, int sc, u32 col, int align) {
  drawText(x + sc, y + sc, s, sc, RGB(0, 0, 0), align);
  drawText(x, y, s, sc, col, align);
}
// ---------- Камера и растеризатор ----------
static V3 camEye, camF, camR, camU;
static void setCamera(V3 eye, V3 target) {
  camEye = eye;
  camF = vnorm(target - eye);
  camR = vnorm(cross(v3(0, 1, 0), camF));
  camU = cross(camF, camR);
}
static inline V3 toView(V3 p) {
  V3 d = p - camEye;
  return v3(dot(d, camR), dot(d, camU), dot(d, camF));
}
static bool projectPt(V3 p, int &sx, int &sy, float &z) {
  V3 v = toView(p);
  z = v.z;
  if (v.z < 0.5f) return false;
  sx = (int)(vcx + v.x * focal / v.z);
  sy = (int)(vcy - v.y * focal / v.z);
  return true;
}

static void rasterTri(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, u32 col) {
  // сортируем вершины по Y: a <= b <= c
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay);
  float dxac = (cx - ax) * iac, dwac = (cw - aw) * iac;
  float dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f;
    float xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f;
    float w = wl + dw * (xs + 0.5f - xl);
    float *zp = zb + y * IW + xs;
    u32 *fp = fb + y * IW + xs;
    int cnt = xe - xs + 1;
    for (; cnt >= 4; cnt -= 4, zp += 4, fp += 4, w += dw * 4.0f) {      // по 4 пикселя за проход
      float w1 = w + dw, w2 = w1 + dw, w3 = w2 + dw;
      if (w > zp[0]) { zp[0] = w; fp[0] = col; }
      if (w1 > zp[1]) { zp[1] = w1; fp[1] = col; }
      if (w2 > zp[2]) { zp[2] = w2; fp[2] = col; }
      if (w3 > zp[3]) { zp[3] = w3; fp[3] = col; }
    }
    for (; cnt > 0; cnt--, zp++, fp++, w += dw) {
      if (w > *zp) { *zp = w; *fp = col; }
    }
  }
}

// Мягкая тень: умножает цвет пикселей под многоугольником (k/256), глубину не пишет
static void rasterTriShadow(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, int k) {
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay), dxac = (cx - ax) * iac, dwac = (cw - aw) * iac, dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f, w = wl + dw * (xs + 0.5f - xl);
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw) {
      float zv = zb[idx];
      if (zv > 0.0f && w > zv * 0.9985f) {
        u32 c = fb[idx];
        fb[idx] = ((((c >> 16) & 255u) * (u32)k >> 8) << 16) | ((((c >> 8) & 255u) * (u32)k >> 8) << 8) | (((c & 255u) * (u32)k) >> 8);
      }
    }
  }
}
static void drawPolyView(const V3 *in, int n, u32 col, int shadowK = 0) {
  if (gDry) return;
  if (gGL) {
    if (shadowK || n < 3) return;
    bool any = false; for (int i = 0; i < n; i++) if (in[i].z > gNearZ) { any = true; break; } if (!any) return;   // целиком за камерой
    unsigned char cr = (col >> 16) & 255, cg = (col >> 8) & 255, cb = col & 255;
    for (int i = 1; i + 1 < n; i++) { const V3 *t[3] = {&in[0], &in[i], &in[i + 1]};
      for (int k = 0; k < 3; k++) { GV g; g.x = t[k]->x; g.y = t[k]->y; g.z = t[k]->z; g.r = cr; g.g = cg; g.b = cb; g.a = 255; gVerts.push_back(g); } }
    return;
  }
  V3 v[32];
  int m = 0;
  const float NZ = gNearZ;
  for (int i = 0; i < n; i++) {
    V3 a = in[i], b = in[(i + 1) % n];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) v[m++] = a;
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m++] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
    }
  }
  if (m < 3) return;
  float sx[32], sy[32], sw[32], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    sx[i] = vcx + v[i].x * focal * iz;
    sy[i] = vcy - v[i].y * focal * iz;
    sw[i] = iz;
    zs += v[i].z;
  }
  zs /= m;
  if (shadowK) {
    for (int i = 1; i + 1 < m; i++) rasterTriShadow(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], shadowK);
    return;
  }
  if (zs > fogA) col = mixc(col, fogCol, (zs - fogA) / (fogB - fogA));
  for (int i = 1; i + 1 < m; i++) rasterTri(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], col);
}

static V3 LDIR;
static float gLight = 1.0f, gNight = 0.0f;        // освещённость мира (1 - день) и «ночность» (0 день ... 1 ночь)
static float lightOf(V3 n) {
  float diff = fmaxv(0.0f, dot(n, LDIR));
  float amb = n.y < -0.4f ? 0.30f : 0.38f + 0.14f * n.y;
  return (amb + 0.62f * diff) * gLight;
}
// Глянец кодируется в старшем байте цвета (0 - матовый, 255 - зеркальный)
#define SHINE(c, sh) (((c) & 0xFFFFFFu) | ((u32)(sh) << 24))

// Грань с известной нормалью: отсечение обратных, свет, отрисовка
// Растеризатор с плавным цветом по треугольнику (освещение интерполируется между вершинами)
// вершина: [0]=x, [1]=y, [2]=1/z, [3]=R, [4]=G, [5]=B
static void rasterTriG(float *a, float *b, float *c) {
  float *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, dr = (R[2] - L[2]) * inv, dg = (R[3] - L[3]) * inv, db = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, rr = L[2] + dr * off, gg = L[3] + dg * off, bb = L[4] + db * off;
    int idx = y * IW + xs;
    {
      float n1 = (float)(xe - xs), r1 = rr + dr * n1, g1 = gg + dg * n1, b1 = bb + db * n1;
      if (rr >= 0.0f && rr <= 255.0f && r1 >= 0.0f && r1 <= 255.0f && gg >= 0.0f && gg <= 255.0f && g1 >= 0.0f && g1 <= 255.0f &&
          bb >= 0.0f && bb <= 255.0f && b1 >= 0.0f && b1 <= 255.0f) {      // быстрый путь: цвет не выходит за 0..255
        for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
          if (w > zb[idx]) { zb[idx] = w; fb[idx] = (u32)(((int)rr << 16) | ((int)gg << 8) | (int)bb); }
        }
        continue;
      }
    }
    for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
      if (w > zb[idx]) {
        zb[idx] = w;
        int ir = (int)rr, ig = (int)gg, ib = (int)bb;
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
        ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
        ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
// Многоугольник с цветами в вершинах: отсечение по ближней плоскости, туман, веер треугольников
// Треугольник с текстурой (ливрея): атрибуты [0]=x, [1]=y, [2]=1/z, [3]=u, [4]=v, [5]=освещённость; tc - цвета уровня текстуры, rx0..ry1 - рамка проекции на этом уровне
static void rasterTriT(const float *a0, const float *b0, const float *c0, const u32 *tc, int stride, int rx0, int ry0, int rx1, int ry1, float invs, float fogT) {
  const float *a = a0, *b = b0, *c = c0, *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbl = (float)(fogCol & 255);
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, du = (R[2] - L[2]) * inv, dv = (R[3] - L[3]) * inv, dl = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, u = L[2] + du * off, v = L[3] + dv * off, l = L[4] + dl * off;
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw, u += du, v += dv, l += dl) {
      if (w > zb[idx]) {
        int tx = (int)(u * invs), ty = (int)(v * invs);
        tx = tx < rx0 ? rx0 : (tx > rx1 ? rx1 : tx);
        ty = ty < ry0 ? ry0 : (ty > ry1 ? ry1 : ty);
        u32 pc = tc[ty * stride + tx];
        float lk = l < 0.0f ? 0.0f : (l > 1.3f ? 1.3f : l);
        float cr = ((pc >> 16) & 255) * lk, cg = ((pc >> 8) & 255) * lk, cb = (pc & 255) * lk;
        if (fogT > 0.0f) { cr += (fr - cr) * fogT; cg += (fg - cg) * fogT; cb += (fbl - cb) * fogT; }
        int ir = cr > 255.0f ? 255 : (int)cr, ig = cg > 255.0f ? 255 : (int)cg, ib = cb > 255.0f ? 255 : (int)cb;
        zb[idx] = w; fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
static void drawPolyViewG(const V3 *in, const float (*cin)[3], int n) {
  if (gDry) return;
  if (n == 3 && in[0].z >= 0.3f && in[1].z >= 0.3f && in[2].z >= 0.3f) {      // целый треугольник перед камерой: без отсечения
    float vt3[3][6];
    float zs3 = (in[0].z + in[1].z + in[2].z) * (1.0f / 3.0f);
    for (int i = 0; i < 3; i++) {
      float iz = 1.0f / in[i].z;
      vt3[i][0] = vcx + in[i].x * focal * iz; vt3[i][1] = vcy - in[i].y * focal * iz; vt3[i][2] = iz;
      vt3[i][3] = cin[i][0]; vt3[i][4] = cin[i][1]; vt3[i][5] = cin[i][2];
    }
    if (zs3 > fogA) {
      float t = clampf((zs3 - fogA) / (fogB - fogA), 0.0f, 1.0f);
      float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
      for (int i = 0; i < 3; i++) { vt3[i][3] += (fr - vt3[i][3]) * t; vt3[i][4] += (fg - vt3[i][4]) * t; vt3[i][5] += (fbb - vt3[i][5]) * t; }
    }
    {   // мелкий треугольник (до ~20 пикселей): плавный цвет не виден, красим одним средним цветом - в разы быстрее
      float ar2 = fabsf((vt3[1][0] - vt3[0][0]) * (vt3[2][1] - vt3[0][1]) - (vt3[2][0] - vt3[0][0]) * (vt3[1][1] - vt3[0][1]));
      if (ar2 < 40.0f) {
        int ir = (int)((vt3[0][3] + vt3[1][3] + vt3[2][3]) * (1.0f / 3.0f)), ig = (int)((vt3[0][4] + vt3[1][4] + vt3[2][4]) * (1.0f / 3.0f)),
            ib = (int)((vt3[0][5] + vt3[1][5] + vt3[2][5]) * (1.0f / 3.0f));
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir); ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig); ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        rasterTri(vt3[0][0], vt3[0][1], vt3[0][2], vt3[1][0], vt3[1][1], vt3[1][2], vt3[2][0], vt3[2][1], vt3[2][2], (u32)((ir << 16) | (ig << 8) | ib));
        return;
      }
    }
    rasterTriG(vt3[0], vt3[1], vt3[2]);
    return;
  }
  V3 v[32];
  float cv[32][3];
  int m = 0;
  const float NZ = gNearZ;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 a = in[i], b = in[j];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) { v[m] = a; cv[m][0] = cin[i][0]; cv[m][1] = cin[i][1]; cv[m][2] = cin[i][2]; m++; }
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
      for (int k = 0; k < 3; k++) cv[m][k] = cin[i][k] + (cin[j][k] - cin[i][k]) * t;
      m++;
    }
  }
  if (m < 3) return;
  float vt[32][6], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    vt[i][0] = vcx + v[i].x * focal * iz;
    vt[i][1] = vcy - v[i].y * focal * iz;
    vt[i][2] = iz;
    vt[i][3] = cv[i][0]; vt[i][4] = cv[i][1]; vt[i][5] = cv[i][2];
    zs += v[i].z;
  }
  zs /= m;
  if (zs > fogA) {
    float t = clampf((zs - fogA) / (fogB - fogA), 0.0f, 1.0f);
    float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
    for (int i = 0; i < m; i++) {
      vt[i][3] += (fr - vt[i][3]) * t; vt[i][4] += (fg - vt[i][4]) * t; vt[i][5] += (fbb - vt[i][5]) * t;
    }
  }
  for (int i = 1; i + 1 < m; i++) rasterTriG(vt[0], vt[i], vt[i + 1]);
}
// цвет в вершине: освещение по нормали вершины плюс глянец (те же формулы, что у плоской грани)
// Цвет «окружения» в направлении R: светлый горизонт и голубое небо сверху, тёмная дорога снизу
static void envColor(V3 R, float *o) {
  if (R.y >= 0) {
    float t = sqrtf(fminv(1.0f, R.y));
    o[0] = 238.0f + (40.0f - 238.0f) * t; o[1] = 244.0f + (96.0f - 244.0f) * t; o[2] = 250.0f + (196.0f - 250.0f) * t;
  } else {
    float t = fminv(1.0f, -R.y * 2.2f);
    o[0] = 120.0f + (22.0f - 120.0f) * t; o[1] = 124.0f + (24.0f - 124.0f) * t; o[2] = 130.0f + (28.0f - 130.0f) * t;
  }
}
// Глянец: отражение окружения (сильнее под скользящим углом, как у лака и стекла) и яркий блик солнца. c = r, g, b (0..255)
static void glossMix(V3 p, V3 nrm, int shi, float *c) {
  V3 vd = vnorm(camEye - p), hv = vnorm(vd + LDIR);
  float nd = dot(nrm, vd), fb = 1.0f - fmaxv(0.0f, nd), fr = fb * fb * fb, g = shi * (1.0f / 255.0f);
  V3 R = nrm * (2.0f * nd) - vd;
  float e[3];
  envColor(R, e);
  float kr = clampf(g * (0.30f + 0.70f * fr), 0.0f, 0.85f);
  c[0] += (e[0] - c[0]) * kr; c[1] += (e[1] - c[1]) * kr; c[2] += (e[2] - c[2]) * kr;
  float sb = fmaxv(0.0f, dot(nrm, hv)), s2 = sb * sb, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
  float k2 = clampf(s16 * s8 * s4 * g, 0.0f, 0.9f);
  c[0] += (255.0f - c[0]) * k2; c[1] += (255.0f - c[1]) * k2; c[2] += (255.0f - c[2]) * k2;
}
// цвет в вершине: освещение по нормали, затемнение у земли и глянец
static void shadeVertex(V3 p, V3 nrm, u32 col, int shi, float *o) {
  float l = lightOf(nrm);
  float c[3] = {((col >> 16) & 255) * l, ((col >> 8) & 255) * l, (col & 255) * l};
  if (shi) {
    float ao = 0.52f + 0.48f * clampf(p.y * (1.0f / 1.0f), 0.0f, 1.0f);     // низ кузова темнее: машина «стоит» на земле
    c[0] *= ao; c[1] *= ao; c[2] *= ao;
    glossMix(p, nrm, shi, c);
  }
  o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
}
// то же для плоской грани: возвращает готовый цвет
static u32 shadeFlat(V3 fc, V3 nrm, u32 col, int shi, bool lit) {
  col &= 0xFFFFFFu;
  if (!shi) return lit ? shadec(col, lightOf(nrm)) : col;
  float l = lit ? lightOf(nrm) : 1.0f;
  float ao = lit ? 0.52f + 0.48f * clampf(fc.y * (1.0f / 1.0f), 0.0f, 1.0f) : 1.0f;
  float c[3] = {((col >> 16) & 255) * l * ao, ((col >> 8) & 255) * l * ao, (col & 255) * l * ao};
  glossMix(fc, nrm, shi, c);
  return RGB((int)clampf(c[0], 0.0f, 255.0f), (int)clampf(c[1], 0.0f, 255.0f), (int)clampf(c[2], 0.0f, 255.0f));
}
static void faceSmooth(const V3 *p, const V3 *nv, int n, V3 fnrm, u32 col) {
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(fnrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[8];
  float cc[8][3];
  for (int i = 0; i < n && i < 8; i++) { vv[i] = toView(p[i]); shadeVertex(p[i], nv[i], col, shi, cc[i]); }
  drawPolyViewG(vv, cc, n);
}
static void faceN(const V3 *p, int n, V3 nrm, u32 col, bool lit) {
  if (gDry) return;
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(nrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[24];
  for (int i = 0; i < n; i++) vv[i] = toView(p[i]);
  u32 c2 = shadeFlat(fc, nrm, col, shi, lit);
  drawPolyView(vv, n, c2);
}
static void quadFace(V3 c, V3 u, V3 v, V3 n, u32 col, bool lit) {
  V3 p[4];
  p[0] = c - u - v; p[1] = c + u - v; p[2] = c + u + v; p[3] = c - u + v;
  faceN(p, 4, n, col, lit);
}
// Параллелепипед: центр, полуразмеры, поворот вокруг Y
static void drawBox(V3 c, V3 h, float yaw, u32 col, bool lit) {
  if (gDry) return;
  float cs = cosf(yaw), sn = sinf(yaw);
  V3 ax = v3(cs, 0, -sn), ay = v3(0, 1, 0), az = v3(sn, 0, cs);
  V3 ex = ax * h.x, ey = ay * h.y, ez = az * h.z;
  quadFace(c + ey, ex, ez, ay, col, lit);
  quadFace(c + ex, ey, ez, ax, col, lit);
  quadFace(c - ex, ey, ez, ax * -1.0f, col, lit);
  quadFace(c + ez, ex, ey, az, col, lit);
  quadFace(c - ez, ex, ey, az * -1.0f, col, lit);
}
// Призма (колесо) вокруг оси X кузова
static void drawWheel(V3 c, V3 ax, V3 ay, V3 az, float r, float hw, u32 col) {
  // мелкие детали (фары, колпаки) рисуются с меньшим числом граней - на картинке не видно, а полигонов втрое меньше
  int N = r < 0.13f ? imin(wheelN, 8) : (r < 0.25f ? imin(wheelN, 12) : wheelN);
  static float CT[25][24], ST[25][24];
  static bool tabOk[25];
  if (N < 6) N = 6;
  if (!tabOk[N]) {
    for (int i = 0; i < N; i++) { float t = 6.2831853f * i / N; CT[N][i] = cosf(t); ST[N][i] = sinf(t); }
    tabOk[N] = true;
  }
  V3 ring[24];
  for (int i = 0; i < N; i++) ring[i] = ay * (r * CT[N][i]) + az * (r * ST[N][i]);
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    V3 p[4];
    p[0] = c + ring[i] - ax * hw; p[1] = c + ring[j] - ax * hw;
    p[2] = c + ring[j] + ax * hw; p[3] = c + ring[i] + ax * hw;
    V3 mid = vnorm(ring[i] + ring[j]);
    faceN(p, 4, mid, col, true);
  }
  V3 cap[24];
  for (int s = -1; s <= 1; s += 2) {
    for (int i = 0; i < N; i++) cap[i] = c + ring[i] + ax * (hw * s);
    faceN(cap, N, ax * (float)s, col, true);
  }
}

static void setClipRect(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
static void clearDepth() {
  if (gGL) return;
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  if (gGL) { gSkyCol = fogCol; return; }
  float horizon = vcy + focal * camF.y / sqrtf(fmaxv(0.05f, 1 - camF.y * camF.y));
  u32 top = mixc(RGB(60, 70, 110), RGB(6, 8, 26), gNight);
  for (int y = clipY0; y < clipY1; y++) {
    u32 c = y < horizon ? mixc(top, fogCol, clampf((y - clipY0) / fmaxv(1.0f, horizon - clipY0), 0, 1)) : fogCol;
    u32 *r = fb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = c;
  }
}
static void groundQuad(float x0, float z0, float x1, float z1, float y, u32 col) {
  if (gLight < 0.999f) col = RGB((int)(CR(col) * gLight), (int)(CG(col) * gLight), (int)(CB(col) * gLight));
  V3 p[4] = {v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1)};
  V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]);
  drawPolyView(vv, 4, col);
}
static void discOct(float cx, float cz, float r, float y, u32 col) {
  V3 vv[8];
  for (int i = 0; i < 8; i++) { float t = 6.2831853f * i / 8 + 0.3927f; vv[i] = toView(v3(cx + cosf(t) * r, y, cz + sinf(t) * r)); }
  drawPolyView(vv, 8, col);
}
static bool hitR(int x, int y, int w, int h) {
  if (!P.clicked) return false;
  if (P.cx >= x && P.cx < x + w && P.cy >= y && P.cy < y + h && P.sx >= x && P.sx < x + w && P.sy >= y && P.sy < y + h) { P.clicked = false; return true; }
  return false;
}
static bool button(int x, int y, int w, int h, const char *label, bool en, u32 col) {
  u32 c = en ? col : RGB(58, 60, 70);
  fillRect(x, y, w, h, shadec(c, 0.55f));
  fillRect(x + 1, y + 1, w - 2, h - 2, c);
  fillRect(x + 1, y + 1, w - 2, imax(1, h / 8), shadec(c, 1.25f));
  char lines[3][64]; int n = 0; const char *p = label;
  while (n < 3) {
    const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p); if (len > 63) len = 63;
    memcpy(lines[n], p, len); lines[n][len] = 0; n++; if (!e) break; p = e + 1;
  }
  int sc = fs; while (sc > 1 && textW(lines[0], sc) > w - 4) sc--;
  int lh = 9 * sc, y0 = y + (h - n * lh + 2 * sc) / 2;
  for (int i = 0; i < n; i++) drawText(x + w / 2, y0 + i * lh, lines[i], sc, en ? RGB(255, 255, 255) : RGB(140, 142, 152), 1);
  return en && hitR(x, y, w, h);
}
static void drawLine(int x0, int y0, int x1, int y1, u32 c, int th) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, n = 0;
  while (n++ < 4000) {
    fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; }
  }
}


// =====================================================================
//  РЕМОНТ КВАРТИР: квартира, задачи, деньги
// =====================================================================
#include <algorithm>
#include <string>
enum { R_OUT, R_HALL, R_LIV, R_KIT, R_BATH, NROOM };
static const char *ROOMN[NROOM] = {"УЛИЦА", "КОРИДОР", "КОМНАТА", "КУХНЯ", "ВАННАЯ"};
enum { MW = 18, MH = 14 };
struct Layout { const char *name; const char *rows[MH]; };
// H коридор, L комната, K кухня, B ванная, пробел - снаружи; вход - первая клетка коридора у края.
// Планировки идут по возрастанию площади; цена считается от площади (см. genOffers).
static const Layout LAYOUTS[] = {
  {"СТУДИЯ У МЕТРО", {"                  ", " LLLLLKKK         ", " LLLLLKKK         ", " LLLLLKKK         ", " LLLLLHHH         ", "      HHH         ", "      BBB         ", "      BBB         ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"ОДНУШКА-ХРУЩЁВКА", {"                  ", " LLLLLKKKK        ", " LLLLLKKKK        ", " LLLLLKKKK        ", " LLLLLHHHH        ", " LLLLLHHHH        ", "      BBBB        ", "      BBBB        ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"ОДНУШКА НОВАЯ", {"                  ", " LLLLLKKKKK       ", " LLLLLKKKKK       ", " LLLLLKKKKK       ", " LLLLLKKKKK       ", " LLLLLHHHHH       ", " LLLLLHHHHH       ", "      BBBB        ", "      BBBB        ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"ДВУШКА-КОРИДОР", {"                  ", " HHLLLLL          ", " HHLLLLL          ", " HHLLLLL          ", " HHLLLLL          ", " HHBBB            ", " HHBBB            ", " HHKKKK           ", " HHKKKK           ", " HHKKKK           ", " HHLLLLL          ", " HHLLLLL          ", " HHLLLLL          ", "                  "}},
  {"ДВУШКА У ПАРКА", {"                  ", " LLLLLLKKKKK      ", " LLLLLLKKKKK      ", " LLLLLLKKKKK      ", " LLLLLLKKKKK      ", " HHHHHHHHHHH      ", " HHHHHHHHHHH      ", " LLLLLLBBBB       ", " LLLLLLBBBB       ", " LLLLLLBBBB       ", " LLLLLL           ", "                  ", "                  ", "                  "}},
  {"ТРЁШКА-РАСПАШОНКА", {"                  ", " LLLLLLHHKKKKK    ", " LLLLLLHHKKKKK    ", " LLLLLLHHKKKKK    ", " LLLLLLHHKKKKK    ", "       HHBBBB     ", " LLLLLLHHBBBB     ", " LLLLLLHHLLLLL    ", " LLLLLLHHLLLLL    ", " LLLLLLHHLLLLL    ", "       HHLLLLL    ", "                  ", "                  ", "                  "}},
  {"ТРЁШКА В ЦЕНТРЕ", {"                  ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " HHHHHHHHHHHHHHHH ", " HHHHHHHHHHHHHHHH ", " LLLLLLBBBB       ", " LLLLLLBBBB       ", " LLLLLLBBBB       ", " LLLLLL           ", "                  ", "                  ", "                  "}},
  {"ЧЕТВЁРТКА-ПЕНТХАУС", {"                  ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " LLLLLLKKKKKLLLLL ", " HHHHHHHHHHHHHHHH ", " HHHHHHHHHHHHHHHH ", " LLLLLLBBBBLLLLLL ", " LLLLLLBBBBLLLLLL ", " LLLLLLBBBBLLLLLL ", " LLLLLL    LLLLLL ", " LLLLLL    LLLLLL ", "                  "}},
  {"ГОСТИНКА", {"                  ", " LLLLKK           ", " LLLLKK           ", " LLLLHH           ", " LLLLHH           ", "     BB           ", "     BB           ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"ДВУШКА-ХРУЩЁВКА", {"                  ", " LLLLLHLLLL       ", " LLLLLHLLLL       ", " LLLLLHLLLL       ", " LLLLLHLLLL       ", " KKKKHHHBBB       ", " KKKKHHHBBB       ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"ЕВРОДВУШКА", {"                  ", " KKKKKKKKLLLLL    ", " KKKKKKKKLLLLL    ", " KKKKKKKKLLLLL    ", " KKKKKKKKLLLLL    ", " HHHHHHHHHHHHH    ", " BBB              ", " BBB              ", "                  ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"ТРЁШКА ПАНЕЛЬНАЯ", {"                  ", " LLLLLKKKKLLLLL   ", " LLLLLKKKKLLLLL   ", " LLLLLKKKKLLLLL   ", " LLLLLKKKKLLLLL   ", " HHHHHHHHHHHHHH   ", " LLLLLLBBB        ", " LLLLLLBBB        ", " LLLLLL           ", "                  ", "                  ", "                  ", "                  ", "                  "}},
  {"СТАЛИНСКАЯ ТРЁШКА", {"                  ", " LLLLLLLHHKKKKKK  ", " LLLLLLLHHKKKKKK  ", " LLLLLLLHHKKKKKK  ", " LLLLLLLHHKKKKKK  ", " LLLLLLLHHBBBB    ", "        HHBBBB    ", " LLLLLLLHHBBBB    ", " LLLLLLLHHLLLLLL  ", " LLLLLLLHHLLLLLL  ", " LLLLLLLHHLLLLLL  ", " LLLLLLLHHLLLLLL  ", "        HH        ", "                  "}},
  {"ПЯТИКОМНАТНАЯ", {"                  ", " LLLLLKKKKKKLLLLL ", " LLLLLKKKKKKLLLLL ", " LLLLLKKKKKKLLLLL ", " LLLLLKKKKKKLLLLL ", " HHHHHHHHHHHHHHHH ", " HHHHHHHHHHHHHHHH ", " LLLLLBBBLLLLBBBB ", " LLLLLBBBLLLLBBBB ", " LLLLLBBBLLLLBBBB ", " LLLLL   LLLL     ", " LLLLL   LLLL     ", " LLLLL   LLLL     ", "                  "}},
};
static const int NLAYOUT = sizeof(LAYOUTS) / sizeof(LAYOUTS[0]);
static int cellR[MW][MH];                          // комната клетки
// грани стен: стена между клеткой (x,z) и соседом справа (+x) или снизу (+z)
struct Face { unsigned char wallpaper, paint, window, putty, paper; };   // старые обои (1), краска (0 нет), окно, зашпаклёвано, новые обои (0 нет)
static bool wallX[MW + 1][MH], wallZ[MW][MH + 1], doorX[MW + 1][MH], doorZ[MW][MH + 1];
static Face fX[MW + 1][MH][2], fZ[MW][MH + 1][2];   // [0] - сторона клетки слева/сверху, [1] - справа/снизу
struct Cell { unsigned char stain, floor, furn, frot; };      // пятно, пол (0 старый, 1 новый), мебель (0 нет), поворот
static Cell cells[MW][MH];
struct Trash { float x, z; int kind; bool alive; };
static std::vector<Trash> trash;
static int layoutId = 0, entX = 0, entZ = 0;                  // вход (клетка коридора у края)
static float binX = 0, binZ = 0;
static int stainsInit = 1;                                    // сколько пятен было при покупке                              // мусорный контейнер за дверью
// краски
// стили ремонта: за единый стиль покупатели платят больше
enum { ST_NONE, ST_LOFT, ST_CLASSIC, ST_SCANDI, ST_MODERN, NSTYLE };
static const char *STYLEN[NSTYLE] = {"БАЗОВАЯ", "ЛОФТ", "КЛАССИКА", "СКАНДИ", "МОДЕРН"};
static const u32 STYLEC[NSTYLE] = {RGB(120, 126, 140), RGB(185, 100, 65), RGB(190, 160, 80), RGB(120, 185, 225), RGB(165, 140, 200)};
static const u32 PAINT[] = {0, RGB(235, 232, 225), RGB(170, 205, 230), RGB(200, 230, 190), RGB(245, 215, 170), RGB(230, 180, 180), RGB(120, 130, 150), RGB(70, 72, 80),
  RGB(185, 105, 75), RGB(130, 50, 60), RGB(150, 160, 120), RGB(215, 210, 225)};
static const char *PAINTN[] = {"", "БЕЛАЯ", "ГОЛУБАЯ", "МЯТНАЯ", "ПЕСОЧНАЯ", "РОЗОВАЯ", "ГРАФИТ", "ТЁМНАЯ", "ТЕРРАКОТА", "БОРДО", "ОЛИВКА", "ЛАВАНДА"};
static const int PAINTS[] = {0, ST_SCANDI, ST_SCANDI, ST_SCANDI, ST_CLASSIC, ST_CLASSIC, ST_MODERN, ST_LOFT, ST_LOFT, ST_CLASSIC, ST_LOFT, ST_MODERN};
static const int NPAINT = 12;
// новые обои: узор и два цвета
struct Paper { const char *name; int kind; u32 a, b; int style; };   // kind: 0 полоска, 1 цветы, 2 ромбы, 3 кирпич, 4 дерево, 5 однотонные с фактурой
static const Paper PAPERS[] = { {"", 0, 0, 0},
  {"ПОЛОСКА", 0, RGB(232, 226, 210), RGB(205, 195, 170), ST_CLASSIC}, {"ЦВЕТЫ", 1, RGB(236, 240, 232), RGB(200, 120, 140), ST_CLASSIC}, {"РОМБЫ", 2, RGB(210, 220, 235), RGB(150, 170, 205), ST_MODERN},
  {"КИРПИЧ", 3, RGB(170, 95, 70), RGB(215, 205, 190), ST_LOFT}, {"ДЕРЕВО", 4, RGB(170, 125, 85), RGB(135, 95, 60), ST_SCANDI}, {"ЛЁН", 5, RGB(220, 214, 200), RGB(205, 198, 182), ST_SCANDI},
  {"ИЗУМРУД", 2, RGB(70, 110, 95), RGB(200, 180, 120), ST_CLASSIC}, {"ГРАФИТ", 5, RGB(80, 84, 92), RGB(70, 74, 82), ST_MODERN},
  {"БЕТОН", 5, RGB(160, 160, 156), RGB(145, 145, 142), ST_LOFT}, {"ГЕОМЕТРИЯ", 2, RGB(238, 238, 240), RGB(60, 62, 70), ST_MODERN},
  {"ВЕНЗЕЛЯ", 1, RGB(225, 205, 160), RGB(150, 110, 60), ST_CLASSIC}, {"ЛИСТЬЯ", 1, RGB(240, 244, 238), RGB(110, 160, 110), ST_SCANDI} };
static const int NPAPER = sizeof(PAPERS) / sizeof(PAPERS[0]);
static const int COST_PUTTY = 300, COST_PAPER = 900;
// мебель
// shape - какую модель рисовать (0 - свою), a/b - свои цвета обивки/корпуса, old - старьё из «бабушкиной» квартиры (не продаётся в магазине)
struct Furn { const char *name; int price, room, value, style, shape; u32 a, b; int old; };
enum { FU_SOFA = 1, FU_BED, FU_TV, FU_WARD, FU_FRIDGE, FU_STOVE, FU_TABLE, FU_BATH, FU_SINK, FU_WC, FU_FLOWER, FU_RUG, FU_LAMP, FU_ARM, FU_COFFEE, FU_BOOKS, FU_KITCH, FU_WASH,
  FU_CRIB = 35, FU_DESK = 36, FU_SIDEBOARD = 38, FU_OLDTV = 39 };
static const Furn FURN[] = { {"", 0, 0, 0},
  {"ДИВАН", 35000, R_LIV, 60000}, {"КРОВАТЬ", 40000, R_LIV, 65000}, {"ТУМБА С ТВ", 30000, R_LIV, 45000}, {"ШКАФ", 25000, R_HALL, 35000},
  {"ХОЛОДИЛЬНИК", 45000, R_KIT, 60000}, {"ПЛИТА", 30000, R_KIT, 45000}, {"СТОЛ СО СТУЛЬЯМИ", 20000, R_KIT, 30000},
  {"ВАННА", 40000, R_BATH, 60000}, {"РАКОВИНА", 15000, R_BATH, 25000}, {"УНИТАЗ", 15000, R_BATH, 25000}, {"ЦВЕТОК", 3000, 0, 6000},
  {"КОВЁР", 8000, 0, 14000, ST_CLASSIC}, {"ТОРШЕР", 7000, 0, 12000}, {"КРЕСЛО", 18000, R_LIV, 28000}, {"ЖУРНАЛЬНЫЙ СТОЛИК", 9000, R_LIV, 15000},
  {"КНИЖНЫЙ ШКАФ", 16000, R_LIV, 26000}, {"КУХОННЫЙ ШКАФ", 28000, R_KIT, 42000}, {"СТИРАЛЬНАЯ МАШИНА", 30000, R_BATH, 42000},
  // 19..34: стильная мебель (те же модели, другие цвета)
  {"КОЖАНЫЙ ДИВАН", 55000, R_LIV, 85000, ST_LOFT, FU_SOFA, RGB(110, 64, 38), RGB(86, 48, 28)},
  {"МЕТАЛЛ. СТЕЛЛАЖ", 22000, R_LIV, 34000, ST_LOFT, FU_BOOKS, RGB(38, 38, 42), RGB(60, 60, 66)},
  {"ЛОФТ-ШКАФ", 30000, R_HALL, 45000, ST_LOFT, FU_WARD, RGB(92, 74, 58)},
  {"ДИВАН КЛАССИКА", 48000, R_LIV, 75000, ST_CLASSIC, FU_SOFA, RGB(135, 40, 52), RGB(108, 30, 40)},
  {"КРЕСЛО БЕРЖЕР", 26000, R_LIV, 40000, ST_CLASSIC, FU_ARM, RGB(62, 108, 72), RGB(46, 86, 56)},
  {"ШКАФ КЛАССИКА", 34000, R_HALL, 50000, ST_CLASSIC, FU_WARD, RGB(118, 76, 44)},
  {"СКАНДИ-ДИВАН", 42000, R_LIV, 66000, ST_SCANDI, FU_SOFA, RGB(188, 190, 194), RGB(164, 166, 171)},
  {"КРЕСЛО СКАНДИ", 20000, R_LIV, 32000, ST_SCANDI, FU_ARM, RGB(226, 176, 62), RGB(196, 148, 46)},
  {"БЕЛЫЙ СТЕЛЛАЖ", 18000, R_LIV, 29000, ST_SCANDI, FU_BOOKS, RGB(240, 238, 232), RGB(222, 214, 200)},
  {"МОДЕРН-ДИВАН", 50000, R_LIV, 78000, ST_MODERN, FU_SOFA, RGB(72, 74, 82), RGB(56, 58, 66)},
  {"КРОВАТЬ МОДЕРН", 52000, R_LIV, 80000, ST_MODERN, FU_BED, RGB(78, 78, 86), RGB(205, 205, 212)},
  {"КУХНЯ ГЛЯНЕЦ", 40000, R_KIT, 60000, ST_MODERN, FU_KITCH, RGB(62, 64, 72), RGB(230, 230, 232)},
  {"ТВ-ПАНЕЛЬ ГЛЯНЕЦ", 38000, R_LIV, 56000, ST_MODERN, FU_TV, RGB(240, 240, 244)},
  {"КУХНЯ ДЕРЕВО", 34000, R_KIT, 50000, ST_CLASSIC, FU_KITCH, RGB(150, 108, 68), RGB(90, 60, 40)},
  {"КУХНЯ СКАНДИ", 32000, R_KIT, 48000, ST_SCANDI, FU_KITCH, RGB(246, 246, 242), RGB(210, 175, 125)},
  {"КУХНЯ ЛОФТ", 36000, R_KIT, 54000, ST_LOFT, FU_KITCH, RGB(56, 56, 60), RGB(150, 100, 62)},
  // 35, 36: новые модели (их ждут покупатели)
  {"ДЕТСКАЯ КРОВАТКА", 18000, R_LIV, 30000},
  {"РАБОЧИЙ СТОЛ С ПК", 25000, R_LIV, 36000},
  // 37..40: старьё (в «бабушкиных» квартирах, вынести руками)
  {"СТАРЫЙ ДИВАН", 3000, R_LIV, 0, ST_NONE, FU_SOFA, RGB(128, 100, 72), RGB(108, 84, 60), 1},
  {"СТАРЫЙ СЕРВАНТ", 4000, R_LIV, 0, ST_NONE, 0, 0, 0, 1},
  {"СТАРЫЙ ТЕЛЕВИЗОР", 2000, R_LIV, 0, ST_NONE, 0, 0, 0, 1},
  {"СТАРЫЙ ШКАФ", 3000, R_HALL, 0, ST_NONE, FU_WARD, RGB(132, 96, 62), 0, 1} };
static const int NFURN = sizeof(FURN) / sizeof(FURN[0]);
static_assert(sizeof(FURN) / sizeof(FURN[0]) == 41, "FURN ids");
static int furnShape(int it) { return it <= 0 || it >= NFURN ? 0 : FURN[it].shape ? FURN[it].shape : it; }
static bool furnSolid(int it) { return furnShape(it) != FU_RUG; }   // по ковру можно ходить
static const int COST_PAINT = 600, COST_FLOOR_LAM = 2500, COST_FLOOR_TILE = 3500;
struct FloorT { const char *name; int cost; bool tile; u32 a, b; int style; bool prem; };   // покрытие пола: название, цена за клетку, плитка ли, два цвета
static const FloorT FLOORS[] = { {"", 0, false, 0, 0},
  {"ЛАМИНАТ", 2500, false, RGB(190, 145, 95), RGB(176, 132, 86)}, {"ПАРКЕТ", 4500, false, RGB(140, 95, 60), RGB(125, 82, 50), ST_CLASSIC, true},
  {"СВЕТЛЫЙ ДУБ", 3000, false, RGB(214, 185, 140), RGB(204, 174, 128), ST_SCANDI}, {"СЕРЫЙ ЛАМИНАТ", 2800, false, RGB(165, 165, 170), RGB(152, 152, 158), ST_MODERN},
  {"ПЛИТКА БЕЛАЯ", 3500, true, RGB(225, 228, 232), RGB(205, 210, 216)}, {"ПЛИТКА ТЁМНАЯ", 3800, true, RGB(84, 88, 98), RGB(70, 74, 84), ST_MODERN, true},
  {"ЛИНОЛЕУМ", 1500, false, RGB(150, 152, 138), RGB(140, 142, 128)}, {"ВЕНГЕ", 3500, false, RGB(78, 56, 44), RGB(66, 46, 36), ST_CLASSIC, true},
  {"ДОСКА ЛОФТ", 3800, false, RGB(112, 82, 58), RGB(96, 70, 50), ST_LOFT, true}, {"КЕРАМОГРАНИТ", 4200, true, RGB(170, 168, 162), RGB(155, 153, 148), ST_LOFT, true},
  {"БЕЛЁНЫЙ ДУБ", 3600, false, RGB(232, 222, 205), RGB(222, 210, 192), ST_SCANDI, true}, {"МРАМОР", 5500, true, RGB(240, 238, 235), RGB(225, 222, 218), ST_MODERN, true} };
static const int NFLOOR = sizeof(FLOORS) / sizeof(FLOORS[0]);
// в каком состоянии квартира, какой дом, какой район
enum { CO_NORMAL, CO_FIRE, CO_FLOOD, CO_GRANNY, CO_BARE, NCOND };
static const char *CONDN[NCOND] = {"УБИТАЯ", "ПОСЛЕ ПОЖАРА", "ПОСЛЕ ПОТОПА", "БАБУШКИНА", "БЕЗ ОТДЕЛКИ"};
static const u32 CONDC[NCOND] = {RGB(220, 190, 150), RGB(255, 130, 90), RGB(120, 190, 255), RGB(220, 170, 230), RGB(190, 190, 190)};
static const float CONDK[NCOND] = {1.0f, 0.84f, 0.88f, 0.93f, 0.96f};       // скидка при покупке
enum { HO_KHRU, HO_PANEL, HO_STALIN, HO_NEW, NHOUSE };
static const char *HOUSEN[NHOUSE] = {"ХРУЩЁВКА", "ПАНЕЛЬКА", "СТАЛИНКА", "НОВОСТРОЙКА"};
static const float HOUSEK[NHOUSE] = {0.85f, 1.0f, 1.3f, 1.18f};
enum { DI_CENTER, DI_SLEEP, DI_EDGE, NDIST };
static const char *DISTN[NDIST] = {"ЦЕНТР", "СПАЛЬНЫЙ РАЙОН", "ОКРАИНА"};
static const float DISTK[NDIST] = {1.3f, 1.0f, 0.82f};
static unsigned flatSeed = 0;
static int flatCond = CO_NORMAL, flatHouse = HO_PANEL, flatDist = DI_SLEEP, oldInit = 0;
static int money = 3600000, flats = 0, carried = 0, flatBuy = 0, spent = 0;
static bool owned = false;
static char msg[128] = ""; static float msgT = 0;
static void say(const char *s) { snprintf(msg, sizeof msg, "%s", s); msgT = 3.0f; }
static bool inRoom(int x, int z) { return x >= 0 && z >= 0 && x < MW && z < MH && cellR[x][z] != R_OUT; }
static bool tiledRoom(int r) { return r == R_KIT || r == R_BATH; }
static void addDoor(int a, int b) {                // дверь между двумя комнатами: первая общая грань
  for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {
    if (x + 1 < MW && ((cellR[x][z] == a && cellR[x + 1][z] == b) || (cellR[x][z] == b && cellR[x + 1][z] == a))) { doorX[x + 1][z] = true; return; }
    if (z + 1 < MH && ((cellR[x][z] == a && cellR[x][z + 1] == b) || (cellR[x][z] == b && cellR[x][z + 1] == a))) { doorZ[x][z + 1] = true; return; }
  }
}
static bool trashOn(int x, int z);
static unsigned rng = 1;
static int rnd(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned)n); }
// старые обои: 1 обычные, 2 обгоревшие, 3 с плесенью, 0 - голый бетон (новостройка)
static unsigned char oldPaper() { int r = rnd(100); return flatCond == CO_BARE ? 0 : flatCond == CO_FIRE ? (r < 70 ? 2 : 1) : flatCond == CO_FLOOD ? (r < 45 ? 3 : 1) : 1; }
static void buildFlat(int id, unsigned seed) {
  layoutId = id; rng = seed; trash.clear(); carried = 0; spent = 0; entX = entZ = 0;
  const Layout &L = LAYOUTS[id];
  for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) {
    char c = L.rows[z][x]; cellR[x][z] = c == 'H' ? R_HALL : c == 'L' ? R_LIV : c == 'K' ? R_KIT : c == 'B' ? R_BATH : R_OUT;
    Cell &ce = cells[x][z]; ce.floor = 0; ce.furn = 0; ce.frot = 0; int r = rnd(100);
    ce.stain = !cellR[x][z] ? 0 : flatCond == CO_FIRE ? (r < 50 ? 2 : r < 60 ? 1 : 0) : flatCond == CO_FLOOD ? (r < 35 ? 3 : r < 50 ? 1 : 0)
             : flatCond == CO_BARE ? (r < 12 ? 1 : 0) : flatCond == CO_GRANNY ? (r < 30 ? 1 : 0) : (r < 45 ? 1 : 0);
  }
  memset(wallX, 0, sizeof wallX); memset(wallZ, 0, sizeof wallZ); memset(doorX, 0, sizeof doorX); memset(doorZ, 0, sizeof doorZ);
  memset(fX, 0, sizeof fX); memset(fZ, 0, sizeof fZ);
  // разные комнаты по соседству (или комната и улица) - стена
  for (int z = 0; z < MH; z++) for (int x = 0; x <= MW; x++) { int a = x > 0 ? cellR[x - 1][z] : R_OUT, b = x < MW ? cellR[x][z] : R_OUT; wallX[x][z] = a != b; }
  for (int x = 0; x < MW; x++) for (int z = 0; z <= MH; z++) { int a = z > 0 ? cellR[x][z - 1] : R_OUT, b = z < MH ? cellR[x][z] : R_OUT; wallZ[x][z] = a != b; }
  // двери: каждая отдельная комната (связная область одного типа) получает дверь из коридора
  static int comp[MW][MH]; memset(comp, -1, sizeof comp); int nc = 0;
  for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (inRoom(x, z) && cellR[x][z] != R_HALL && comp[x][z] < 0) {
    int stx[MW * MH], stz[MW * MH], sp = 0; stx[sp] = x; stz[sp] = z; sp++; comp[x][z] = nc; bool door = false;
    while (sp) { sp--; int cx = stx[sp], cz = stz[sp]; int dxs[4] = {1, -1, 0, 0}, dzs[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; d++) { int nx = cx + dxs[d], nz = cz + dzs[d]; if (!inRoom(nx, nz)) continue;
        if (cellR[nx][nz] == cellR[x][z] && comp[nx][nz] < 0) { comp[nx][nz] = nc; stx[sp] = nx; stz[sp] = nz; sp++; }
        else if (cellR[nx][nz] == R_HALL && !door) { door = true; if (dxs[d]) doorX[imax(cx, nx)][cz] = true; else doorZ[cx][imax(cz, nz)] = true; } } }
    nc++;
  }
  // вход: клетка коридора у улицы
  bool entFound = false;
  for (int z = 0; z < MH && !entFound; z++) for (int x = 0; x < MW && !entFound; x++) if (cellR[x][z] == R_HALL) {
    if (x + 1 < MW && cellR[x + 1][z] == R_OUT) { doorX[x + 1][z] = true; entX = x; entZ = z; binX = x + 2.0f; binZ = z + 0.5f; entFound = true; continue; }
    if (x > 0 && cellR[x - 1][z] == R_OUT) { doorX[x][z] = true; entX = x; entZ = z; binX = x - 1.0f; binZ = z + 0.5f; entFound = true; continue; }
    if (z > 0 && cellR[x][z - 1] == R_OUT) { doorZ[x][z] = true; entX = x; entZ = z; binX = x + 0.5f; binZ = z - 1.0f; entFound = true; continue; }
  }
  // стороны стен: в комнатах - старые обои; на улицу - иногда окна
  for (int z = 0; z < MH; z++) for (int x = 0; x <= MW; x++) if (wallX[x][z] && !doorX[x][z]) for (int s = 0; s < 2; s++) {
    int cx = s == 0 ? x - 1 : x; if (!inRoom(cx, z)) continue; fX[x][z][s].wallpaper = oldPaper();
    int other = s == 0 ? (x < MW ? cellR[x][z] : 0) : (x > 0 ? cellR[x - 1][z] : 0); if (other == R_OUT && cellR[cx][z] != R_BATH && rnd(100) < 40) fX[x][z][s].window = 1; }
  for (int x = 0; x < MW; x++) for (int z = 0; z <= MH; z++) if (wallZ[x][z] && !doorZ[x][z]) for (int s = 0; s < 2; s++) {
    int cz = s == 0 ? z - 1 : z; if (!inRoom(x, cz)) continue; fZ[x][z][s].wallpaper = oldPaper();
    int other = s == 0 ? (z < MH ? cellR[x][z] : 0) : (z > 0 ? cellR[x][z - 1] : 0); if (other == R_OUT && cellR[x][cz] != R_BATH && rnd(100) < 40) fZ[x][z][s].window = 1; }
  stainsInit = 0; for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (cells[x][z].stain) stainsInit++;
  // мусор
  int tp = flatCond == CO_FIRE ? 38 : flatCond == CO_BARE ? 34 : flatCond == CO_GRANNY ? 22 : 30;
  int nt = 0; for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (inRoom(x, z) && !(x == entX && z == entZ) && rnd(100) < tp) {
    int k = rnd(4); if (flatCond == CO_FIRE && rnd(100) < 55) k = 5; else if (flatCond == CO_BARE) k = rnd(100) < 60 ? 4 : 1; else if (flatCond == CO_FLOOD && rnd(100) < 40) k = 6;
    Trash t = {x + 0.25f + rnd(50) / 100.0f, z + 0.25f + rnd(50) / 100.0f, k, true}; trash.push_back(t); nt++; }
  // «бабушкина» квартира: старая мебель у стен, её надо вынести
  oldInit = 0;
  if (flatCond == CO_GRANNY) for (int z = 0; z < MH && oldInit < 6; z++) for (int x = 0; x < MW && oldInit < 6; x++) {
    if (!inRoom(x, z) || cellR[x][z] != R_LIV || trashOn(x, z) || doorX[x][z] || doorX[x + 1][z] || doorZ[x][z] || doorZ[x][z + 1]) continue;
    int rot = -1; if (wallZ[x][z]) rot = 0; else if (wallZ[x][z + 1]) rot = 2; else if (wallX[x][z]) rot = 1; else if (wallX[x + 1][z]) rot = 3;
    if (rot < 0 || rnd(100) >= 30) continue;
    static const int OLDF[4] = {37, 38, 39, 40}; cells[x][z].furn = (unsigned char)OLDF[rnd(4)]; cells[x][z].frot = (unsigned char)rot; oldInit++;
  }
}
// ---- прогресс ремонта ----
struct Prog { int oldLeft, oldAll, trashLeft, trashAll, stains, stainsAll, paper, paperAll, unputty, unpainted, wallsAll, floorsOld, floorsAll, furnOk, furnNeed, furnValue; float k; };
static Prog progress() {
  Prog p; memset(&p, 0, sizeof p); int fcnt[NFURN]; memset(fcnt, 0, sizeof fcnt);
  for (size_t i = 0; i < trash.size(); i++) { p.trashAll++; if (trash[i].alive) p.trashLeft++; }
  p.trashLeft += carried;
  for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (inRoom(x, z)) { p.floorsAll++; if (!cells[x][z].floor) p.floorsOld++; if (cells[x][z].stain) p.stains++; p.stainsAll++; if (cells[x][z].furn) { int it = cells[x][z].furn; if (FURN[it].old) p.oldLeft++; else if (fcnt[it]++ < 4) p.furnValue += FURN[it].value; } }
  for (int z = 0; z < MH; z++) for (int x = 0; x <= MW; x++) for (int s = 0; s < 2; s++) { int cx = s == 0 ? x - 1 : x; if (!wallX[x][z] || doorX[x][z] || !inRoom(cx, z)) continue; p.wallsAll++; const Face &f = fX[x][z][s]; if (f.wallpaper) p.paper++; if (!f.putty) p.unputty++; if (!f.paint && !f.paper) p.unpainted++; }
  for (int x = 0; x < MW; x++) for (int z = 0; z <= MH; z++) for (int s = 0; s < 2; s++) { int cz = s == 0 ? z - 1 : z; if (!wallZ[x][z] || doorZ[x][z] || !inRoom(x, cz)) continue; p.wallsAll++; const Face &f = fZ[x][z][s]; if (f.wallpaper) p.paper++; if (!f.putty) p.unputty++; if (!f.paint && !f.paper) p.unpainted++; }
  // нужная мебель: в комнате диван или кровать, на кухне холодильник и плита, в ванной ванна, раковина, унитаз
  bool has[NFURN] = {false}; for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) { int it = cells[x][z].furn; if (it && !FURN[it].old && (FURN[it].room == 0 || FURN[it].room == cellR[x][z])) has[furnShape(it)] = true; }
  p.furnNeed = 6; p.furnOk = (has[FU_SOFA] || has[FU_BED]) + has[FU_FRIDGE] + has[FU_STOVE] + has[FU_BATH] + has[FU_SINK] + has[FU_WC];
  p.oldAll = imax(oldInit, p.oldLeft);
  int tl = p.trashLeft + p.oldLeft, ta = p.trashAll + p.oldAll;   // старая мебель считается как мусор
  p.paperAll = p.wallsAll; p.k = 0.15f * (ta ? 1.0f - (float)tl / ta : 1) + 0.10f * clampf(1.0f - (float)p.stains / imax(1, stainsInit), 0.0f, 1.0f)
    + 0.10f * (1.0f - (float)p.paper / imax(1, p.wallsAll)) + 0.08f * (1.0f - (float)p.unputty / imax(1, p.wallsAll)) + 0.22f * (1.0f - (float)p.unpainted / imax(1, p.wallsAll)) + 0.20f * (1.0f - (float)p.floorsOld / imax(1, p.floorsAll)) + 0.15f * p.furnOk / p.furnNeed;
  return p;
}
// ---- стиль: сколько отделки и мебели в одном стиле ----
struct StyleInfo { int best; float unity; int cnt[NSTYLE]; int total; };
static StyleInfo styleInfo() {
  StyleInfo si; memset(&si, 0, sizeof si);
  int pot = 0;                                   // сколько всего можно отделать: пока сделано мало, стиль не засчитывается
  #define FACE_ST(f) { const Face &ff = (f); pot++; if (ff.paint || ff.paper) { si.total++; si.cnt[ff.paper ? PAPERS[imin(ff.paper, NPAPER - 1)].style : PAINTS[imin(ff.paint, NPAINT - 1)]]++; } }
  for (int z = 0; z < MH; z++) for (int x = 0; x <= MW; x++) for (int q = 0; q < 2; q++) { int cx = q == 0 ? x - 1 : x; if (wallX[x][z] && !doorX[x][z] && inRoom(cx, z)) FACE_ST(fX[x][z][q]); }
  for (int x = 0; x < MW; x++) for (int z = 0; z <= MH; z++) for (int q = 0; q < 2; q++) { int cz = q == 0 ? z - 1 : z; if (wallZ[x][z] && !doorZ[x][z] && inRoom(x, cz)) FACE_ST(fZ[x][z][q]); }
  #undef FACE_ST
  for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (inRoom(x, z)) {
    const Cell &c = cells[x][z];
    if (!tiledRoom(cellR[x][z])) pot++;
    if (c.floor && !tiledRoom(cellR[x][z])) { si.total++; si.cnt[FLOORS[imin(c.floor, NFLOOR - 1)].style]++; }   // плитка в ванной и на кухне в стиль не считается
    if (c.furn && !FURN[c.furn].old) { si.total += 3; si.cnt[FURN[c.furn].style] += 3; }
  }
  si.best = 0; for (int i = 1; i < NSTYLE; i++) if (si.cnt[i] > (si.best ? si.cnt[si.best] : 0)) si.best = i;
  si.unity = si.best && si.total ? (float)si.cnt[si.best] / imax(si.total, pot * 6 / 10) : 0;
  return si;
}
static float styleBonus(const StyleInfo &si) { return si.unity > 0.45f ? fminv(0.1f, (si.unity - 0.45f) * 0.25f) : 0; }   // до +10% за единый стиль
static int salePrice() {
  Prog p = progress(); float m = CONDK[flatCond], base = flatBuy / m, lo = 0.88f * m, g = (0.35f * p.k * p.k + 0.07f * p.k) / 0.42f;   // после пожара дёшево купил - но и дёшево продашь, пока не отремонтируешь
  StyleInfo si = styleInfo();
  return (int)(base * (lo + (1.30f - lo) * g) * (1.0f + styleBonus(si) * p.k)) + p.furnValue * 8 / 10;
}
// ---- покупатели с пожеланиями ----
enum { BU_FAMILY, BU_STUDENT, BU_BOSS, BU_GRANNY, BU_IT, BU_COUPLE, NBUYER };
static const char *BUYERN[NBUYER] = {"СЕМЬЯ С ДЕТЬМИ", "СТУДЕНТ", "БИЗНЕСМЕН", "ПЕНСИОНЕРКА", "АЙТИШНИК", "МОЛОДАЯ ПАРА"};
static const float BUYERK[NBUYER] = {1.0f, 0.93f, 1.03f, 0.98f, 1.0f, 1.0f};
enum { W_SHAPE, W_STYLE, W_STYLE2, W_ANYSTYLE, W_PREMFLOOR, W_DONE, W_NODARK };
struct Wish { int kind, a, b; float bonus; const char *text; };
static const Wish WISHES[NBUYER][3] = {
  {{W_SHAPE, FU_CRIB, 0, 0.06f, "ДЕТСКАЯ КРОВАТКА"}, {W_SHAPE, FU_WASH, 0, 0.03f, "СТИРАЛЬНАЯ МАШИНА"}, {W_DONE, 90, 0, 0.04f, "РЕМОНТ 90% И ВЫШЕ"}},
  {{W_SHAPE, FU_DESK, 0, 0.04f, "РАБОЧИЙ СТОЛ"}, {W_SHAPE, FU_BED, 0, 0.03f, "КРОВАТЬ"}, {W_DONE, 60, 0, 0.02f, "РЕМОНТ 60% И ВЫШЕ"}},
  {{W_ANYSTYLE, 70, 0, 0.08f, "ЕДИНЫЙ СТИЛЬ 70%"}, {W_PREMFLOOR, 50, 0, 0.05f, "ДОРОГОЙ ПОЛ"}, {W_SHAPE, FU_TV, 0, 0.02f, "ТЕЛЕВИЗОР"}},
  {{W_STYLE, ST_CLASSIC, 0, 0.07f, "СТИЛЬ КЛАССИКА"}, {W_SHAPE, FU_FLOWER, 0, 0.02f, "ЦВЕТОК"}, {W_SHAPE, FU_RUG, 0, 0.03f, "КОВЁР"}},
  {{W_STYLE2, ST_LOFT, ST_MODERN, 0.06f, "ЛОФТ ИЛИ МОДЕРН"}, {W_SHAPE, FU_DESK, 0, 0.05f, "РАБОЧИЙ СТОЛ С ПК"}, {W_SHAPE, FU_TV, 0, 0.02f, "ТЕЛЕВИЗОР"}},
  {{W_STYLE, ST_SCANDI, 0, 0.06f, "СТИЛЬ СКАНДИ"}, {W_SHAPE, FU_FLOWER, 0, 0.02f, "ЦВЕТОК"}, {W_SHAPE, FU_BED, 0, 0.03f, "КРОВАТЬ"}} };
static int buyers[3];
static void genBuyers() {                     // три разных покупателя; в центре чаще богатые, на окраине - студенты и семьи
  unsigned r = flatSeed * 2246822519u + 777u; int w[NBUYER] = {3, 3, 2, 2, 2, 2};
  if (flatDist == DI_CENTER) { w[BU_BOSS] = 5; w[BU_IT] = 3; w[BU_STUDENT] = 1; }
  if (flatDist == DI_EDGE) { w[BU_STUDENT] = 5; w[BU_FAMILY] = 4; w[BU_BOSS] = 1; }
  for (int i = 0; i < 3; i++) for (;;) { r = r * 1103515245u + 12345u; int sum = 0; for (int k = 0; k < NBUYER; k++) sum += w[k]; int v = (int)((r >> 16) % (unsigned)sum), t = 0;
    while (v >= w[t]) { v -= w[t]; t++; } bool dup = false; for (int k = 0; k < i; k++) if (buyers[k] == t) dup = true; if (!dup) { buyers[i] = t; break; } }
}
static bool hasShape(int sh) { for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) { int it = cells[x][z].furn; if (it && !FURN[it].old && furnShape(it) == sh) return true; } return false; }
static bool wishOk(const Wish &w, const Prog &p, const StyleInfo &si) {
  switch (w.kind) {
    case W_SHAPE: return hasShape(w.a);
    case W_STYLE: return si.best == w.a && si.unity * 100 >= 50;
    case W_STYLE2: return (si.best == w.a || si.best == w.b) && si.unity * 100 >= 50;
    case W_ANYSTYLE: return si.unity * 100 >= w.a;
    case W_PREMFLOOR: { int n = 0, all = 0; for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (inRoom(x, z) && !tiledRoom(cellR[x][z])) { all++; if (cells[x][z].floor && FLOORS[imin(cells[x][z].floor, NFLOOR - 1)].prem) n++; } return all && n * 100 >= all * w.a; }
    case W_DONE: return p.k * 100 >= w.a;
  }
  return true;
}
static int buyerPrice(int b) {
  Prog p = progress(); StyleInfo si = styleInfo(); float k = BUYERK[b];
  for (int i = 0; i < 3; i++) { const Wish &w = WISHES[b][i]; if (!w.text[0]) continue; if (wishOk(w, p, si)) k += w.bonus; else if (b != BU_STUDENT) k -= 0.02f; }
  return (int)((long long)salePrice() * (int)(k * 1000) / 1000 / 1000 * 1000);
}

// ---------- игрок, инструменты, прицел ----------
enum { T_HAND, T_MOP, T_SCRAPER, T_PUTTY, T_ROLLER, T_PAPER, T_FLOOR, T_FURN, NTOOL };
static const char *TOOLN[NTOOL] = {"РУКИ", "ШВАБРА", "ШПАТЕЛЬ", "ШПАКЛЁВКА", "ВАЛИК", "ОБОИ", "ПОЛ", "МЕБЕЛЬ"};
static float px = 2.5f, pz = 4.5f, yaw = 0, pitch = -0.2f;
static int tool = T_HAND, paintSel = 1, furnSel = 1, paperSel = 1, floorSel = 1;
static float workT = 0, workBob = 0; static bool working = false;
enum { H_NONE, H_TRASH, H_FLOOR, H_WALLX, H_WALLZ };
struct Hit { int type, x, z, s, idx; float hx, hz; };
static Hit aim;
static int cellOf(float v) { return (int)floorf(v); }
static bool edgeOpen(int x0, int z0, int x1, int z1) {   // можно ли пройти между соседними клетками
  if (x1 != x0) { int ex = imax(x0, x1); return !wallX[ex][z0] || doorX[ex][z0]; }
  int ez = imax(z0, z1); return !wallZ[x0][ez] || doorZ[x0][ez];
}
static bool canStand(float x, float z) {
  int cx = cellOf(px), cz = cellOf(pz);
  const float R = 0.26f;                                   // «толщина» игрока: к стене ближе не подойти
  float off[8][2] = {{-R, -R}, {R, -R}, {-R, R}, {R, R}, {0, -R}, {0, R}, {-R, 0}, {R, 0}};
  for (int i = 0; i < 8; i++) {
    { int fx = cellOf(x + off[i][0]), fz = cellOf(z + off[i][1]);   // мебель - твёрдая
      if (fx >= 0 && fz >= 0 && fx < MW && fz < MH && cells[fx][fz].furn && furnSolid(cells[fx][fz].furn) && !(fx == cx && fz == cz)) {
        float lx = x + off[i][0] - fx, lz = z + off[i][1] - fz; if (lx > 0.06f && lx < 0.94f && lz > 0.06f && lz < 0.94f) return false; } }
    int sx = cellOf(x + off[i][0]), sz = cellOf(z + off[i][1]);
    if (!inRoom(sx, sz)) return false;
    if (sx == cx && sz == cz) continue;
    if (abs(sx - cx) + abs(sz - cz) == 1) { if (!edgeOpen(cx, cz, sx, sz)) return false; }
    else if (abs(sx - cx) <= 1 && abs(sz - cz) <= 1) { if (!edgeOpen(cx, cz, sx, cz) || !edgeOpen(sx, cz, sx, sz) || !edgeOpen(cx, cz, cx, sz) || !edgeOpen(cx, sz, sx, sz)) return false; }
    else return false;
  }
  return true;
}
static void tryMove(float dx, float dz) {
  if (canStand(px + dx, pz)) px += dx;
  if (canStand(px, pz + dz)) pz += dz;
}
static V3 eyePos() { return v3(px, 1.62f, pz); }
static V3 lookDir() { return v3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)); }
static Hit castAim() {
  Hit h; memset(&h, 0, sizeof h); h.type = H_NONE;
  V3 e = eyePos(), d = lookDir(); int pcx = cellOf(e.x), pcz = cellOf(e.z);
  for (float t = 0.05f; t < 3.2f; t += 0.03f) {
    V3 p = e + d * t;
    for (size_t i = 0; i < trash.size(); i++) if (trash[i].alive) { float dx = p.x - trash[i].x, dz = p.z - trash[i].z; if (dx * dx + dz * dz < 0.06f && p.y < 0.45f) { h.type = H_TRASH; h.idx = (int)i; return h; } }
    int cx = cellOf(p.x), cz = cellOf(p.z);
    if (cx != pcx || cz != pcz) {                       // пересекли грань клетки: стена?
      if (cx != pcx) { int ex = imax(cx, pcx); if (ex >= 0 && ex <= MW && pcz >= 0 && pcz < MH && wallX[ex][pcz] && !doorX[ex][pcz]) { h.type = H_WALLX; h.x = ex; h.z = pcz; h.s = cx > pcx ? 0 : 1; h.hx = p.x; h.hz = p.z; return h; } }
      if (cz != pcz) { int ez = imax(cz, pcz); if (ez >= 0 && ez <= MH && cx >= 0 && cx < MW && wallZ[cx][ez] && !doorZ[cx][ez]) { h.type = H_WALLZ; h.x = cx; h.z = ez; h.s = cz > pcz ? 0 : 1; h.hx = p.x; h.hz = p.z; return h; } }
      pcx = cx; pcz = cz;
    }
    if (p.y <= 0.0f) { if (inRoom(cx, cz)) { h.type = H_FLOOR; h.x = cx; h.z = cz; h.hx = p.x; h.hz = p.z; } return h; }
    if (p.y > 2.6f) return h;
  }
  return h;
}
static Face *aimFace() { if (aim.type == H_WALLX) return &fX[aim.x][aim.z][aim.s]; if (aim.type == H_WALLZ) return &fZ[aim.x][aim.z][aim.s]; return NULL; }
static bool trashOn(int x, int z) { for (size_t i = 0; i < trash.size(); i++) if (trash[i].alive && cellOf(trash[i].x) == x && cellOf(trash[i].z) == z) return true; return false; }
// что сделает кнопка «ДЕЛАТЬ» сейчас: подпись, время, можно ли
static const char *actionInfo(float &need, bool &ok, char *why) {
  need = 0.6f; ok = false; why[0] = 0; Face *f = aimFace();
  switch (tool) {
    case T_HAND:
      if (aim.type == H_TRASH) { if (carried >= 4) { strcpy(why, "РУКИ ЗАНЯТЫ: ОТНЕСИ МУСОР К ВХОДНОЙ ДВЕРИ"); return "ВЗЯТЬ"; } ok = true; need = 0.35f; return "ВЗЯТЬ МУСОР"; }
      if (aim.type == H_FLOOR && cells[aim.x][aim.z].furn) { ok = true; need = FURN[cells[aim.x][aim.z].furn].old ? 1.2f : 0.5f; return FURN[cells[aim.x][aim.z].furn].old ? "ВЫНЕСТИ СТАРЬЁ" : "УБРАТЬ МЕБЕЛЬ"; }
      strcpy(why, "НАВЕДИ НА МУСОР"); return "ВЗЯТЬ";
    case T_MOP:
      if (aim.type == H_FLOOR && cells[aim.x][aim.z].stain) { int st = cells[aim.x][aim.z].stain; ok = true; need = st == 2 ? 1.4f : st == 3 ? 0.7f : 0.9f; return st == 2 ? "ОТМЫТЬ ГАРЬ" : st == 3 ? "СОБРАТЬ ВОДУ" : "ОТМЫТЬ"; }
      strcpy(why, "НАВЕДИ НА ГРЯЗНОЕ ПЯТНО НА ПОЛУ"); return "МЫТЬ";
    case T_SCRAPER:
      if (f && f->wallpaper) { ok = true; need = f->wallpaper >= 2 ? 1.5f : 1.1f; return f->wallpaper == 2 ? "СОДРАТЬ ГАРЬ" : f->wallpaper == 3 ? "СОДРАТЬ ПЛЕСЕНЬ" : "СОДРАТЬ ОБОИ"; }
      strcpy(why, f ? "ЗДЕСЬ ОБОЕВ УЖЕ НЕТ" : "НАВЕДИ НА СТЕНУ СО СТАРЫМИ ОБОЯМИ"); return "СОДРАТЬ";
    case T_PUTTY:
      if (f && f->wallpaper) { strcpy(why, "СНАЧАЛА СОДРИ СТАРЫЕ ОБОИ ШПАТЕЛЕМ"); return "ШПАКЛЕВАТЬ"; }
      if (f && !f->putty) { if (money < COST_PUTTY) { strcpy(why, "НЕ ХВАТАЕТ ДЕНЕГ"); return "ШПАКЛЕВАТЬ"; } ok = true; need = 1.2f; return "ЗАШПАКЛЕВАТЬ"; }
      strcpy(why, f ? "СТЕНА УЖЕ РОВНАЯ" : "НАВЕДИ НА СТЕНУ С ТРЕЩИНАМИ"); return "ШПАКЛЕВАТЬ";
    case T_PAPER:
      if (f && f->wallpaper) { strcpy(why, "СНАЧАЛА СОДРИ СТАРЫЕ ОБОИ ШПАТЕЛЕМ"); return "КЛЕИТЬ"; }
      if (f && !f->putty) { strcpy(why, "СНАЧАЛА ЗАШПАКЛЮЙ СТЕНУ, ИНАЧЕ ОБОИ ЛЯГУТ КРИВО"); return "КЛЕИТЬ"; }
      if (f && f->paper != paperSel) { if (money < COST_PAPER) { strcpy(why, "НЕ ХВАТАЕТ ДЕНЕГ"); return "КЛЕИТЬ"; } ok = true; need = 1.4f; return "ПОКЛЕИТЬ ОБОИ"; }
      strcpy(why, f ? "ЗДЕСЬ УЖЕ ЭТИ ОБОИ" : "НАВЕДИ НА СТЕНУ"); return "КЛЕИТЬ";
    case T_ROLLER:
      if (f && f->wallpaper) { strcpy(why, "СНАЧАЛА СОДРИ СТАРЫЕ ОБОИ ШПАТЕЛЕМ"); return "КРАСИТЬ"; }
      if (f && !f->putty) { strcpy(why, "СНАЧАЛА ЗАШПАКЛЮЙ ТРЕЩИНЫ"); return "КРАСИТЬ"; }
      if (f && (f->paint != paintSel || f->paper)) { if (money < COST_PAINT) { strcpy(why, "НЕ ХВАТАЕТ ДЕНЕГ"); return "КРАСИТЬ"; } ok = true; need = 0.8f; return "ПОКРАСИТЬ"; }
      strcpy(why, f ? "УЖЕ ЭТОГО ЦВЕТА" : "НАВЕДИ НА СТЕНУ"); return "КРАСИТЬ";
    case T_FLOOR:
      if (aim.type == H_FLOOR) { Cell &c = cells[aim.x][aim.z];
        if (c.floor == floorSel) { strcpy(why, "ТАКОЙ ПОЛ УЖЕ ЕСТЬ"); return "ПОЛ"; }
        if (c.stain || trashOn(aim.x, aim.z)) { strcpy(why, "СНАЧАЛА УБЕРИ МУСОР И ОТМОЙ ПЯТНО"); return "ПОЛ"; }
        if (c.furn) { strcpy(why, "МЕШАЕТ МЕБЕЛЬ"); return "ПОЛ"; }
        if (money < FLOORS[floorSel].cost) { strcpy(why, "НЕ ХВАТАЕТ ДЕНЕГ"); return "ПОЛ"; }
        static char fb[64]; snprintf(fb, sizeof fb, "%s: %s", c.floor ? "ПЕРЕСТЕЛИТЬ" : "ПОЛОЖИТЬ", FLOORS[floorSel].name);
        ok = true; need = 1.0f; return fb; }
      strcpy(why, "НАВЕДИ НА ПОЛ"); return "ПОЛ";
    case T_FURN:
      if (aim.type == H_FLOOR) { Cell &c = cells[aim.x][aim.z];
        if (c.furn || trashOn(aim.x, aim.z)) { strcpy(why, "МЕСТО ЗАНЯТО"); return "ПОСТАВИТЬ"; }
        if (aim.x == cellOf(px) && aim.z == cellOf(pz)) { strcpy(why, "ОТОЙДИ: ТЫ СТОИШЬ НА ЭТОМ МЕСТЕ"); return "ПОСТАВИТЬ"; }
        if (money < FURN[furnSel].price) { strcpy(why, "НЕ ХВАТАЕТ ДЕНЕГ"); return "ПОСТАВИТЬ"; }
        ok = true; need = 0.3f; return "ПОСТАВИТЬ"; }
      strcpy(why, "НАВЕДИ НА ПОЛ"); return "ПОСТАВИТЬ";
  }
  return "";
}
static void doAction() {
  Face *f = aimFace(); char b[96];
  switch (tool) {
    case T_HAND:
      if (aim.type == H_TRASH) { trash[aim.idx].alive = false; carried++; snprintf(b, sizeof b, "В РУКАХ МУСОРА: %d/4 - НЕСИ К ВХОДНОЙ ДВЕРИ", carried); say(b); }
      else if (aim.type == H_FLOOR && cells[aim.x][aim.z].furn && FURN[cells[aim.x][aim.z].furn].old) { int it = cells[aim.x][aim.z].furn; money += FURN[it].price; spent -= FURN[it].price; cells[aim.x][aim.z].furn = 0;
        snprintf(b, sizeof b, "%s ВЫНЕСЕН: БАРАХОЛКА +%d Р", FURN[it].name, FURN[it].price); say(b); }
      else if (aim.type == H_FLOOR && cells[aim.x][aim.z].furn) { int it = cells[aim.x][aim.z].furn; money += FURN[it].price / 2; spent -= FURN[it].price / 2; cells[aim.x][aim.z].furn = 0; say("МЕБЕЛЬ УБРАНА (ВЕРНУЛИ ПОЛОВИНУ ЦЕНЫ)"); }
      break;
    case T_MOP: cells[aim.x][aim.z].stain = 0; break;
    case T_SCRAPER: f->wallpaper = 0; break;
    case T_PUTTY: f->putty = 1; money -= COST_PUTTY; spent += COST_PUTTY; break;
    case T_PAPER: f->paper = (unsigned char)paperSel; f->paint = 0; money -= COST_PAPER; spent += COST_PAPER; break;
    case T_ROLLER: f->paint = (unsigned char)paintSel; f->paper = 0; money -= COST_PAINT; spent += COST_PAINT; break;
    case T_FLOOR: { int cost = FLOORS[floorSel].cost; cells[aim.x][aim.z].floor = (unsigned char)floorSel; money -= cost; spent += cost; } break;
    case T_FURN: { Cell &c = cells[aim.x][aim.z]; c.furn = (unsigned char)furnSel; c.frot = (unsigned char)(((int)roundf(yaw / 1.5708f) % 4 + 6) % 4); money -= FURN[furnSel].price; spent += FURN[furnSel].price;
      if (FURN[furnSel].room && FURN[furnSel].room != cellR[aim.x][aim.z]) { snprintf(b, sizeof b, "%s ЛУЧШЕ СМОТРИТСЯ: %s", FURN[furnSel].name, ROOMN[FURN[furnSel].room]); say(b); } } break;
  }
}

// ---------- 3D: стены, полы, мусор, мебель, руки ----------
static float camTanX = 1, camTanY = 1;
static bool seen(V3 c, float r) {                 // объект попадает в кадр? (то, что сзади и сбоку, не рисуем)
  V3 v = toView(c);
  if (v.z < -r) return false;
  float zz = fmaxv(v.z, 0.05f);
  if (fabsf(v.x) - r > zz * camTanX + r * 0.5f) return false;
  if (fabsf(v.y) - r > zz * camTanY + r * 0.5f) return false;
  return true;
}
static float camDist2(V3 c) { V3 d = c - camEye; return dot(d, d); }
// двери: угол открытия каждой (0 закрыта, ±1 открыта в одну или другую сторону)
static float angX[MW + 1][MH], angZ[MW][MH + 1];
static void doorsUpdate(float dt) {
  for (int z = 0; z < MH; z++) for (int x = 0; x <= MW; x++) if (doorX[x][z]) {
    float dx = px - x, dz = pz - (z + 0.5f); bool near_ = dx * dx + dz * dz < 2.6f;
    float tgt = near_ ? (fabsf(angX[x][z]) > 0.05f ? (angX[x][z] > 0 ? 1.0f : -1.0f) : (dx < 0 ? 1.0f : -1.0f)) : 0.0f;   // открывается от игрока
    angX[x][z] += fmaxv(-dt * 2.5f, fminv(dt * 2.5f, tgt - angX[x][z]));
  }
  for (int x = 0; x < MW; x++) for (int z = 0; z <= MH; z++) if (doorZ[x][z]) {
    float dx = px - (x + 0.5f), dz = pz - z; bool near_ = dx * dx + dz * dz < 2.6f;
    float tgt = near_ ? (fabsf(angZ[x][z]) > 0.05f ? (angZ[x][z] > 0 ? 1.0f : -1.0f) : (dz < 0 ? 1.0f : -1.0f)) : 0.0f;
    angZ[x][z] += fmaxv(-dt * 2.5f, fminv(dt * 2.5f, tgt - angZ[x][z]));
  }
}
static void doorLeaf(V3 hinge, float baseYaw, float k, bool ent) {   // полотно на петле: k от -1 до 1
  float a = baseYaw + k * 1.5f * 1.0f;
  V3 dir = v3(sinf(a), 0, cosf(a));
  u32 lf = ent ? RGB(120, 70, 45) : RGB(215, 185, 140);
  drawBox(hinge + dir * 0.44f + v3(0, 1.0f, 0), v3(0.02f, 1.0f, 0.44f), a, lf, true);
  V3 side = v3(cosf(a), 0, -sinf(a));
  for (int s2 = -1; s2 <= 1; s2 += 2) drawBox(hinge + dir * 0.78f + side * (0.035f * s2) + v3(0, 1.0f, 0), v3(0.01f, 0.012f, 0.04f), a, RGB(200, 180, 90), true);   // ручки
  drawBox(hinge + dir * 0.44f + v3(0, 1.45f, 0) + side * 0.021f, v3(0.002f, 0.3f, 0.3f), a, mixc(lf, RGB(0, 0, 0), 0.12f), false);   // филёнка
}
static u32 oldFloorCol(int r) {
  if (flatCond == CO_BARE) return RGB(150, 150, 147);                                // бетонная стяжка
  u32 c = r == R_BATH ? RGB(120, 120, 105) : r == R_KIT ? RGB(140, 120, 90) : flatHouse == HO_STALIN ? RGB(128, 90, 56) : RGB(120, 95, 70);
  if (flatCond == CO_FIRE) c = mixc(c, RGB(30, 26, 22), 0.55f);
  if (flatCond == CO_FLOOD) c = mixc(c, RGB(150, 138, 115), 0.3f);
  return c;
}
static u32 outerWallCol() { return flatHouse == HO_KHRU ? RGB(178, 112, 88) : flatHouse == HO_PANEL ? RGB(175, 172, 165) : flatHouse == HO_STALIN ? RGB(222, 198, 145) : RGB(200, 205, 212); }
static u32 newFloorCol(int type, int x, int z) { const FloorT &f = FLOORS[imax(1, imin(NFLOOR - 1, type))]; return f.tile ? ((x + z) & 1 ? f.a : f.b) : ((x * 3 + z) % 2 ? f.a : f.b); }
static void motif(V3 c, V3 n, bool alongZ, float o, float y, float k, float hw, float hh, u32 col) {
  float l = fmaxv(o - hw, -0.5f), r = fminv(o + hw, 0.5f); if (r <= l + 0.001f) return;   // ничего не вылезает за край полосы
  float mo = (l + r) * 0.5f, mw = (r - l) * 0.5f;
  drawBox(c + n * k + v3(0, y, 0) + (alongZ ? v3(0, 0, mo) : v3(mo, 0, 0)), alongZ ? v3(0.001f, hh, mw) : v3(mw, hh, 0.001f), 0, col, false);
}
static void wallSide(V3 c, bool alongZ, const Face &f, float sgn, int room, int seed) {   // одна сторона стены (полоска 1 м)
  if (!seen(c, 1.4f)) return;
  bool near_ = camDist2(c) < 4.5f * 4.5f;                         // узоры и мелкие детали - только рядом
  V3 hs = alongZ ? v3(0.04f, 1.3f, 0.5f) : v3(0.5f, 1.3f, 0.04f);
  V3 n = alongZ ? v3(sgn * 0.041f, 0, 0) : v3(0, 0, sgn * 0.041f);
  #define AT(o, y, k) (c + n * (k) + v3(0, (y), 0) + (alongZ ? v3(0, 0, (o)) : v3((o), 0, 0)))
  #define HS(l, h, d) (alongZ ? v3((d), (h), (l)) : v3((l), (h), (d)))
  // где на стене окно: там узор не рисуем
  #define FREE(o, y) (!f.window || fabsf(o) > 0.4f || (y) < -0.42f || (y) > 0.86f)
  const Paper &pp = PAPERS[f.paper < NPAPER ? f.paper : 0];
  u32 base = f.paint ? PAINT[f.paint] : f.paper ? pp.a : f.wallpaper == 2 ? RGB(92, 80, 68) : f.wallpaper == 3 ? RGB(192, 182, 140) : f.wallpaper ? RGB(206, 190, 142)
           : f.putty ? RGB(234, 232, 226) : flatCond == CO_BARE ? RGB(170, 168, 162) : RGB(158, 156, 150);
  drawBox(c, hs, 0, base, true);
  unsigned h = (unsigned)seed * 2654435761u;
  if (near_ && f.wallpaper == 2) {                                // обгоревшие обои: копоть снизу вверх, прогары до бетона
    for (int i = 0; i < 6; i++) { float o = -0.42f + i * 0.17f + ((h >> (i * 3)) % 5) * 0.012f, hh = 0.35f + ((h >> (i * 2 + 1)) % 6) * 0.13f;
      drawBox(AT(o, -1.28f + hh, 1.03f), HS(0.05f + (i % 2) * 0.03f, hh, 0.001f), 0, RGB(34, 30, 27), false);
      motif(c, n, alongZ, o, -1.28f + hh * 2, 1.04f, 0.09f, 0.07f, RGB(40, 36, 32)); }
    drawBox(AT(0, 1.15f, 1.04f), HS(0.5f, 0.15f, 0.001f), 0, RGB(30, 27, 25), false);                 // чёрный верх под потолком
    for (int i = 0; i < 2; i++) { float o = ((h >> (5 + i * 4)) % 7) * 0.12f - 0.36f, y = -0.6f + i * 0.9f;
      if (FREE(o, y) && (h >> (9 + i)) % 3) { motif(c, n, alongZ, o, y, 1.05f, 0.08f, 0.06f, RGB(110, 72, 40)); motif(c, n, alongZ, o + 0.03f, y + 0.04f, 1.05f, 0.05f, 0.05f, RGB(110, 72, 40));
        motif(c, n, alongZ, o, y, 1.06f, 0.06f, 0.04f, RGB(140, 138, 132)); motif(c, n, alongZ, o + 0.03f, y + 0.04f, 1.06f, 0.03f, 0.035f, RGB(140, 138, 132)); } }   // прогар: бетон с рыжей каймой
  }
  else if (near_ && f.wallpaper) {                                // старые обои: дамаск, швы рулонов, пожелтение, подтёки, оторванные куски
    u32 m1 = RGB(178, 160, 112), m2 = RGB(222, 208, 165);
    for (int r = 0; r < 7; r++) for (int q = 0; q < 4; q++) {
      float o = (r % 2) ? -0.5f + q * 0.333f : -0.333f + q * 0.333f, y = -1.05f + r * 0.36f; if (!(r % 2) && q == 3) continue; if (!FREE(o, y)) continue;
      motif(c, n, alongZ, o, y, 1.03f, 0.05f, 0.05f, m1);
      motif(c, n, alongZ, o, y + 0.07f, 1.04f, 0.022f, 0.025f, m1); motif(c, n, alongZ, o, y - 0.07f, 1.04f, 0.022f, 0.025f, m1);
      motif(c, n, alongZ, o, y, 1.05f, 0.02f, 0.02f, m2);
    }
    for (int i = -1; i <= 1; i += 2) drawBox(AT(i * 0.25f, 0, 1.06f), HS(0.003f, 1.28f, 0.001f), 0, RGB(160, 140, 95), false);   // швы рулонов
    drawBox(AT(0, 1.12f, 1.07f), HS(0.5f, 0.16f, 0.001f), 0, RGB(190, 168, 112), false);                                       // пожелтевший верх
    if (h % 3 == 0) drawBox(AT(-0.2f, 0.75f, 1.08f), HS(0.12f, 0.28f, 0.001f), 0, RGB(170, 140, 95), false);                  // подтёк
    if (h % 2 == 0) {                                               // оторванный кусок: виден бетон
      float o = ((h >> 4) % 5) * 0.15f - 0.3f, y = -0.75f + ((h >> 8) % 3) * 0.45f;
      if (FREE(o, y)) { motif(c, n, alongZ, o, y, 1.09f, 0.11f, 0.13f, RGB(150, 148, 142)); motif(c, n, alongZ, o + 0.09f, y + 0.1f, 1.1f, 0.04f, 0.06f, RGB(150, 148, 142));
        motif(c, n, alongZ, o - 0.1f, y - 0.11f, 1.11f, 0.03f, 0.05f, RGB(225, 212, 170)); }   // загнутый край
    }
    if (f.wallpaper == 3) {                                         // после потопа: разводы и чёрно-зелёная плесень снизу и под потолком
      drawBox(AT(0, -0.95f, 1.12f), HS(0.5f, 0.33f, 0.001f), 0, RGB(165, 150, 110), false);
      drawBox(AT(0, -0.62f, 1.125f), HS(0.5f, 0.012f, 0.001f), 0, RGB(130, 112, 80), false);   // граница воды
      for (int i = 0; i < 7; i++) { float o = ((h >> (i * 3)) % 9) * 0.11f - 0.44f, y = i < 4 ? -1.15f + ((h >> (i + 7)) % 4) * 0.12f : 1.1f - ((h >> (i + 3)) % 3) * 0.1f;
        motif(c, n, alongZ, o, y, 1.13f, 0.07f + (i % 3) * 0.02f, 0.06f, RGB(58, 74, 50)); motif(c, n, alongZ, o + 0.03f, y + 0.02f, 1.14f, 0.035f, 0.03f, RGB(32, 40, 30)); }
    }
    if (flatCond == CO_GRANNY && f.wallpaper == 1 && !f.window && room == R_LIV && seed % 9 == 4) {   // ковёр на стене
      drawBox(AT(0, 0.15f, 1.15f), HS(0.4f, 0.55f, 0.003f), 0, RGB(130, 30, 38), false);
      drawBox(AT(0, 0.15f, 1.18f), HS(0.33f, 0.47f, 0.002f), 0, RGB(175, 60, 50), false);
      drawBox(AT(0, 0.15f, 1.21f), HS(0.17f, 0.25f, 0.002f), 0, RGB(40, 50, 100), false);
      drawBox(AT(0, 0.15f, 1.24f), HS(0.08f, 0.12f, 0.002f), 0, RGB(225, 190, 110), false);
      for (int i = 0; i < 4; i++) drawBox(AT((i % 2 ? 1 : -1) * 0.24f, 0.15f + (i / 2 ? 1 : -1) * 0.36f, 1.22f), HS(0.05f, 0.06f, 0.002f), 0, RGB(225, 190, 110), false);
    }
  }
  else if (near_ && flatCond == CO_BARE && !f.putty && !f.paint && !f.paper) {   // монолит: швы опалубки и отверстия от стяжек
    for (int i = 0; i < 3; i++) drawBox(AT(0, -0.8f + i * 0.8f, 1.03f), HS(0.5f, 0.005f, 0.001f), 0, RGB(140, 138, 132), false);
    for (int i = 0; i < 6; i++) drawBox(AT(-0.3f + (i % 3) * 0.3f, -0.4f + (i / 3) * 0.8f, 1.04f), HS(0.015f, 0.015f, 0.001f), 0, RGB(110, 108, 104), false);
  }
  else if (near_ && !f.putty && !f.paint && !f.paper) {          // голая стена: трещины, выбоины, остатки клея
    float o = ((h >> 3) % 7) * 0.12f - 0.36f, y = 0.9f;
    for (int i = 0; i < 9; i++) { drawBox(AT(o, y, 1.03f), HS(0.012f, 0.05f, 0.001f), 0, RGB(95, 92, 88), false); o += ((h >> (i + 5)) & 1) ? 0.025f : -0.025f; y -= 0.09f; }
    float o2 = ((h >> 6) % 5) * 0.15f - 0.3f, y2 = -0.2f;
    for (int i = 0; i < 6; i++) { drawBox(AT(o2, y2, 1.03f), HS(0.03f, 0.008f, 0.001f), 0, RGB(100, 97, 92), false); o2 += 0.05f; y2 += ((h >> (i + 2)) & 1) ? 0.03f : -0.03f; }
    for (int i = 0; i < 3; i++) { float ho = (((h >> (i * 4)) % 9) * 0.1f - 0.4f), hy = -1.0f + ((h >> (i * 3 + 1)) % 6) * 0.35f; if (FREE(ho, hy)) drawBox(AT(ho, hy, 1.04f), HS(0.025f, 0.022f, 0.001f), 0, RGB(110, 108, 104), false); }   // выбоины
    drawBox(AT(0.15f, 0.3f, 1.05f), HS(0.14f, 0.1f, 0.001f), 0, RGB(176, 168, 140), false);                                    // остатки клея и бумаги
  }
  else if (near_ && f.putty && !f.paint && !f.paper) {           // зашпаклёвано: ровная белая стена со следами шпателя
    for (int i = 0; i < 4; i++) drawBox(AT(-0.3f + i * 0.2f, -0.6f + (i % 2) * 0.9f, 1.03f), HS(0.12f, 0.25f, 0.001f), 0, RGB(242, 240, 236), false);
  }
  else if (near_ && f.paper) {                                    // новые обои по узору
    u32 a = pp.a, b = pp.b;
    switch (pp.kind) {
      case 0: for (int i = 0; i < 6; i++) { float o = -0.42f + i * 0.168f; drawBox(AT(o, 0, 1.03f), HS(0.04f, 1.28f, 0.001f), 0, b, false); drawBox(AT(o + 0.05f, 0, 1.04f), HS(0.004f, 1.28f, 0.001f), 0, mixc(b, a, 0.5f), false); } break;
      case 1: for (int r = 0; r < 7; r++) for (int q = 0; q < 4; q++) { float o = (r % 2) ? -0.5f + q * 0.333f : -0.333f + q * 0.333f, y = -1.05f + r * 0.36f; if (!(r % 2) && q == 3) continue; if (!FREE(o, y)) continue;
                motif(c, n, alongZ, o, y, 1.03f, 0.03f, 0.012f, b); motif(c, n, alongZ, o, y, 1.04f, 0.012f, 0.03f, b);
                motif(c, n, alongZ, o, y, 1.05f, 0.01f, 0.01f, RGB(245, 210, 90)); motif(c, n, alongZ, o + 0.035f, y - 0.05f, 1.04f, 0.015f, 0.006f, RGB(110, 160, 100)); } break;
      case 2: for (int r = 0; r < 9; r++) for (int q = 0; q < 5; q++) { float o = (r % 2) ? -0.5f + q * 0.25f : -0.375f + q * 0.25f, y = -1.15f + r * 0.29f; if (!(r % 2) && q == 4) continue; if (!FREE(o, y)) continue;
                motif(c, n, alongZ, o, y, 1.03f, 0.05f, 0.012f, b); motif(c, n, alongZ, o, y, 1.04f, 0.028f, 0.035f, b); motif(c, n, alongZ, o, y, 1.05f, 0.01f, 0.06f, b); } break;
      case 3: for (int r = 0; r < 20; r++) { float y = -1.25f + r * 0.13f; if (y > 1.25f) break; drawBox(AT(0, y, 1.03f), HS(0.5f, 0.008f, 0.001f), 0, b, false);
                for (int q = 0; q < 3; q++) { float o = -0.4f + q * 0.33f + (r % 2) * 0.165f; if (o > 0.5f) o -= 1.0f; if (FREE(o, y + 0.065f)) motif(c, n, alongZ, o, y + 0.065f, 1.03f, 0.008f, 0.06f, b); }
                if ((r * 7 + seed) % 5 == 0) motif(c, n, alongZ, -0.2f + (r % 3) * 0.2f, y + 0.065f, 1.02f, 0.15f, 0.055f, mixc(a, RGB(0, 0, 0), 0.12f)); } break;   // кирпичи разных оттенков
      case 4: for (int i = 0; i < 5; i++) { float o = -0.4f + i * 0.2f; drawBox(AT(o - 0.1f, 0, 1.03f), HS(0.004f, 1.28f, 0.001f), 0, mixc(b, RGB(0, 0, 0), 0.3f), false);
                for (int g = 0; g < 3; g++) drawBox(AT(o + (g - 1) * 0.045f, -0.4f + g * 0.5f, 1.04f), HS(0.002f, 0.5f, 0.001f), 0, b, false); } break;
      default: for (int i = 0; i < 8; i++) drawBox(AT(0, -1.2f + i * 0.34f, 1.03f), HS(0.5f, 0.004f, 0.001f), 0, b, false); for (int i = 0; i < 6; i++) drawBox(AT(-0.42f + i * 0.17f, 0, 1.04f), HS(0.003f, 1.28f, 0.001f), 0, b, false); break;
    }
    drawBox(AT(0, 0, 1.06f), HS(0.002f, 1.28f, 0.001f), 0, mixc(a, RGB(0, 0, 0), 0.15f), false);   // стык рулонов
  }
  #undef FREE
  #undef AT
  #undef HS
  bool fresh = f.paint != 0 || f.paper != 0;                       // после ремонта всё новое и белое
  // вдоль стены: смещение по длине (o) и наружу от стены (k)
  #define AT(o, y, k) (c + n * (k) + v3(0, (y), 0) + (alongZ ? v3(0, 0, (o)) : v3((o), 0, 0)))
  #define HS(l, h, d) (alongZ ? v3((d), (h), (l)) : v3((l), (h), (d)))
  if (room == R_BATH && fresh && near_) {                                      // в ванной после ремонта - плитка до середины стены
    drawBox(AT(0, -0.7f, 1.02f), HS(0.5f, 0.6f, 0.002f), 0, mixc(f.paint ? PAINT[f.paint] : pp.a, RGB(255, 255, 255), 0.4f), false);
    for (int i = 0; i < 4; i++) drawBox(AT(-0.5f + i * 0.25f, -0.7f, 1.1f), HS(0.003f, 0.6f, 0.001f), 0, RGB(200, 205, 212), false);
    for (int j = 0; j < 5; j++) drawBox(AT(0, -1.28f + j * 0.25f, 1.1f), HS(0.5f, 0.003f, 0.001f), 0, RGB(200, 205, 212), false);
  }
  drawBox(AT(0, -1.25f, 1.5f), HS(0.5f, 0.05f, 0.012f), 0, fresh ? RGB(245, 245, 245) : RGB(110, 80, 55), true);     // плинтус
  if (!fresh) drawBox(AT(0, 1.27f, 1.2f), HS(0.5f, 0.03f, 0.008f), 0, RGB(190, 185, 170), false);                  // старый потолочный бордюр
  else drawBox(AT(0, 1.25f, 1.6f), HS(0.5f, 0.05f, 0.02f), 0, RGB(250, 250, 250), true);                            // новый карниз
  if (seed % 4 == 1 && !f.window && near_) {                           // розетка
    drawBox(AT(0.2f, -1.0f, 1.3f), HS(0.04f, 0.04f, 0.006f), 0, fresh ? RGB(250, 250, 250) : RGB(215, 205, 175), true);
    drawBox(AT(0.2f, -1.0f, 1.9f), HS(0.01f, 0.006f, 0.002f), 0, RGB(60, 60, 60), false);
  }
  if (seed % 6 == 3 && !f.window && near_) drawBox(AT(-0.35f, -0.1f, 1.3f), HS(0.035f, 0.05f, 0.006f), 0, fresh ? RGB(250, 250, 250) : RGB(215, 205, 175), true);   // выключатель
  if (f.window) {                                                   // окно: рама, вид на улицу, подоконник, батарея, шторы
    drawBox(AT(0, 0.25f, 1.12f), HS(0.36f, 0.55f, 0.003f), 0, fresh ? RGB(250, 250, 250) : RGB(200, 190, 165), true);
    drawBox(AT(0, 0.45f, 1.3f), HS(0.31f, 0.3f, 0.002f), 0, RGB(150, 195, 240), false);                              // небо
    if (flatHouse == HO_KHRU) {                                     // двор: кирпичная пятиэтажка и деревья
      drawBox(AT(0, 0.0f, 1.32f), HS(0.31f, 0.22f, 0.002f), 0, RGB(185, 120, 95), false);
      if (near_) for (int i = 0; i < 12; i++) drawBox(AT(-0.24f + (i % 4) * 0.16f, -0.12f + (i / 4) * 0.11f, 1.4f), HS(0.03f, 0.03f, 0.001f), 0, i % 3 ? RGB(80, 95, 115) : RGB(250, 225, 150), false);
      drawBox(AT(-0.2f, -0.1f, 1.45f), HS(0.1f, 0.13f, 0.002f), 0, RGB(70, 130, 60), false); drawBox(AT(0.2f, -0.14f, 1.46f), HS(0.08f, 0.1f, 0.002f), 0, RGB(58, 115, 52), false);
    } else if (flatHouse == HO_STALIN) {                            // старая улица: жёлтый дом с колоннами, липы
      drawBox(AT(0, 0.1f, 1.32f), HS(0.31f, 0.3f, 0.002f), 0, RGB(222, 198, 145), false);
      drawBox(AT(0, 0.38f, 1.38f), HS(0.31f, 0.025f, 0.001f), 0, RGB(240, 228, 200), false);
      if (near_) for (int i = 0; i < 4; i++) { drawBox(AT(-0.22f + i * 0.147f, 0.08f, 1.4f), HS(0.015f, 0.26f, 0.001f), 0, RGB(242, 232, 210), false);
        drawBox(AT(-0.15f + i * 0.147f, 0.12f, 1.4f), HS(0.03f, 0.08f, 0.001f), 0, i == 1 ? RGB(250, 225, 150) : RGB(90, 100, 120), false); }
      drawBox(AT(-0.05f, -0.14f, 1.45f), HS(0.26f, 0.1f, 0.002f), 0, RGB(80, 140, 65), false);
    } else if (flatHouse == HO_NEW) {                               // новый квартал: стеклянные башни и кран
      drawBox(AT(-0.15f, 0.2f, 1.32f), HS(0.08f, 0.48f, 0.002f), 0, RGB(120, 155, 190), false);
      drawBox(AT(0.1f, 0.1f, 1.32f), HS(0.1f, 0.38f, 0.002f), 0, RGB(170, 180, 198), false);
      if (near_) for (int i = 0; i < 8; i++) drawBox(AT(-0.15f, -0.2f + i * 0.11f, 1.4f), HS(0.08f, 0.004f, 0.001f), 0, RGB(200, 220, 240), false);
      drawBox(AT(0.27f, 0.36f, 1.4f), HS(0.007f, 0.36f, 0.001f), 0, RGB(235, 190, 40), false); drawBox(AT(0.18f, 0.7f, 1.4f), HS(0.12f, 0.007f, 0.001f), 0, RGB(235, 190, 40), false);
    } else {                                                        // панельки
    drawBox(AT(-0.12f, 0.02f, 1.32f), HS(0.12f, 0.24f, 0.002f), 0, RGB(150, 140, 135), false);                      // соседний дом
    drawBox(AT(0.16f, 0.08f, 1.32f), HS(0.14f, 0.3f, 0.002f), 0, RGB(185, 175, 160), false);
    if (near_) for (int i = 0; i < 6; i++) drawBox(AT(-0.18f + (i % 3) * 0.06f + (i > 2 ? 0.3f : 0), -0.05f + (i / 3) * 0.12f + (i > 2 ? 0.05f : 0), 1.35f), HS(0.012f, 0.02f, 0.001f), 0, RGB(250, 230, 150), false); }   // окна напротив
    drawBox(AT(0, 0.25f, 1.5f), HS(0.012f, 0.5f, 0.002f), 0, fresh ? RGB(250, 250, 250) : RGB(200, 190, 165), false);   // перекладины рамы
    drawBox(AT(0, 0.25f, 1.5f), HS(0.31f, 0.012f, 0.002f), 0, fresh ? RGB(250, 250, 250) : RGB(200, 190, 165), false);
    drawBox(AT(0, -0.32f, 1.5f), HS(0.42f, 0.02f, 0.07f), 0, fresh ? RGB(248, 248, 248) : RGB(215, 205, 180), true);   // подоконник
    drawBox(AT(0, -0.72f, 2.0f), HS(0.3f, 0.18f, 0.03f), 0, fresh ? RGB(245, 245, 245) : RGB(180, 175, 165), true);   // батарея
    if (near_) for (int i = 0; i < 8; i++) drawBox(AT(-0.27f + i * 0.077f, -0.72f, 2.9f), HS(0.008f, 0.17f, 0.002f), 0, fresh ? RGB(215, 215, 220) : RGB(150, 145, 135), false);
    if (fresh) for (int k = -1; k <= 1; k += 2) {                   // шторы по бокам
      u32 cc = f.paint == 1 ? RGB(150, 120, 90) : mixc(PAINT[f.paint], RGB(60, 50, 80), 0.35f);
      drawBox(AT(k * 0.44f, 0.2f, 2.0f), HS(0.07f, 0.85f, 0.025f), 0, cc, true);
      if (near_) for (int j = 0; j < 3; j++) drawBox(AT(k * 0.44f + (j - 1) * 0.04f, 0.2f, 3.1f), HS(0.004f, 0.85f, 0.002f), 0, mixc(cc, RGB(0, 0, 0), 0.2f), false);
    }
    if (fresh) drawBox(AT(0, 1.05f, 2.2f), HS(0.55f, 0.012f, 0.012f), 0, RGB(120, 120, 128), true);   // карниз для штор
  }
  #undef AT
  #undef HS
}
static void drawFurn(int it, float cx, float cz, int rot) {
  int sh = furnShape(it); u32 cA = it > 0 && it < NFURN ? FURN[it].a : 0, cB = it > 0 && it < NFURN ? FURN[it].b : 0;   // свои цвета у стильной мебели
  float yawF = rot * 1.5708f, cs = cosf(yawF), sn = sinf(yawF);
  V3 c = v3(cx, 0, cz), ax = v3(cs, 0, -sn), az = v3(sn, 0, cs);
  #define L(x, y, z) (c + ax * (x) + v3(0, (y), 0) + az * (z))
  #define B(x, y, z, hx, hy, hz, col) drawBox(L(x, y, z), v3(hx, hy, hz), yawF, col, true)
  #define F(x, y, z, hx, hy, hz, col) drawBox(L(x, y, z), v3(hx, hy, hz), yawF, col, false)
  u32 wood = RGB(150, 105, 70), woodD = RGB(105, 72, 48), white = RGB(242, 242, 245), metal = RGB(185, 188, 195);
  switch (sh) {
    case 1: {   // диван: ножки, сиденье с двумя подушками, спинка, подлокотники, декоративная подушка
      u32 fab = cA ? cA : RGB(70, 95, 145), fabD = cB ? cB : RGB(58, 80, 125);
      for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) B(i * 0.4f, 0.04f, j * 0.25f, 0.025f, 0.04f, 0.025f, woodD);
      B(0, 0.17f, 0.02f, 0.46f, 0.09f, 0.32f, fabD);
      for (int i = -1; i <= 1; i += 2) B(i * 0.2f, 0.3f, 0.06f, 0.19f, 0.05f, 0.26f, fab);
      for (int i = -1; i <= 1; i += 2) B(i * 0.2f, 0.47f, -0.24f, 0.19f, 0.16f, 0.07f, fab);
      for (int i = -1; i <= 1; i += 2) B(i * 0.43f, 0.32f, 0.02f, 0.05f, 0.14f, 0.32f, fabD);
      B(0.27f, 0.42f, -0.12f, 0.08f, 0.07f, 0.03f, RGB(235, 190, 80));
    } break;
    case 2: {   // кровать: изголовье, рама, матрас, одеяло, две подушки
      if (cA) { wood = cA; woodD = mixc(cA, RGB(0, 0, 0), 0.2f); }
      B(0, 0.12f, 0.04f, 0.42f, 0.1f, 0.44f, wood);
      B(0, 0.25f, 0.04f, 0.4f, 0.05f, 0.42f, white);
      B(0, 0.31f, 0.14f, 0.405f, 0.02f, 0.32f, cB ? cB : RGB(90, 130, 170));
      F(0, 0.333f, -0.16f, 0.405f, 0.004f, 0.03f, RGB(240, 240, 245));                // отворот одеяла
      for (int i = -1; i <= 1; i += 2) B(i * 0.18f, 0.33f, -0.32f, 0.15f, 0.045f, 0.08f, RGB(250, 250, 255));
      B(0, 0.5f, -0.44f, 0.44f, 0.38f, 0.035f, woodD);
      F(0, 0.62f, -0.405f, 0.36f, 0.18f, 0.002f, wood);
    } break;
    case 3: {   // тумба и телевизор на подставке, светящийся экран, саундбар
      B(0, 0.22f, 0.05f, 0.46f, 0.22f, 0.22f, cA ? cA : RGB(60, 52, 48));
      for (int i = -1; i <= 1; i += 2) { F(i * 0.23f, 0.22f, 0.272f, 0.21f, 0.19f, 0.002f, cA ? mixc(cA, RGB(0, 0, 0), 0.06f) : RGB(75, 66, 60)); F(i * 0.05f, 0.24f, 0.276f, 0.006f, 0.05f, 0.004f, metal); }
      B(0, 0.46f, 0.0f, 0.08f, 0.015f, 0.06f, RGB(30, 30, 34)); B(0, 0.52f, -0.03f, 0.02f, 0.06f, 0.015f, RGB(30, 30, 34));
      B(0, 0.8f, -0.03f, 0.44f, 0.26f, 0.015f, RGB(18, 18, 22));
      F(0, 0.8f, -0.012f, 0.42f, 0.24f, 0.002f, RGB(60, 110, 170)); F(-0.15f, 0.88f, -0.009f, 0.12f, 0.06f, 0.001f, RGB(110, 170, 220));
      B(0, 0.465f, 0.18f, 0.25f, 0.025f, 0.035f, RGB(25, 25, 28));
    } break;
    case 4: {   // шкаф: две створки, ручки, карниз
      u32 bd = cA ? cA : RGB(210, 180, 135);
      B(0, 1.0f, -0.12f, 0.45f, 1.0f, 0.3f, bd);
      F(0, 1.0f, 0.182f, 0.003f, 0.95f, 0.002f, mixc(bd, RGB(0, 0, 0), 0.25f));
      for (int i = -1; i <= 1; i += 2) F(i * 0.06f, 1.05f, 0.19f, 0.008f, 0.1f, 0.006f, metal);
      B(0, 2.02f, -0.12f, 0.47f, 0.025f, 0.32f, mixc(bd, RGB(0, 0, 0), 0.1f));
      B(0, 0.04f, -0.12f, 0.44f, 0.04f, 0.29f, mixc(bd, RGB(0, 0, 0), 0.42f));
      if (it == 24) for (int i = -1; i <= 1; i += 2) F(i * 0.23f, 1.0f, 0.184f, 0.17f, 0.8f, 0.002f, mixc(bd, RGB(255, 220, 150), 0.12f));   // филёнки у классики
    } break;
    case 5: {   // холодильник: морозилка и основная дверь, ручки
      B(0, 0.9f, -0.08f, 0.31f, 0.9f, 0.3f, RGB(232, 235, 240));
      F(0, 1.32f, 0.222f, 0.3f, 0.004f, 0.002f, RGB(170, 175, 182));
      F(0.24f, 1.55f, 0.23f, 0.012f, 0.14f, 0.012f, metal); F(0.24f, 0.85f, 0.23f, 0.012f, 0.3f, 0.012f, metal);
      F(-0.15f, 1.7f, 0.223f, 0.05f, 0.012f, 0.002f, RGB(90, 140, 200));
    } break;
    case 6: {   // плита: духовка со стеклом, конфорки, ручки управления
      B(0, 0.43f, 0, 0.3f, 0.43f, 0.3f, white);
      F(0, 0.36f, 0.302f, 0.24f, 0.17f, 0.002f, RGB(30, 30, 34)); F(0, 0.36f, 0.305f, 0.2f, 0.12f, 0.001f, RGB(70, 60, 50));
      F(0, 0.58f, 0.31f, 0.2f, 0.012f, 0.012f, metal);
      for (int i = 0; i < 4; i++) { F(-0.12f + (i % 2) * 0.24f, 0.862f, -0.12f + (i / 2) * 0.24f, 0.075f, 0.004f, 0.075f, RGB(28, 28, 30)); F(-0.12f + (i % 2) * 0.24f, 0.865f, -0.12f + (i / 2) * 0.24f, 0.04f, 0.003f, 0.04f, RGB(60, 60, 64)); }
      for (int i = 0; i < 4; i++) F(-0.18f + i * 0.12f, 0.78f, 0.305f, 0.02f, 0.02f, 0.01f, RGB(40, 40, 44));
    } break;
    case 7: {   // стол на ножках, два стула со спинками, ваза с цветами
      B(0, 0.73f, 0, 0.42f, 0.025f, 0.32f, wood);
      for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) B(i * 0.37f, 0.36f, j * 0.27f, 0.025f, 0.36f, 0.025f, woodD);
      for (int k = -1; k <= 1; k += 2) { V3 o = ax * (0.0f) + az * (0.5f * k);
        drawBox(L(0, 0.43f, 0.48f * k), v3(0.17f, 0.025f, 0.17f), yawF, RGB(170, 125, 85), true);
        for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) drawBox(L(i * 0.14f, 0.21f, 0.48f * k + j * 0.14f), v3(0.018f, 0.21f, 0.018f), yawF, woodD, true);
        drawBox(L(0, 0.68f, 0.48f * k + 0.15f * k), v3(0.17f, 0.22f, 0.02f), yawF, RGB(170, 125, 85), true); (void)o; }
      B(0, 0.82f, 0, 0.04f, 0.07f, 0.04f, RGB(120, 170, 200)); F(0, 0.92f, 0, 0.07f, 0.04f, 0.07f, RGB(230, 90, 110));
    } break;
    case 8: {   // ванна: бортик, вода, кран
      B(0, 0.27f, 0, 0.36f, 0.27f, 0.48f, white);
      F(0, 0.535f, 0.02f, 0.29f, 0.004f, 0.41f, RGB(150, 200, 235));
      B(0, 0.62f, -0.44f, 0.02f, 0.08f, 0.02f, metal); B(0, 0.68f, -0.4f, 0.015f, 0.015f, 0.05f, metal);
    } break;
    case 9: {   // тумба с раковиной, кран, зеркало
      B(0, 0.4f, -0.1f, 0.3f, 0.4f, 0.22f, RGB(240, 240, 245));
      F(0, 0.4f, 0.122f, 0.003f, 0.36f, 0.002f, RGB(200, 200, 208));
      B(0, 0.84f, -0.1f, 0.32f, 0.04f, 0.24f, white); F(0, 0.882f, -0.08f, 0.2f, 0.004f, 0.14f, RGB(205, 225, 240));
      B(0, 0.95f, -0.27f, 0.015f, 0.07f, 0.015f, metal); B(0, 1.01f, -0.22f, 0.012f, 0.012f, 0.05f, metal);
      B(0, 1.45f, -0.33f, 0.26f, 0.3f, 0.015f, RGB(200, 200, 205)); F(0, 1.45f, -0.313f, 0.23f, 0.27f, 0.002f, RGB(190, 220, 240));
    } break;
    case 10: {  // унитаз: бачок, чаша, сиденье
      B(0, 0.16f, 0.02f, 0.13f, 0.16f, 0.17f, white);
      B(0, 0.36f, 0.04f, 0.18f, 0.035f, 0.22f, RGB(250, 250, 252));
      F(0, 0.398f, 0.05f, 0.12f, 0.003f, 0.15f, RGB(220, 230, 238));
      B(0, 0.62f, -0.22f, 0.19f, 0.2f, 0.08f, white); F(0, 0.83f, -0.22f, 0.04f, 0.008f, 0.03f, metal);
    } break;
    case 11: {  // цветок: горшок, земля, листья ярусами
      B(0, 0.12f, 0, 0.13f, 0.12f, 0.13f, RGB(190, 100, 70)); B(0, 0.25f, 0, 0.15f, 0.02f, 0.15f, RGB(170, 90, 60));
      F(0, 0.27f, 0, 0.12f, 0.004f, 0.12f, RGB(70, 50, 35));
      for (int k = 0; k < 6; k++) { float a = k * 1.047f; drawBox(L(cosf(a) * 0.12f, 0.45f + (k % 2) * 0.12f, sinf(a) * 0.12f), v3(0.13f, 0.02f, 0.05f), yawF + a, k % 2 ? RGB(60, 150, 70) : RGB(80, 175, 85), true); }
      B(0, 0.62f, 0, 0.06f, 0.12f, 0.06f, RGB(70, 160, 75));
    } break;
    case 12: {  // ковёр: бордюр и узор
      F(0, 0.006f, 0, 0.46f, 0.004f, 0.46f, RGB(150, 50, 60));
      F(0, 0.011f, 0, 0.4f, 0.003f, 0.4f, RGB(215, 190, 150));
      F(0, 0.015f, 0, 0.24f, 0.003f, 0.24f, RGB(150, 50, 60));
      for (int i = 0; i < 4; i++) F((i % 2 ? 1 : -1) * 0.32f, 0.016f, (i / 2 ? 1 : -1) * 0.32f, 0.05f, 0.002f, 0.05f, RGB(40, 70, 120));
    } break;
    case 13: {  // торшер: основание, стойка, светящийся абажур
      B(0, 0.02f, 0, 0.14f, 0.02f, 0.14f, RGB(40, 40, 44)); B(0, 0.8f, 0, 0.015f, 0.78f, 0.015f, RGB(60, 60, 66));
      B(0, 1.62f, 0, 0.17f, 0.12f, 0.17f, RGB(250, 235, 190)); F(0, 1.5f, 0, 0.12f, 0.003f, 0.12f, RGB(255, 250, 220));
    } break;
    case 14: {  // кресло
      u32 fab = cA ? cA : RGB(160, 110, 80), fabD = cB ? cB : RGB(135, 90, 65);
      B(0, 0.2f, 0.02f, 0.3f, 0.1f, 0.3f, fabD); B(0, 0.33f, 0.06f, 0.24f, 0.05f, 0.25f, fab);
      B(0, 0.55f, -0.25f, 0.3f, 0.25f, 0.07f, fab);
      for (int i = -1; i <= 1; i += 2) B(i * 0.27f, 0.38f, 0.02f, 0.05f, 0.12f, 0.3f, fabD);
      for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) B(i * 0.25f, 0.05f, j * 0.25f, 0.02f, 0.05f, 0.02f, woodD);
    } break;
    case 15: {  // журнальный столик со стеклом и книгой
      for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) B(i * 0.32f, 0.18f, j * 0.2f, 0.02f, 0.18f, 0.02f, RGB(40, 40, 44));
      B(0, 0.37f, 0, 0.36f, 0.015f, 0.24f, RGB(170, 205, 220)); B(0, 0.12f, 0, 0.33f, 0.01f, 0.21f, wood);
      B(0.12f, 0.395f, 0.05f, 0.08f, 0.012f, 0.06f, RGB(180, 60, 60)); B(-0.15f, 0.4f, -0.05f, 0.04f, 0.02f, 0.04f, white);
    } break;
    case 16: {  // книжный шкаф: полки с книгами разного цвета
      if (cA) { woodD = cB; wood = cA; }
      B(0, 0.95f, -0.25f, 0.45f, 0.95f, 0.04f, woodD);
      for (int i = -1; i <= 1; i += 2) B(i * 0.43f, 0.95f, -0.1f, 0.02f, 0.95f, 0.17f, wood);
      for (int s = 0; s < 5; s++) { float y = 0.08f + s * 0.42f; B(0, y, -0.1f, 0.42f, 0.015f, 0.17f, wood);
        if (s < 4) for (int bk = 0; bk < 9; bk++) { float h = 0.12f + ((bk * 7 + s * 3) % 5) * 0.025f; u32 bc = bk % 4 == 0 ? RGB(170, 60, 60) : bk % 4 == 1 ? RGB(60, 100, 160) : bk % 4 == 2 ? RGB(220, 190, 90) : RGB(70, 130, 80);
          B(-0.36f + bk * 0.09f, y + 0.015f + h, -0.08f, 0.035f, h, 0.12f, bc); } }
    } break;
    case 17: {  // кухонный шкаф: нижняя тумба со столешницей, верхний шкаф, фартук
      u32 kb = cA ? cA : RGB(235, 235, 238);
      B(0, 0.42f, -0.18f, 0.48f, 0.42f, 0.28f, kb);
      B(0, 0.87f, -0.18f, 0.5f, 0.025f, 0.3f, cB ? cB : RGB(110, 85, 65));
      for (int i = -1; i <= 1; i += 2) { F(i * 0.24f, 0.42f, 0.102f, 0.22f, 0.38f, 0.002f, mixc(kb, RGB(0, 0, 0), 0.05f)); F(i * 0.05f, 0.62f, 0.107f, 0.006f, 0.06f, 0.004f, metal); }
      F(0, 1.15f, -0.455f, 0.48f, 0.25f, 0.003f, RGB(200, 225, 230));
      B(0, 1.75f, -0.3f, 0.48f, 0.3f, 0.16f, kb); F(0, 1.75f, -0.138f, 0.003f, 0.28f, 0.002f, RGB(200, 200, 205));
      B(0.2f, 0.94f, -0.25f, 0.08f, 0.05f, 0.06f, RGB(90, 90, 96));   // чайник
    } break;
    case 18: {  // стиральная машина: круглая дверца, панель
      B(0, 0.43f, -0.05f, 0.3f, 0.43f, 0.29f, white);
      F(0, 0.78f, 0.242f, 0.29f, 0.06f, 0.002f, RGB(215, 218, 225)); F(0.18f, 0.78f, 0.246f, 0.03f, 0.03f, 0.002f, RGB(60, 60, 66));
      for (int k = 0; k < 8; k++) { float a = k * 0.785f; F(cosf(a) * 0.15f, 0.38f + sinf(a) * 0.15f, 0.243f, 0.06f, 0.06f, 0.002f, metal); }
      F(0, 0.38f, 0.246f, 0.12f, 0.12f, 0.002f, RGB(120, 150, 180)); F(0.06f, 0.43f, 0.248f, 0.03f, 0.03f, 0.001f, RGB(200, 220, 240));
    } break;
    case FU_CRIB: {   // детская кроватка: рама с прутьями, матрас, одеяло с рисунком, мобиль с игрушками
      u32 fr = RGB(240, 236, 228);
      for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) B(i * 0.36f, 0.42f, j * 0.24f, 0.025f, 0.42f, 0.025f, fr);
      B(0, 0.25f, 0, 0.35f, 0.03f, 0.23f, fr); B(0, 0.3f, 0, 0.34f, 0.04f, 0.22f, RGB(250, 250, 252));
      B(0.08f, 0.35f, 0, 0.24f, 0.015f, 0.21f, RGB(160, 200, 230)); F(0.05f, 0.368f, 0.05f, 0.04f, 0.002f, 0.04f, RGB(250, 210, 90));
      B(-0.25f, 0.37f, 0, 0.07f, 0.03f, 0.12f, RGB(255, 255, 255));
      for (int j = -1; j <= 1; j += 2) { B(0, 0.8f, j * 0.24f, 0.36f, 0.02f, 0.02f, fr); for (int k = 0; k < 9; k++) B(-0.32f + k * 0.08f, 0.56f, j * 0.24f, 0.008f, 0.22f, 0.008f, fr); }
      for (int i = -1; i <= 1; i += 2) { B(i * 0.36f, 0.56f, 0, 0.012f, 0.22f, 0.22f, fr); }
      B(0.36f, 1.05f, 0, 0.01f, 0.25f, 0.01f, RGB(200, 200, 205)); B(0.2f, 1.3f, 0, 0.16f, 0.008f, 0.008f, RGB(200, 200, 205));
      for (int k = 0; k < 3; k++) B(0.06f + (k - 1) * 0.08f, 1.2f, (k - 1) * 0.06f, 0.025f, 0.025f, 0.025f, k == 0 ? RGB(240, 120, 120) : k == 1 ? RGB(120, 200, 140) : RGB(250, 210, 90));
    } break;
    case FU_DESK: {   // рабочий стол: столешница, ножки, тумба, монитор, клавиатура, офисное кресло
      B(0, 0.74f, -0.12f, 0.46f, 0.02f, 0.26f, RGB(235, 235, 232));
      for (int i = -1; i <= 1; i += 2) B(i * 0.42f, 0.36f, -0.12f, 0.02f, 0.36f, 0.24f, RGB(50, 50, 54));
      B(0.28f, 0.3f, -0.12f, 0.12f, 0.28f, 0.22f, RGB(225, 225, 222)); F(0.28f, 0.45f, 0.102f, 0.1f, 0.003f, 0.002f, RGB(150, 150, 155));
      B(-0.05f, 0.77f, -0.26f, 0.08f, 0.01f, 0.05f, RGB(30, 30, 34)); B(-0.05f, 0.86f, -0.28f, 0.015f, 0.09f, 0.012f, RGB(30, 30, 34));
      B(-0.05f, 1.06f, -0.27f, 0.27f, 0.15f, 0.012f, RGB(20, 20, 24)); F(-0.05f, 1.06f, -0.256f, 0.25f, 0.13f, 0.002f, RGB(40, 90, 150));
      F(-0.12f, 1.1f, -0.253f, 0.1f, 0.012f, 0.001f, RGB(120, 220, 140)); F(-0.1f, 1.06f, -0.253f, 0.12f, 0.008f, 0.001f, RGB(220, 220, 240)); F(-0.14f, 1.02f, -0.253f, 0.08f, 0.008f, 0.001f, RGB(240, 180, 90));   // код на экране
      B(-0.05f, 0.765f, -0.04f, 0.18f, 0.008f, 0.06f, RGB(40, 40, 44)); B(0.2f, 0.765f, -0.03f, 0.03f, 0.008f, 0.045f, RGB(40, 40, 44));
      B(0.36f, 0.95f, -0.3f, 0.05f, 0.2f, 0.05f, RGB(30, 30, 34)); F(0.36f, 0.98f, -0.248f, 0.002f, 0.12f, 0.002f, RGB(80, 220, 255));   // системный блок с подсветкой
      B(-0.05f, 0.46f, 0.3f, 0.2f, 0.04f, 0.19f, RGB(40, 42, 50)); B(-0.05f, 0.75f, 0.48f, 0.19f, 0.25f, 0.03f, RGB(40, 42, 50));
      B(-0.05f, 0.24f, 0.3f, 0.025f, 0.2f, 0.025f, RGB(120, 120, 126));
      for (int k = 0; k < 5; k++) { float a = k * 1.2566f; B(-0.05f + cosf(a) * 0.16f, 0.03f, 0.3f + sinf(a) * 0.16f, 0.025f, 0.02f, 0.025f, RGB(30, 30, 34)); }
    } break;
    case FU_SIDEBOARD: {   // старый сервант: лакированный корпус, стекло с хрусталём и сервизом, нижние дверцы
      u32 lac = RGB(105, 62, 38), lacD = RGB(80, 46, 28);
      B(0, 0.95f, -0.18f, 0.48f, 0.95f, 0.24f, lac);
      F(0, 1.35f, 0.062f, 0.44f, 0.45f, 0.002f, RGB(140, 160, 165));               // стекло
      for (int sh2 = 0; sh2 < 2; sh2++) { F(0, 1.08f + sh2 * 0.32f, 0.03f, 0.43f, 0.008f, 0.03f, lacD);
        for (int k = 0; k < 6; k++) F(-0.33f + k * 0.13f, 1.18f + sh2 * 0.32f, 0.066f, 0.045f, 0.08f, 0.001f, k % 2 ? RGB(240, 240, 245) : RGB(200, 225, 235)); }   // тарелки и хрусталь
      for (int i = -1; i <= 1; i += 2) { F(i * 0.24f, 0.45f, 0.062f, 0.21f, 0.38f, 0.002f, lacD); F(i * 0.04f, 0.5f, 0.066f, 0.008f, 0.03f, 0.004f, RGB(200, 180, 100)); }
      B(0, 1.92f, -0.18f, 0.5f, 0.03f, 0.26f, lacD);
    } break;
    case FU_OLDTV: {   // старый телевизор: пузатый кинескоп на тумбочке, антенна «рога»
      B(0, 0.28f, 0, 0.32f, 0.28f, 0.22f, RGB(120, 80, 50));
      for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) B(i * 0.28f, 0.03f, j * 0.18f, 0.02f, 0.03f, 0.02f, RGB(80, 55, 35));
      B(0, 0.82f, -0.04f, 0.3f, 0.26f, 0.24f, RGB(60, 58, 55)); B(0, 0.82f, -0.25f, 0.2f, 0.18f, 0.1f, RGB(55, 52, 50));
      F(-0.05f, 0.84f, 0.203f, 0.2f, 0.18f, 0.002f, RGB(70, 85, 80)); F(-0.1f, 0.9f, 0.206f, 0.06f, 0.04f, 0.001f, RGB(105, 125, 118));
      for (int k = 0; k < 3; k++) F(0.22f, 0.95f - k * 0.08f, 0.205f, 0.02f, 0.02f, 0.002f, RGB(180, 175, 160));
      for (int i = -1; i <= 1; i += 2) drawBox(L(i * 0.1f, 1.25f, -0.05f), v3(0.006f, 0.2f, 0.006f), yawF + i * 0.5f, RGB(170, 170, 175), true);
    } break;
  }
  #undef L
  #undef B
  #undef F
}
static void drawFlat() {
  // земля вокруг и пол
  groundQuad(-6, -6, MW + 6, MH + 6, -0.01f, RGB(110, 120, 100));
  for (int x = 0; x < MW; x++) for (int z = 0; z < MH; z++) if (inRoom(x, z)) {
    Cell &c = cells[x][z]; int r = cellR[x][z];
    V3 cc = v3(x + 0.5f, 1.3f, z + 0.5f); if (!seen(cc, 1.6f)) continue;
    bool nearC = camDist2(cc) < 4.0f * 4.0f;
    groundQuad((float)x, (float)z, x + 1.0f, z + 1.0f, 0, c.floor ? newFloorCol(c.floor, x, z) : oldFloorCol(r));
    if (!nearC) {}
    else if (!c.floor && flatCond == CO_BARE) { groundQuad(x + 0.3f, z + 0.2f, x + 0.36f, z + 0.26f, 0.002f, RGB(125, 125, 122)); groundQuad(x + 0.7f, z + 0.6f, x + 0.78f, z + 0.66f, 0.002f, RGB(165, 165, 162)); }
    else if (!c.floor && flatCond == CO_FLOOD && !tiledRoom(r)) { for (int k = 0; k < 3; k++) drawBox(v3(x + 0.5f, 0.012f, z + 0.17f + k * 0.33f), v3(0.5f, 0.012f + ((x + z + k) % 2) * 0.01f, 0.14f), ((x * 3 + k) % 3 - 1) * 0.04f, mixc(oldFloorCol(r), RGB(255, 255, 255), 0.06f * k), true); }   // вздутые доски
    else if (!c.floor && flatHouse == HO_STALIN && !tiledRoom(r)) { for (int k = 0; k < 4; k++) for (int q = 0; q < 2; q++) groundQuad(x + q * 0.5f + 0.02f, z + k * 0.25f + 0.02f, x + q * 0.5f + 0.48f, z + k * 0.25f + 0.23f, 0.0015f, (k + q) % 2 ? mixc(oldFloorCol(r), RGB(0, 0, 0), 0.12f) : mixc(oldFloorCol(r), RGB(255, 255, 255), 0.06f)); }   // старый паркет ёлочкой
    else if (!c.floor) { groundQuad(x + 0.1f, z + 0.45f, x + 0.6f, z + 0.55f, 0.002f, mixc(oldFloorCol(r), RGB(0, 0, 0), 0.2f));   // трещины старого линолеума
      groundQuad(x + 0.05f, z + 0.05f, x + 0.45f, z + 0.45f, 0.001f, mixc(oldFloorCol(r), RGB(255, 255, 255), 0.08f)); groundQuad(x + 0.55f, z + 0.55f, x + 0.95f, z + 0.95f, 0.001f, mixc(oldFloorCol(r), RGB(255, 255, 255), 0.08f)); }
    else if (FLOORS[imax(1, imin(NFLOOR - 1, (int)c.floor))].tile) { for (int k = 1; k < 3; k++) { groundQuad(x + k * 0.333f - 0.004f, (float)z, x + k * 0.333f + 0.004f, z + 1.0f, 0.002f, RGB(175, 180, 188)); groundQuad((float)x, z + k * 0.333f - 0.004f, x + 1.0f, z + k * 0.333f + 0.004f, 0.002f, RGB(175, 180, 188)); } }
    else for (int k = 0; k < 4; k++) { float zz = z + k * 0.25f; groundQuad((float)x, zz - 0.003f, x + 1.0f, zz + 0.003f, 0.002f, RGB(140, 100, 62)); float sx = x + fmodf((k * 0.37f + x * 0.61f + z * 0.29f), 1.0f); groundQuad(sx - 0.003f, zz, sx + 0.003f, zz + 0.25f, 0.002f, RGB(140, 100, 62));
      if ((x + z + k) % 3 == 0) groundQuad((float)x, zz + 0.003f, x + 1.0f, zz + 0.25f - 0.003f, 0.0015f, RGB(184, 140, 92)); }   // доски ламината со сдвигом швов
    if (c.stain == 2) { groundQuad(x + 0.12f, z + 0.18f, x + 0.82f, z + 0.78f, 0.004f, RGB(28, 24, 22)); groundQuad(x + 0.3f, z + 0.08f, x + 0.6f, z + 0.9f, 0.0045f, RGB(36, 31, 28)); }   // гарь
    else if (c.stain == 3) { groundQuad(x + 0.1f, z + 0.15f, x + 0.9f, z + 0.85f, 0.03f, RGB(118, 145, 165)); groundQuad(x + 0.25f, z + 0.3f, x + 0.5f, z + 0.45f, 0.031f, RGB(185, 205, 220)); }   // лужа
    else if (c.stain) groundQuad(x + 0.2f, z + 0.25f, x + 0.75f, z + 0.7f, 0.004f, RGB(70, 55, 40));
    if (c.furn && seen(v3(x + 0.5f, 0.6f, z + 0.5f), 0.9f)) drawFurn(c.furn, x + 0.5f, z + 0.5f, c.frot);
    if (pitch > -0.6f) groundQuad((float)x, (float)z, x + 1.0f, z + 1.0f, 2.6f, c.floor ? RGB(248, 248, 246) : flatCond == CO_FIRE ? RGB(92, 86, 80) : flatCond == CO_BARE ? RGB(182, 182, 178) : flatCond == CO_FLOOD ? RGB(200, 192, 168) : RGB(215, 210, 196));   // потолок (когда смотришь в пол - не нужен)
    if (x % 3 == 1 && z % 3 == 1) {                                                             // лампа: старая лампочка на проводе или новый светильник
      if (!c.floor) { drawBox(v3(x + 0.5f, 2.45f, z + 0.5f), v3(0.006f, 0.15f, 0.006f), 0, RGB(30, 30, 30), false); drawBox(v3(x + 0.5f, 2.27f, z + 0.5f), v3(0.035f, 0.045f, 0.035f), 0, RGB(255, 235, 170), false); }
      else { drawBox(v3(x + 0.5f, 2.57f, z + 0.5f), v3(0.22f, 0.025f, 0.22f), 0.4f, RGB(250, 250, 250), true); drawBox(v3(x + 0.5f, 2.545f, z + 0.5f), v3(0.17f, 0.002f, 0.17f), 0.4f, RGB(255, 250, 225), false); }
    }
  }
  // стены
  for (int z = 0; z < MH; z++) for (int x = 0; x <= MW; x++) {
    if (!wallX[x][z]) continue;
    if (doorX[x][z]) { bool ent = (x == entX || x == entX + 1) && z == entZ; u32 lf = ent ? RGB(120, 70, 45) : RGB(215, 185, 140);
      for (int k = 0; k < 2; k++) drawBox(v3((float)x, 1.05f, z + 0.05f + k * 0.9f), v3(0.06f, 1.05f, 0.05f), 0, RGB(240, 240, 240), true); drawBox(v3((float)x, 2.35f, z + 0.5f), v3(0.08f, 0.25f, 0.5f), 0, RGB(210, 208, 200), true);
      (void)lf; if (seen(v3((float)x, 1.2f, z + 0.5f), 1.5f)) doorLeaf(v3((float)x, 0, z + 0.06f), 0.0f, angX[x][z], ent); continue; }
    for (int s = 0; s < 2; s++) { int cx = s == 0 ? x - 1 : x; V3 c = v3(x + (s == 0 ? -0.04f : 0.04f), 1.3f, z + 0.5f);
      if (inRoom(cx, z)) wallSide(c, true, fX[x][z][s], s == 0 ? -1.0f : 1.0f, cellR[cx][z], x * 7 + z * 3 + s); else drawBox(c, v3(0.04f, 1.3f, 0.5f), 0, outerWallCol(), true); }
  }
  for (int x = 0; x < MW; x++) for (int z = 0; z <= MH; z++) {
    if (!wallZ[x][z]) continue;
    if (doorZ[x][z]) { bool ent = x == entX && (z == entZ || z == entZ + 1); u32 lf = ent ? RGB(120, 70, 45) : RGB(215, 185, 140);
      for (int k = 0; k < 2; k++) drawBox(v3(x + 0.05f + k * 0.9f, 1.05f, (float)z), v3(0.05f, 1.05f, 0.06f), 0, RGB(240, 240, 240), true); drawBox(v3(x + 0.5f, 2.35f, (float)z), v3(0.5f, 0.25f, 0.08f), 0, RGB(210, 208, 200), true);
      (void)lf; if (seen(v3(x + 0.5f, 1.2f, (float)z), 1.5f)) doorLeaf(v3(x + 0.06f, 0, (float)z), 1.5708f, -angZ[x][z], ent); continue; }
    for (int s = 0; s < 2; s++) { int cz = s == 0 ? z - 1 : z; V3 c = v3(x + 0.5f, 1.3f, z + (s == 0 ? -0.04f : 0.04f));
      if (inRoom(x, cz)) wallSide(c, false, fZ[x][z][s], s == 0 ? -1.0f : 1.0f, cellR[x][cz], x * 5 + z * 11 + s); else drawBox(c, v3(0.5f, 1.3f, 0.04f), 0, outerWallCol(), true); }
  }
  // контейнер для мусора у входа
  drawBox(v3(binX, 0.4f, binZ), v3(0.35f, 0.4f, 0.3f), 0, RGB(40, 110, 60), true); drawBox(v3(binX, 0.82f, binZ), v3(0.37f, 0.03f, 0.32f), 0, RGB(30, 80, 45), true);
  // мусор
  for (size_t i = 0; i < trash.size(); i++) { const Trash &t = trash[i]; if (!t.alive) continue; V3 c = v3(t.x, 0, t.z); if (!seen(c + v3(0, 0.2f, 0), 0.4f)) continue;
    if (t.kind == 0) { drawBox(c + v3(0, 0.15f, 0), v3(0.17f, 0.15f, 0.15f), t.x, RGB(25, 25, 28), true); drawBox(c + v3(0, 0.33f, 0), v3(0.04f, 0.04f, 0.04f), t.x, RGB(25, 25, 28), true); }
    else if (t.kind == 1) drawBox(c + v3(0, 0.14f, 0), v3(0.2f, 0.14f, 0.16f), t.z, RGB(170, 130, 80), true);
    else if (t.kind == 2) for (int k = 0; k < 3; k++) drawBox(c + v3(k * 0.08f - 0.08f, 0.1f, (k % 2) * 0.06f), v3(0.03f, 0.1f, 0.03f), 0, RGB(60, 120, 60), true);
    else if (t.kind == 4) { drawBox(c + v3(0, 0.07f, 0), v3(0.22f, 0.07f, 0.14f), t.x, RGB(205, 195, 170), true); drawBox(c + v3(0, 0.141f, 0), v3(0.12f, 0.002f, 0.08f), t.x, RGB(60, 90, 160), false);   // мешок цемента
      drawBox(c + v3(0.3f, 0.03f, 0.1f), v3(0.06f, 0.03f, 0.06f), t.z, RGB(175, 175, 170), true); }                                                                                                    // обломок
    else if (t.kind == 5) for (int k = 0; k < 3; k++) drawBox(c + v3(0, 0.025f + k * 0.045f, 0), v3(0.32f, 0.022f, 0.05f), t.x * 2 + k * 0.7f, k == 1 ? RGB(55, 40, 30) : RGB(30, 24, 20), true);   // обгоревшие доски
    else if (t.kind == 6) { groundQuad(t.x - 0.25f, t.z - 0.22f, t.x + 0.25f, t.z + 0.22f, 0.006f, RGB(110, 125, 135)); drawBox(c + v3(0, 0.1f, 0), v3(0.2f, 0.1f, 0.17f), t.z + 0.3f, RGB(135, 112, 82), true);
      drawBox(c + v3(0.02f, 0.2f, 0), v3(0.2f, 0.01f, 0.15f), t.z + 0.4f, RGB(120, 98, 70), true); }                                                                                                  // размокшая коробка
    else { drawBox(c + v3(0, 0.12f, 0), v3(0.22f, 0.12f, 0.22f), t.x + t.z, RGB(40, 40, 42), true); drawBox(c + v3(0, 0.12f, 0), v3(0.1f, 0.13f, 0.1f), t.x + t.z, RGB(110, 120, 100), true); } }
  // призрак мебели при выборе места
  if (tool == T_FURN && aim.type == H_FLOOR && !cells[aim.x][aim.z].furn) drawFurn(furnSel, aim.x + 0.5f, aim.z + 0.5f, ((int)roundf(yaw / 1.5708f) % 4 + 6) % 4);
}
// стержень из коротких кубиков между двумя точками (рукоятки, черенки, проволока валика)
static void rod(V3 a, V3 b, float r, float yw, u32 col) {
  V3 d = b - a; float len = sqrtf(dot(d, d)); int n = imax(2, (int)(len / (r * 1.6f)));
  for (int i = 0; i <= n; i++) drawBox(a + d * ((float)i / n), v3(r, r, r), yw, col, true);
}
static void drawHands() {                         // руки в рабочих перчатках и инструмент от первого лица
  V3 E = camEye, F = camF, R = camR, U = camU; float yw = atan2f(F.x, F.z);
  float t = workBob, sw = working ? sinf(t) : 0, sw2 = working ? cosf(t * 0.5f) : 0;
  V3 rh = E + F * 0.46f + R * 0.2f + U * -0.12f, lh = E + F * 0.44f + R * -0.21f + U * -0.14f;   // руки в нижней части кадра, над панелью
  u32 sleeve = RGB(40, 70, 120), glove = RGB(230, 140, 40), gloveD = RGB(190, 110, 30);
  // движение руки во время работы
  if (working) {
    if (tool == T_MOP) rh = rh + R * (0.06f * sw);
    else if (tool == T_SCRAPER) rh = rh + F * (0.03f * sw) + U * (0.02f * sw2);
    else if (tool == T_ROLLER) rh = rh + U * (0.06f * sw);
    else if (tool == T_FLOOR) rh = rh + U * (0.03f * fabsf(sw));
    else if (tool == T_HAND) { rh = rh + F * (0.05f * fabsf(sw)) + U * (-0.03f * fabsf(sw)); lh = lh + F * (0.05f * fabsf(sw)); }
  }
  for (int k = 0; k < 2; k++) {                     // рукава и перчатки с пальцами
    V3 h = k ? rh : lh;
    drawBox(h - F * 0.1f - U * 0.035f, v3(0.024f, 0.024f, 0.065f), yw, sleeve, true);   // рукав тоньше, чтобы не закрывал обзор
    drawBox(h - F * 0.035f - U * 0.008f, v3(0.028f, 0.024f, 0.01f), yw, RGB(30, 50, 90), true);                         // манжета
    drawBox(h, v3(0.032f, 0.024f, 0.036f), yw, glove, true);
    for (int f = 0; f < 4; f++) drawBox(h + F * 0.045f + R * (-0.022f + f * 0.0145f) + U * 0.004f, v3(0.0055f, 0.0075f, 0.014f), yw, gloveD, true);
    drawBox(h + R * (k ? -0.035f : 0.035f) + F * 0.012f, v3(0.007f, 0.008f, 0.014f), yw, gloveD, true);   // большой палец
  }
  switch (tool) {
    case T_HAND:
      for (int i = 0; i < carried; i++) {          // мешки с мусором в левой руке
        V3 bp = lh + U * (-0.06f - 0.035f * i) + F * (0.03f + 0.01f * i) + R * (0.02f * (i % 2));
        drawBox(bp, v3(0.045f, 0.04f, 0.04f), yw + i * 0.5f, RGB(22, 22, 26), true); drawBox(bp + U * 0.045f, v3(0.012f, 0.01f, 0.012f), yw, RGB(22, 22, 26), true);
        drawBox(bp + U * 0.056f, v3(0.016f, 0.003f, 0.004f), yw, RGB(230, 200, 40), false); }   // жёлтая завязка
      break;
    case T_MOP: {                                  // швабра: черенок, мопа из полосок
      V3 head = rh + F * 0.42f + U * -0.16f + R * (-0.08f + 0.05f * sw);
      rod(rh + U * 0.05f - F * 0.04f, head, 0.007f, yw, RGB(200, 170, 120));
      drawBox(head, v3(0.075f, 0.012f, 0.022f), yw, RGB(60, 120, 200), true);
      for (int i = 0; i < 9; i++) { float ox = -0.065f + i * 0.0163f; drawBox(head + R * (ox + 0.004f * sinf(t * 2 + i)) + U * -0.03f + F * (0.004f * (i % 3)), v3(0.0055f, 0.022f, 0.008f), yw, i % 2 ? RGB(230, 230, 235) : RGB(205, 210, 220), true); }
      if (working) drawBox(head + U * -0.055f + R * (0.05f * sw), v3(0.07f, 0.002f, 0.04f), yw, RGB(150, 190, 230), false);   // мокрый след
    } break;
    case T_SCRAPER: {                              // шпатель: жёлто-чёрная ручка, стальная шейка, широкое лезвие
      V3 tip = rh + F * 0.08f + U * 0.02f;
      drawBox(rh + F * 0.012f, v3(0.012f, 0.014f, 0.04f), yw, RGB(240, 190, 30), true);
      drawBox(rh + F * 0.012f + U * 0.0145f, v3(0.006f, 0.001f, 0.035f), yw, RGB(30, 30, 30), false);
      drawBox(rh + F * 0.06f + U * 0.01f, v3(0.008f, 0.004f, 0.016f), yw, RGB(170, 175, 185), true);
      drawBox(tip + F * 0.025f, v3(0.055f, 0.0025f, 0.032f), yw, RGB(205, 210, 220), true);
      drawBox(tip + F * 0.057f + U * 0.003f, v3(0.055f, 0.001f, 0.002f), yw, RGB(250, 250, 255), false);   // заточенный край
      if (working) for (int i = 0; i < 3; i++) drawBox(tip + F * (0.07f + 0.01f * i) + U * (0.01f + 0.015f * fabsf(sinf(t + i))) + R * (0.02f * (i - 1)), v3(0.015f, 0.002f, 0.012f), yw + i, RGB(200, 185, 135), false);   // стружка обоев
    } break;
    case T_PUTTY: {                                // широкий шпатель с шпаклёвкой и ведёрко в другой руке
      V3 tip = rh + F * 0.07f + U * (0.02f + 0.02f * sw);
      drawBox(rh + F * 0.01f, v3(0.012f, 0.012f, 0.035f), yw, RGB(40, 90, 200), true);
      drawBox(tip + F * 0.02f, v3(0.07f, 0.003f, 0.035f), yw, RGB(205, 210, 220), true);
      drawBox(tip + F * 0.02f + U * 0.006f, v3(0.05f, 0.006f, 0.022f), yw, RGB(245, 243, 238), true);           // шпаклёвка на шпателе
      drawBox(lh + U * -0.03f, v3(0.035f, 0.035f, 0.035f), yw, RGB(245, 245, 245), true);                        // ведёрко
      drawBox(lh + U * 0.006f, v3(0.03f, 0.002f, 0.03f), yw, RGB(232, 230, 224), false);
      drawBox(lh + U * -0.03f, v3(0.036f, 0.01f, 0.036f), yw, RGB(40, 90, 200), false);                         // этикетка
    } break;
    case T_PAPER: {                                // рулон обоев с узором и кисть с клеем
      const Paper &pp = PAPERS[paperSel];
      drawBox(lh + F * 0.03f + U * 0.02f, v3(0.09f, 0.03f, 0.03f), yw, pp.a, true);
      drawBox(lh + F * 0.03f + U * 0.02f, v3(0.09f, 0.031f, 0.012f), yw, pp.b, true);
      drawBox(lh + F * 0.03f + U * 0.02f + R * 0.092f, v3(0.003f, 0.02f, 0.02f), yw, RGB(200, 190, 170), true);   // торец рулона
      V3 br = rh + F * 0.08f + U * (0.03f + 0.04f * sw);
      drawBox(rh + F * 0.02f, v3(0.01f, 0.01f, 0.04f), yw, RGB(170, 120, 70), true);
      drawBox(br, v3(0.035f, 0.01f, 0.025f), yw, RGB(60, 60, 66), true);
      drawBox(br + F * 0.03f, v3(0.035f, 0.008f, 0.012f), yw, RGB(225, 220, 200), true);                          // щетина в клее
    } break;
    case T_ROLLER: {                               // валик: ручка, гнутая проволока, мягкий цилиндр в краске
      V3 grip = rh, bend = rh + F * 0.04f + U * 0.06f, axle = rh + F * 0.12f + U * 0.08f - R * 0.03f;
      drawBox(grip, v3(0.013f, 0.03f, 0.013f), yw, RGB(35, 35, 40), true);
      rod(grip + U * 0.03f, bend, 0.004f, yw, RGB(180, 185, 195));
      rod(bend, axle - R * 0.05f, 0.004f, yw, RGB(180, 185, 195));
      u32 pc = PAINT[paintSel], pd = mixc(pc, RGB(0, 0, 0), 0.25f);
      drawBox(axle, v3(0.05f, 0.024f, 0.014f), yw, pc, true);       // «круглый» валик: крест из двух брусков
      drawBox(axle, v3(0.05f, 0.014f, 0.024f), yw, pd, true);
      drawBox(axle + R * 0.052f, v3(0.003f, 0.018f, 0.018f), yw, RGB(60, 60, 66), true); drawBox(axle - R * 0.052f, v3(0.003f, 0.018f, 0.018f), yw, RGB(60, 60, 66), true);
      if (working) for (int i = 0; i < 2; i++) drawBox(axle + U * (-0.03f - 0.02f * fabsf(sinf(t * 0.7f + i))) + R * (0.02f * (i ? 1 : -1)), v3(0.003f, 0.006f, 0.003f), yw, pc, false);   // капли
    } break;
    case T_FLOOR: {
      bool tile = FLOORS[floorSel].tile; u32 fc = FLOORS[floorSel].a, fd = mixc(fc, RGB(0, 0, 0), 0.15f);
      if (tile) {                                  // плитка в левой руке, резиновая киянка в правой
        drawBox(lh + F * 0.03f + U * 0.02f, v3(0.045f, 0.004f, 0.045f), yw, fc, true);
        drawBox(lh + F * 0.03f + U * 0.025f, v3(0.044f, 0.001f, 0.001f), yw, RGB(170, 175, 185), false); drawBox(lh + F * 0.03f + U * 0.025f, v3(0.001f, 0.001f, 0.044f), yw, RGB(170, 175, 185), false);
        V3 hd = rh + F * 0.1f + U * (0.06f - 0.05f * fabsf(sw)); rod(rh, hd, 0.006f, yw, RGB(180, 140, 90));
        drawBox(hd, v3(0.03f, 0.018f, 0.018f), yw, RGB(30, 30, 34), true);
      } else {                                     // доска ламината
        drawBox(lh + F * 0.06f + R * 0.06f + U * 0.02f, v3(0.1f, 0.006f, 0.03f), yw, fc, true);
        for (int i = 0; i < 3; i++) drawBox(lh + F * 0.06f + R * (0.0f + 0.05f * i) + U * 0.027f, v3(0.012f, 0.0005f, 0.028f), yw, fd, false);
        V3 hd = rh + F * 0.1f + U * (0.06f - 0.05f * fabsf(sw)); rod(rh, hd, 0.006f, yw, RGB(180, 140, 90)); drawBox(hd, v3(0.03f, 0.018f, 0.018f), yw, RGB(200, 60, 40), true);
      }
    } break;
    case T_FURN:                                   // рулетка и каталог мебели
      drawBox(rh + F * 0.02f, v3(0.022f, 0.022f, 0.012f), yw, RGB(250, 200, 30), true);
      drawBox(rh + F * 0.02f + R * -0.023f, v3(0.002f, 0.004f, 0.006f), yw, RGB(30, 30, 30), true);
      drawBox(rh + F * 0.05f + R * -0.018f + U * -0.016f, v3(0.003f, 0.002f, 0.03f), yw, RGB(240, 240, 230), false);
      drawBox(lh + F * 0.03f + U * 0.02f, v3(0.04f, 0.003f, 0.05f), yw, RGB(240, 240, 245), true);
      drawBox(lh + F * 0.03f + U * 0.024f, v3(0.03f, 0.001f, 0.012f), yw, RGB(200, 140, 60), false);
      break;
  }
}

// ---------- рынок, сохранение ----------
struct Offer { int layout, price; unsigned seed; int cond, house, dist; };
struct OfferV5 { int layout, price; unsigned seed; };
static Offer offers[3];
enum { GS_TITLE, GS_MARKET, GS_FLAT, GS_SOLD };
static int gs = GS_TITLE; static bool hasSave = false;
static int soldPrice = 0, soldProfit = 0, soldBuyer = -1;
static int layoutCells(int id) { int n = 0; for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) if (LAYOUTS[id].rows[z][x] != ' ') n++; return n; }
static int layoutRooms(int id) {                 // сколько жилых комнат (отдельных областей «L»)
  bool v[MW][MH]; memset(v, 0, sizeof v); int n = 0;
  for (int z0 = 0; z0 < MH; z0++) for (int x0 = 0; x0 < MW; x0++) if (LAYOUTS[id].rows[z0][x0] == 'L' && !v[x0][z0]) { n++; int sx[MW * MH], sz[MW * MH], sp = 0; sx[sp] = x0; sz[sp++] = z0; v[x0][z0] = true;
    while (sp) { sp--; int x = sx[sp], z = sz[sp]; int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (int k = 0; k < 4; k++) { int nx = x + d[k][0], nz = z + d[k][1];
      if (nx >= 0 && nz >= 0 && nx < MW && nz < MH && !v[nx][nz] && LAYOUTS[id].rows[nz][nx] == 'L') { v[nx][nz] = true; sx[sp] = nx; sz[sp++] = nz; } } } }
  return n;
}
static int layoutPrice(int id) { long long a = layoutCells(id); return (int)(a * (36000 + a * 100)); }   // цена за клетку растёт вместе с площадью
static void genOffers() {
  // три РАЗНЫЕ по площади планировки; с каждой проданной квартирой открываются всё большие
  int bySize[NLAYOUT]; for (int i = 0; i < NLAYOUT; i++) bySize[i] = i;
  for (int a = 0; a < NLAYOUT; a++) for (int b = a + 1; b < NLAYOUT; b++) if (layoutCells(bySize[b]) < layoutCells(bySize[a])) { int t = bySize[a]; bySize[a] = bySize[b]; bySize[b] = t; }
  int maxL = imin(NLAYOUT - 1, 4 + flats);
  int pick[3]; pick[0] = rand() % (imin(flats < 3 ? 2 : maxL, maxL) + 1);       // в начале одна всегда маленькая и по карману
  for (int i = 1; i < 3; i++) { for (;;) { int c = rand() % (maxL + 1); bool dup = false; for (int k = 0; k < i; k++) if (pick[k] == c) dup = true; if (!dup) { pick[i] = c; break; } } }
  for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) if (pick[b] < pick[a]) { int t = pick[a]; pick[a] = pick[b]; pick[b] = t; }
  for (int i = 0; i < 3; i++) pick[i] = bySize[pick[i]];
  for (int i = 0; i < 3; i++) { Offer &o = offers[i]; o.layout = pick[i];
    int hr = rand() % 100; o.house = hr < 28 ? HO_KHRU : hr < 62 ? HO_PANEL : hr < 82 ? HO_STALIN : HO_NEW;
    int dr = rand() % 100; o.dist = dr < 25 ? DI_CENTER : dr < 72 ? DI_SLEEP : DI_EDGE;
    if (o.house == HO_STALIN && o.dist == DI_EDGE) o.dist = DI_SLEEP;            // сталинки - ближе к центру
    int cr = rand() % 100;
    if (o.house == HO_NEW) o.cond = cr < 65 ? CO_BARE : cr < 85 ? CO_FLOOD : CO_NORMAL;   // в новостройках - голый бетон или залили соседи
    else o.cond = cr < 34 ? CO_NORMAL : cr < 52 ? CO_FIRE : cr < 70 ? CO_FLOOD : CO_GRANNY;
    int area = layoutCells(o.layout);                                              // в хрущёвках - маленькие, в сталинках - просторные
    if (o.house == HO_KHRU && area > 75) o.house = HO_PANEL;
    if (o.house == HO_STALIN && area < 50) o.house = HO_KHRU;
    if (o.layout == 1 || o.layout == 9) o.house = HO_KHRU;                          // название говорит само за себя
    if (o.layout == 12) { o.house = HO_STALIN; if (o.dist == DI_EDGE) o.dist = DI_SLEEP; }
    if (o.house != HO_NEW && o.cond == CO_BARE) o.cond = CO_NORMAL;
    if (flats == 0 && i == 0) { o.house = HO_PANEL; o.dist = DI_SLEEP; o.cond = CO_NORMAL; }   // первая квартира - попроще и по карману
    o.price = (int)((long long)layoutPrice(o.layout) * (92 + rand() % 17) / 100 * HOUSEK[o.house] * DISTK[o.dist] * CONDK[o.cond] / 1000 * 1000); o.seed = (unsigned)rand() * 7u + 11u; }
}
static void saveGame() {
  FILE *f = fopen(pth("remont.sav"), "wb"); if (!f) return;
  int head[12] = {6, money, flats, owned, layoutId, flatBuy, spent, carried, (int)flatSeed, (int)trash.size(), entX, entZ};
  int head2[4] = {flatCond, flatHouse, flatDist, oldInit};
  fwrite(head, sizeof head, 1, f); fwrite(head2, sizeof head2, 1, f); fwrite(&px, sizeof px, 1, f); fwrite(&pz, sizeof pz, 1, f); fwrite(&yaw, sizeof yaw, 1, f);
  fwrite(cells, sizeof cells, 1, f); fwrite(fX, sizeof fX, 1, f); fwrite(fZ, sizeof fZ, 1, f);
  if (!trash.empty()) fwrite(&trash[0], sizeof(Trash), trash.size(), f);
  fwrite(offers, sizeof offers, 1, f); fclose(f);
}
static bool loadGame() {
  FILE *f = fopen(pth("remont.sav"), "rb"); if (!f) return false;
  int head[12]; bool ok = fread(head, sizeof head, 1, f) == 1 && head[0] >= 2 && head[0] <= 6;
  if (!ok) { fclose(f); return false; }
  money = head[1]; flats = head[2];
  if (head[0] < 5) {                                   // сохранение со старыми (маленькими) квартирами: возвращаем деньги за недоделанную
    owned = false; carried = 0; spent = 0;
    if (head[3]) money += head[5] + head[6];
    fclose(f); return true;
  }
  int head2[4] = {CO_NORMAL, HO_PANEL, DI_SLEEP, 0};                 // в 5-й версии этого не было: обычная панелька
  if (head[0] >= 6 && fread(head2, sizeof head2, 1, f) != 1) { fclose(f); return false; }
  flatCond = clampi(head2[0], 0, NCOND - 1); flatHouse = clampi(head2[1], 0, NHOUSE - 1); flatDist = clampi(head2[2], 0, NDIST - 1);
  owned = head[3]; layoutId = clampi(head[4], 0, NLAYOUT - 1); flatBuy = head[5]; flatSeed = (unsigned)head[8];
  if (owned) buildFlat(layoutId, flatSeed);
  oldInit = head2[3]; genBuyers();
  spent = head[6]; carried = head[7]; int nt = head[9];
  if (nt < 0 || nt > MW * MH) { fclose(f); owned = false; return false; }
  ok = fread(&px, sizeof px, 1, f) == 1 && fread(&pz, sizeof pz, 1, f) == 1 && fread(&yaw, sizeof yaw, 1, f) == 1;
  ok = ok && fread(cells, sizeof cells, 1, f) == 1 && fread(fX, sizeof fX, 1, f) == 1 && fread(fZ, sizeof fZ, 1, f) == 1;
  trash.resize(nt); if (nt) ok = ok && fread(&trash[0], sizeof(Trash), nt, f) == (size_t)nt;
  if (head[0] >= 6) ok = ok && fread(offers, sizeof offers, 1, f) == 1;
  else { OfferV5 o5[3]; ok = ok && fread(o5, sizeof o5, 1, f) == 1; for (int i = 0; i < 3; i++) { offers[i].layout = o5[i].layout; offers[i].price = o5[i].price; offers[i].seed = o5[i].seed; offers[i].cond = CO_NORMAL; offers[i].house = HO_PANEL; offers[i].dist = DI_SLEEP; } }
  for (int i = 0; i < 3; i++) { offers[i].layout = clampi(offers[i].layout, 0, NLAYOUT - 1); offers[i].cond = clampi(offers[i].cond, 0, NCOND - 1); offers[i].house = clampi(offers[i].house, 0, NHOUSE - 1); offers[i].dist = clampi(offers[i].dist, 0, NDIST - 1); }
  fclose(f); return ok;
}
static void buyFlat(int i) {
  Offer o = offers[i]; if (money < o.price) { say("НЕ ХВАТАЕТ ДЕНЕГ"); return; }
  money -= o.price; flatBuy = o.price; flatSeed = o.seed; flatCond = o.cond; flatHouse = o.house; flatDist = o.dist; buildFlat(o.layout, o.seed); genBuyers(); owned = true; memset(angX, 0, sizeof angX); memset(angZ, 0, sizeof angZ);
  px = entX + 0.5f; pz = entZ + 0.5f; yaw = 0; pitch = -0.15f; tool = T_HAND;
  for (int k = 0; k < 4; k++) { float a = k * 1.5708f; if (canStand(px + sinf(a) * 0.6f, pz + cosf(a) * 0.6f)) { yaw = a; break; } }
  static const char *HINT[NCOND] = {"КВАРТИРА ТВОЯ! УБЕРИ МУСОР, СДЕРИ ОБОИ, ПОКРАСЬ, ПОЛОЖИ ПОЛ, РАССТАВЬ МЕБЕЛЬ",
    "ПОСЛЕ ПОЖАРА: ОТМОЙ ГАРЬ, СДЕРИ ОБГОРЕВШИЕ ОБОИ, ЗАМЕНИ ПОЛ", "ПОСЛЕ ПОТОПА: СОБЕРИ ЛУЖИ, СДЕРИ ПЛЕСЕНЬ, ЗАМЕНИ ВЗДУТЫЙ ПОЛ",
    "БАБУШКИНА КВАРТИРА: ВЫНЕСИ СТАРУЮ МЕБЕЛЬ РУКАМИ", "БЕЗ ОТДЕЛКИ: ВЫНЕСИ СТРОЙМУСОР, ЗАШПАКЛЮЙ БЕТОН, ПОЛОЖИ ПОЛ"};
  gs = GS_FLAT; say(HINT[flatCond]); saveGame();
}
static void sellFlat(int buyer) {
  soldBuyer = buyer; soldPrice = buyerPrice(buyer); soldProfit = soldPrice - flatBuy - spent; money += soldPrice; flats++; owned = false; carried = 0;
  genOffers();
  { int need = imin(offers[0].price, imin(offers[1].price, offers[2].price)) + 450000; if (money < need) { money = need; say("РОДНЯ ПОМОГЛА ДЕНЬГАМИ НА НОВУЮ КВАРТИРУ"); } }
  gs = GS_SOLD; saveGame();
}
static void fmtR(char *b, int n, int v) { char t[32]; snprintf(t, sizeof t, "%d", v < 0 ? -v : v); int L = (int)strlen(t), k = 0; char o[48]; if (v < 0) o[k++] = '-'; for (int i = 0; i < L; i++) { o[k++] = t[i]; if ((L - i - 1) % 3 == 0 && i < L - 1) o[k++] = ' '; } o[k] = 0; snprintf(b, n, "%s Р", o); }

// ---------- сенсор: несколько пальцев (ходьба и обзор одновременно) ----------
struct Finger { long long id; int role, x, y, sx, sy; bool on; };   // роль: 0 джойстик, 1 обзор, 2 кнопка, 3 «делать»
static Finger fing[6];
static float joyX = 0, joyY = 0; static bool actHeld = false; static int lookDX = 0, lookDY = 0;
static int JR() { return imin(IW, IH) / 6; }
static int joyCX() { return JR() + fs * 6; }
static int joyCY() { return IH - JR() - fs * 6; }
static void actRect(int &x, int &y, int &w, int &h) { w = h = JR() * 2; x = IW - w - fs * 6; y = IH - h - fs * 6; }
static bool overUI(int x, int y);                // есть ли под пальцем кнопка интерфейса
static void fingerDown(long long id, int x, int y) {
  for (int i = 0; i < 6; i++) if (!fing[i].on) {
    Finger &f = fing[i]; f.on = true; f.id = id; f.x = f.sx = x; f.y = f.sy = y;
    int ax, ay, aw, ah; actRect(ax, ay, aw, ah);
    if (gs == GS_FLAT && (x - joyCX()) * (x - joyCX()) + (y - joyCY()) * (y - joyCY()) < JR() * JR() * 2) f.role = 0;
    else if (gs == GS_FLAT && x >= ax && y >= ay && x < ax + aw && y < ay + ah) { f.role = 3; actHeld = true; }
    else if (gs != GS_FLAT || overUI(x, y)) { f.role = 2; P.down = true; P.pressed = true; P.x = P.sx = x; P.y = P.sy = y; }
    else f.role = 1;
    return;
  }
}
static void fingerMove(long long id, int x, int y) {
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].id == id) { Finger &f = fing[i];
    if (f.role == 1) { lookDX += x - f.x; lookDY += y - f.y; }
    if (f.role == 2) { P.x = x; P.y = y; }
    f.x = x; f.y = y; }
}
static void fingerUp(long long id, int x, int y) {
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].id == id) { Finger &f = fing[i]; f.on = false;
    if (f.role == 2) { P.down = false; P.cx = x; P.cy = y; P.clicked = true; }
    if (f.role == 3) actHeld = false;
    if (f.role == 0) { joyX = joyY = 0; } }
}
static void updateJoy() {
  joyX = joyY = 0;
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].role == 0) { float dx = (float)(fing[i].x - joyCX()) / JR(), dy = (float)(fing[i].y - joyCY()) / JR(); float l = sqrtf(dx * dx + dy * dy); if (l > 1) { dx /= l; dy /= l; } joyX = dx; joyY = dy; }
}

// ---------- кадр в квартире ----------
static bool showTasks = false, showPick = false, showBuyers = false; static int furnTab = 0;
static std::vector<SDL_Rect> uiRects;
static bool overUI(int x, int y) { for (size_t i = 0; i < uiRects.size(); i++) { const SDL_Rect &r = uiRects[i]; if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return true; } return false; }
static bool ub(int x, int y, int w, int h, const char *l, bool en, u32 c) { SDL_Rect r = {x, y, w, h}; uiRects.push_back(r); return button(x, y, w, h, l, en, c); }
static void frameFlat(float dt) {
  if (msgT > 0) msgT -= dt;
  uiRects.clear();
  updateJoy();
  // ходьба и обзор
  float sp = 2.0f * dt, fx = sinf(yaw), fz = cosf(yaw);
  if (!showTasks && !showPick && !showBuyers) tryMove((fx * -joyY + fz * joyX) * sp, (fz * -joyY - fx * joyX) * sp);
  yaw += lookDX * 0.006f; pitch = clampf(pitch - lookDY * 0.005f, -1.25f, 1.0f); lookDX = lookDY = 0;
  // мусор сам выбрасывается у входной двери
  if (carried > 0 && (px - (entX + 0.5f)) * (px - (entX + 0.5f)) + (pz - (entZ + 0.5f)) * (pz - (entZ + 0.5f)) < 1.0f) { char b[64]; snprintf(b, sizeof b, "МУСОР ВЫНЕСЕН (%d)", carried); carried = 0; say(b); }
  aim = castAim();
  // работа: держишь «ДЕЛАТЬ» - прогресс
  float need; bool ok; char why[96]; const char *lbl = actionInfo(need, ok, why);
  working = actHeld && ok && !showTasks && !showPick && !showBuyers;
  if (working) { workT += dt; workBob += dt * 18; if (workT >= need) { doAction(); workT = 0; actHeld = tool == T_MOP || tool == T_SCRAPER || tool == T_ROLLER || tool == T_FLOOR ? actHeld : false; saveGame(); } }
  else workT = 0;
  // 3D
  setClipRect(0, 0, IW, IH); V3 e = eyePos(); setCamera(e, e + lookDir());
  focal = IW * 0.55f; vcx = IW * 0.5f; vcy = IH * 0.5f; fogCol = RGB(200, 215, 235); fogA = 1e9f; fogB = 2e9f; gLight = 1; gNight = 0; LDIR = vnorm(v3(0.35f, 0.85f, 0.4f));
  camTanX = (IW * 0.5f) / focal; camTanY = (IH * 0.5f) / focal; doorsUpdate(dt); gNearZ = 0.06f;
  fillSky(); clearDepth(); drawFlat(); drawHands();
  // прицел и подсказка
  fillRect(IW / 2 - fs * 3, IH / 2, fs * 6 + 1, 1, RGB(255, 255, 255)); fillRect(IW / 2, IH / 2 - fs * 3, 1, fs * 6 + 1, RGB(255, 255, 255));
  char b[128], m[32];
  if (working) { fillRect(IW / 2 - fs * 20, IH / 2 + fs * 8, fs * 40, fs * 3, RGB(30, 30, 30)); fillRect(IW / 2 - fs * 20, IH / 2 + fs * 8, (int)(fs * 40 * workT / need), fs * 3, RGB(120, 220, 120)); }
  else if (!ok && why[0] && aim.type != H_NONE) drawTextSh(IW / 2, IH / 2 + fs * 8, why, fs, RGB(255, 210, 160), 1);
  // верх: деньги, название, прогресс
  Prog pr = progress();
  fillRectA(0, 0, IW, fs * 20, RGB(14, 16, 22), 200);
  fmtR(m, sizeof m, money); drawTextSh(fs * 3, fs * 3, m, fs + 1, RGB(150, 240, 150), 0);
  snprintf(b, sizeof b, "%s  РЕМОНТ %d%%", LAYOUTS[layoutId].name, (int)(pr.k * 100)); drawTextSh(fs * 3 + textW("00 000 000 Р", fs + 1) + fs * 8, fs * 1 + 1, b, fs, RGB(255, 230, 160), 0);
  { snprintf(b, sizeof b, "%s, %s, %s", HOUSEN[flatHouse], DISTN[flatDist], CONDN[flatCond]); drawTextSh(fs * 3 + textW("00 000 000 Р", fs + 1) + fs * 8, fs * 11, b, fs, CONDC[flatCond], 0); }
  if (ub(IW - fs * 92, fs, fs * 44, fs * 11, "ЗАДАЧИ", true, RGB(70, 90, 130))) { showTasks = !showTasks; showBuyers = false; }
  if (ub(IW - fs * 46, fs, fs * 44, fs * 11, "ПРОДАТЬ", true, RGB(60, 140, 70))) { showBuyers = !showBuyers; showTasks = false; }
  // инструменты
  int tbw = imin(fs * 34, (IW - JR() * 4 - fs * 24) / NTOOL), tbx = IW / 2 - tbw * NTOOL / 2, tby = IH - fs * 16;
  for (int i = 0; i < NTOOL; i++) { char tl[48]; if (i == T_ROLLER) snprintf(tl, sizeof tl, "ВАЛИК\n%s", PAINTN[paintSel]); else if (i == T_FURN) snprintf(tl, sizeof tl, "МЕБЕЛЬ\n%s", FURN[furnSel].name); else if (i == T_PAPER) snprintf(tl, sizeof tl, "ОБОИ\n%s", PAPERS[paperSel].name); else if (i == T_FLOOR) snprintf(tl, sizeof tl, "ПОЛ\n%s", FLOORS[floorSel].name); else snprintf(tl, sizeof tl, "%s", TOOLN[i]);
    bool pick = i == T_ROLLER || i == T_FURN || i == T_PAPER || i == T_FLOOR;
    if (ub(tbx + i * tbw, tby, tbw - fs, fs * 15, tl, true, tool == i ? RGB(200, 140, 50) : RGB(60, 66, 80))) { if (tool == i && pick) showPick = true; else { tool = i; showPick = pick; } } }
  // джойстик и кнопка «делать»
  fillCircle(joyCX(), joyCY(), JR(), RGB(255, 255, 255), 40); fillCircle(joyCX() + (int)(joyX * JR() * 0.6f), joyCY() + (int)(joyY * JR() * 0.6f), JR() / 2, RGB(255, 255, 255), 90);
  int ax, ay, aw, ah; actRect(ax, ay, aw, ah); fillCircle(ax + aw / 2, ay + ah / 2, aw / 2, ok ? RGB(90, 200, 110) : RGB(120, 120, 130), actHeld ? 200 : 140);
  { int sc = fs; while (sc > 1 && textW(lbl, sc) > aw - fs * 4) sc--; drawText(ax + aw / 2, ay + ah / 2 - sc * 3, lbl, sc, RGB(255, 255, 255), 1); }
  if (carried) { snprintf(b, sizeof b, "МУСОР В РУКАХ: %d/4 (НЕСИ К ВХОДУ)", carried); drawTextSh(fs * 3, fs * 22, b, fs, RGB(255, 220, 120), 0); }
  // выбор краски или мебели
  if (showPick) {
    int pw = imin(IW - fs * 10, tool == T_FURN ? fs * 330 : fs * 260), ph = IH * 2 / 3 + fs * 8, x0 = IW / 2 - pw / 2, y0 = IH / 6 - fs * 2; fillRectA(x0 - fs * 2, y0 - fs * 2, pw + fs * 4, ph + fs * 4, RGB(14, 18, 26), 230);
    if (tool == T_ROLLER) { drawText(IW / 2, y0 + fs * 2, "ЦВЕТ КРАСКИ (600 Р ЗА КУСОК СТЕНЫ)", fs, RGB(255, 230, 160), 1);
      int cw = (pw - fs * 3) / 4, chh = (ph - fs * 30) / ((NPAINT + 2) / 4);
      for (int i = 1; i < NPAINT; i++) { int cx = x0 + ((i - 1) % 4) * (cw + fs), cy = y0 + fs * 12 + ((i - 1) / 4) * (chh + fs); fillRect(cx, cy, cw, chh - fs * 10, PAINT[i]); fillRect(cx, cy, fs * 3, fs * 3, STYLEC[PAINTS[i]]);
        if (ub(cx, cy + chh - fs * 10, cw, fs * 9, PAINTN[i], true, paintSel == i ? RGB(200, 140, 50) : RGB(60, 66, 80))) { paintSel = i; showPick = false; } } }
    else if (tool == T_FLOOR) { drawText(IW / 2, y0 + fs * 2, "ПОКРЫТИЕ ПОЛА (ЦЕНА ЗА КЛЕТКУ)", fs, RGB(255, 230, 160), 1);
      int cw = (pw - fs * 3) / 4, chh = (ph - fs * 30) / ((NFLOOR + 2) / 4);
      for (int i = 1; i < NFLOOR; i++) { int cx = x0 + ((i - 1) % 4) * (cw + fs), cy = y0 + fs * 12 + ((i - 1) / 4) * (chh + fs), sh = imax(fs * 4, chh - fs * 18);
        fillRect(cx, cy, cw, sh, FLOORS[i].a);                                    // образец
        if (FLOORS[i].tile) { fillRect(cx + cw / 2, cy, cw / 2, sh / 2, FLOORS[i].b); fillRect(cx, cy + sh / 2, cw / 2, sh / 2, FLOORS[i].b); }
        else for (int k = 1; k < 4; k++) fillRect(cx, cy + k * sh / 4, cw, 1 + fs / 3, FLOORS[i].b);
        fillRect(cx, cy, fs * 3, fs * 3, STYLEC[FLOORS[i].style]);
        fmtR(m, sizeof m, FLOORS[i].cost); snprintf(b, sizeof b, "%s\n%s", FLOORS[i].name, m);
        if (ub(cx, cy + sh, cw, imin(fs * 17, chh - sh), b, true, floorSel == i ? RGB(200, 140, 50) : RGB(60, 66, 80))) { floorSel = i; showPick = false; } } }
    else if (tool == T_PAPER) { drawText(IW / 2, y0 + fs * 2, "ОБОИ (900 Р ЗА ПОЛОСУ СТЕНЫ)", fs, RGB(255, 230, 160), 1);
      int cw = (pw - fs * 3) / 4, chh = (ph - fs * 30) / ((NPAPER + 2) / 4);
      for (int i = 1; i < NPAPER; i++) { int cx = x0 + ((i - 1) % 4) * (cw + fs), cy = y0 + fs * 12 + ((i - 1) / 4) * (chh + fs), sh = chh - fs * 10;
        fillRect(cx, cy, cw, sh, PAPERS[i].a);                                   // образец узора
        for (int k = 0; k < 4; k++) { if (PAPERS[i].kind == 0) fillRect(cx + k * cw / 4, cy, cw / 9, sh, PAPERS[i].b); else if (PAPERS[i].kind == 3) fillRect(cx, cy + k * sh / 4, cw, fs, PAPERS[i].b); else if (PAPERS[i].kind == 4) fillRect(cx + k * cw / 4, cy, fs / 2 + 1, sh, PAPERS[i].b); else for (int q = 0; q < 3; q++) fillRect(cx + cw / 8 + q * cw / 3 + (k % 2) * cw / 6, cy + sh / 8 + k * sh / 4, fs * 3, fs * 3, PAPERS[i].b); }
        fillRect(cx, cy, fs * 3, fs * 3, STYLEC[PAPERS[i].style]);
        if (ub(cx, cy + chh - fs * 10, cw, fs * 9, PAPERS[i].name, true, paperSel == i ? RGB(200, 140, 50) : RGB(60, 66, 80))) { paperSel = i; showPick = false; } } }
    else { drawText(IW / 2, y0 + fs * 2, "МЕБЕЛЬ (СТАВИТСЯ ЛИЦОМ К ТЕБЕ)", fs, RGB(255, 230, 160), 1);
      int tw = (pw - fs * 4) / NSTYLE;
      for (int t = 0; t < NSTYLE; t++) if (ub(x0 + t * (tw + fs), y0 + fs * 11, tw, fs * 10, STYLEN[t], true, furnTab == t ? mixc(STYLEC[t], RGB(255, 255, 255), 0.15f) : mixc(STYLEC[t], RGB(30, 34, 44), 0.6f))) furnTab = t;
      int ids[NFURN], nn = 0; for (int i = 1; i < NFURN; i++) if (!FURN[i].old && FURN[i].style == furnTab) ids[nn++] = i;
      int cols = 4, rows = (nn + cols - 1) / cols, cw = (pw - fs * 3) / cols, chh = imin(fs * 18, (ph - fs * 38) / imax(1, rows) - fs);
      for (int q = 0; q < nn; q++) { int i = ids[q], cx = x0 + (q % cols) * (cw + fs), cy = y0 + fs * 24 + (q / cols) * (chh + fs); fmtR(m, sizeof m, FURN[i].price);
        snprintf(b, sizeof b, "%s\n%s", FURN[i].name, m); if (ub(cx, cy, cw, chh, b, true, furnSel == i ? RGB(200, 140, 50) : RGB(60, 66, 80))) { furnSel = i; showPick = false; } } }
    if (ub(IW / 2 - fs * 30, y0 + ph - fs * 12, fs * 60, fs * 11, "ЗАКРЫТЬ", true, RGB(80, 86, 110))) showPick = false;
  }
  // список задач и продажа
  if (showTasks) {
    int pw = imin(IW - fs * 10, fs * 230), x0 = IW / 2 - pw / 2, y0 = IH / 8; fillRectA(x0 - fs * 3, y0 - fs * 3, pw + fs * 6, IH * 3 / 4 + fs * 6, RGB(14, 18, 26), 235);
    drawText(IW / 2, y0, "ЧТО ОСТАЛОСЬ СДЕЛАТЬ", fs + 1, RGB(255, 230, 160), 1); int y = y0 + fs * 14;
    struct { const char *n; int left, all; } T[8] = {{flatCond == CO_BARE ? "ВЫНЕСТИ СТРОИТЕЛЬНЫЙ МУСОР" : "ВЫНЕСТИ МУСОР", pr.trashLeft, pr.trashAll},
      {flatCond == CO_FIRE ? "ОТМЫТЬ ГАРЬ И ПЯТНА" : flatCond == CO_FLOOD ? "СОБРАТЬ ЛУЖИ И ОТМЫТЬ ПЯТНА" : "ОТМЫТЬ ПЯТНА", pr.stains, pr.stainsAll},
      {flatCond == CO_FIRE ? "СОДРАТЬ ОБГОРЕВШИЕ ОБОИ" : flatCond == CO_FLOOD ? "СОДРАТЬ ОБОИ С ПЛЕСЕНЬЮ" : "СОДРАТЬ СТАРЫЕ ОБОИ", pr.paper, pr.wallsAll},
      {flatCond == CO_BARE ? "ЗАШПАКЛЕВАТЬ БЕТОН" : "ЗАШПАКЛЕВАТЬ ТРЕЩИНЫ", pr.unputty, pr.wallsAll}, {"ПОКРАСИТЬ ИЛИ ПОКЛЕИТЬ ОБОИ", pr.unpainted, pr.wallsAll},
      {flatCond == CO_FIRE ? "ЗАМЕНИТЬ ОБГОРЕВШИЙ ПОЛ" : flatCond == CO_FLOOD ? "ЗАМЕНИТЬ ВЗДУТЫЙ ПОЛ" : flatCond == CO_BARE ? "ПОЛОЖИТЬ ПОЛ НА СТЯЖКУ" : "НОВЫЙ ПОЛ", pr.floorsOld, pr.floorsAll},
      {"НУЖНАЯ МЕБЕЛЬ (ДИВАН/КРОВАТЬ, ХОЛОДИЛЬНИК, ПЛИТА, ВАННА, РАКОВИНА, УНИТАЗ)", pr.furnNeed - pr.furnOk, pr.furnNeed},
      {"ВЫНЕСТИ СТАРУЮ МЕБЕЛЬ (РУКАМИ)", pr.oldLeft, pr.oldAll}};
    int nT = pr.oldAll ? 8 : 7;
    for (int i = 0; i < nT; i++) { if (i == 2 && flatCond == CO_BARE) continue; char tb[256]; snprintf(tb, sizeof tb, "%s: %s", T[i].n, T[i].left ? "" : "ГОТОВО"); char cnt[32]; if (T[i].left) { snprintf(cnt, sizeof cnt, "ОСТАЛОСЬ %d", T[i].left); strncat(tb, cnt, sizeof tb - strlen(tb) - 1); } char *b = tb;
      int sc = fs; while (sc > 1 && textW(b, sc) > pw - fs * 10) sc--; fillRect(x0, y + fs, fs * 4, fs * 4, T[i].left ? RGB(220, 120, 60) : RGB(80, 200, 100)); drawText(x0 + fs * 7, y, b, sc, RGB(230, 230, 235), 0); y += fs * 8; }
    { StyleInfo si = styleInfo(); if (si.best) { int bon = (int)(styleBonus(si) * pr.k * 100 + 0.5f); snprintf(b, sizeof b, "СТИЛЬ: %s %d%%%s", STYLEN[si.best], (int)(si.unity * 100), bon ? "" : " (ДЛЯ НАДБАВКИ НУЖНО БОЛЬШЕ 45%)");
        if (bon) { char t2[32]; snprintf(t2, sizeof t2, " (+%d%% К ЦЕНЕ)", bon); strncat(b, t2, sizeof b - strlen(b) - 1); } }
      else snprintf(b, sizeof b, "СТИЛЬ: НЕТ (ОТДЕЛКА И МЕБЕЛЬ ОДНОГО СТИЛЯ ДАЮТ НАДБАВКУ)");
      int sc = fs; while (sc > 1 && textW(b, sc) > pw) sc--; drawText(IW / 2, y + fs, b, sc, si.best ? STYLEC[si.best] : RGB(170, 170, 180), 1); y += fs * 9; } fmtR(m, sizeof m, flatBuy); char sp2[32]; fmtR(sp2, sizeof sp2, spent); snprintf(b, sizeof b, "КУПЛЕНА ЗА %s, ПОТРАЧЕНО НА РЕМОНТ %s", m, sp2); drawText(IW / 2, y, b, fs, RGB(200, 200, 210), 1); y += fs * 10;
    int sale = salePrice(); fmtR(m, sizeof m, sale); char pf[32]; fmtR(pf, sizeof pf, sale - flatBuy - spent); snprintf(b, sizeof b, "РЫНОЧНАЯ ЦЕНА: %s (ПРИБЫЛЬ %s)", m, pf); drawText(IW / 2, y, b, fs + 1, sale - flatBuy - spent >= 0 ? RGB(130, 240, 130) : RGB(255, 130, 110), 1); y += fs * 14;
    if (ub(x0, y, pw / 2 - fs, fs * 13, "К ПОКУПАТЕЛЯМ", true, RGB(60, 150, 70))) { showTasks = false; showBuyers = true; }
    if (ub(x0 + pw / 2 + fs, y, pw / 2 - fs, fs * 13, "РАБОТАТЬ ДАЛЬШЕ", true, RGB(80, 86, 110))) showTasks = false;
  }
  if (showBuyers) {                                                           // три покупателя: у каждого свои пожелания
    int pw = IW - fs * 8, x0 = IW / 2 - pw / 2, y0 = fs * 23, ph = IH - fs * 26;
    Prog p2 = progress(); StyleInfo si = styleInfo(); int cw = (pw - fs * 8) / 3, ch = imin(ph - fs * 28, fs * 92); ph = ch + fs * 30;
    fillRectA(x0 - fs * 3, y0 - fs * 3, pw + fs * 6, ph + fs * 4, RGB(14, 18, 26), 238); drawText(IW / 2, y0, "ПОКУПАТЕЛИ: ВЫПОЛНИ ПОЖЕЛАНИЯ - ЗАПЛАТЯТ БОЛЬШЕ", fs, RGB(255, 230, 160), 1);
    for (int i = 0; i < 3; i++) { int bu = buyers[i], cx = x0 + i * (cw + fs * 4), cy = y0 + fs * 10;
      fillRect(cx, cy, cw, ch, RGB(36, 42, 56)); fillRect(cx, cy, cw, fs * 12, RGB(52, 62, 84));
      { int sc = fs; while (sc > 1 && textW(BUYERN[bu], sc) > cw - fs * 4) sc--; drawText(cx + cw / 2, cy + fs * 3, BUYERN[bu], sc, RGB(255, 255, 255), 1); }
      int yy = cy + fs * 18;
      if (bu == BU_STUDENT) { drawText(cx + cw / 2, yy, "ПЛАТИТ МЕНЬШЕ,", imax(1, fs - 1), RGB(190, 190, 200), 1); drawText(cx + cw / 2, yy + fs * 6, "НО НЕ ПРИДИРАЕТСЯ", imax(1, fs - 1), RGB(190, 190, 200), 1); yy += fs * 14; }
      for (int w = 0; w < 3; w++) { const Wish &ws = WISHES[bu][w]; bool okw = wishOk(ws, p2, si);
        fillRect(cx + fs * 3, yy + fs, fs * 4, fs * 4, okw ? RGB(80, 200, 100) : RGB(220, 110, 70));
        char wt[64]; snprintf(wt, sizeof wt, "%s +%d%%", ws.text, (int)(ws.bonus * 100 + 0.5f)); int sc = fs; while (sc > 1 && textW(wt, sc) > cw - fs * 10) sc--;
        drawText(cx + fs * 9, yy, wt, sc, okw ? RGB(200, 245, 200) : RGB(235, 215, 200), 0); yy += fs * 11; }
      int pr2 = buyerPrice(bu), prof = pr2 - flatBuy - spent; fmtR(m, sizeof m, pr2);
      { int sc = fs + 1; while (sc > 1 && textW(m, sc) > cw - fs * 4) sc--; drawText(cx + cw / 2, cy + ch - fs * 34, m, sc, RGB(150, 240, 150), 1); }
      char pf2[32]; fmtR(pf2, sizeof pf2, prof); snprintf(b, sizeof b, "ПРИБЫЛЬ %s", pf2);
      { int sc = fs; while (sc > 1 && textW(b, sc) > cw - fs * 4) sc--; drawText(cx + cw / 2, cy + ch - fs * 21, b, sc, prof >= 0 ? RGB(130, 230, 130) : RGB(255, 130, 110), 1); }
      if (ub(cx + fs * 3, cy + ch - fs * 14, cw - fs * 6, fs * 12, "ПРОДАТЬ", true, RGB(60, 150, 70))) { showBuyers = false; sellFlat(bu); return; } }
    if (ub(IW / 2 - fs * 30, y0 + ph - fs * 15, fs * 60, fs * 12, "РАБОТАТЬ ДАЛЬШЕ", true, RGB(80, 86, 110))) showBuyers = false;
  }
  if (msgT > 0) { int sc = fs; while (sc > 1 && textW(msg, sc) > IW - fs * 10) sc--; int w = textW(msg, sc) + fs * 8; fillRectA(IW / 2 - w / 2, fs * 31, w, fs * 11, RGB(0, 0, 0), 170); drawText(IW / 2, fs * 34, msg, sc, RGB(255, 255, 255), 1); }
}
// ---------- рынок квартир ----------
static void miniMap(int id, int x0, int y0, int w, int h) {
  const Layout &L = LAYOUTS[id]; int ax = MW, az = MH, bx = 0, bz = 0;
  for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) if (L.rows[z][x] != ' ') { ax = imin(ax, x); az = imin(az, z); bx = imax(bx, x + 1); bz = imax(bz, z + 1); }
  int c = imax(1, imin(w / 16, h / 12)); u32 RC[NROOM] = {0, RGB(170, 160, 140), RGB(120, 160, 200), RGB(220, 180, 110), RGB(140, 200, 210)};   // общий масштаб: маленькая квартира и выглядит маленькой
  int ox = x0 + (w - c * (bx - ax)) / 2, oy = y0 + (h - c * (bz - az)) / 2;                                         // по центру карточки
  for (int z = az; z < bz; z++) for (int x = ax; x < bx; x++) { char ch = L.rows[z][x]; int r = ch == 'H' ? R_HALL : ch == 'L' ? R_LIV : ch == 'K' ? R_KIT : ch == 'B' ? R_BATH : 0; if (r) fillRect(ox + (x - ax) * c, oy + (z - az) * c, c > 2 ? c - 1 : c, c > 2 ? c - 1 : c, RC[r]); }
}
static void frameMarket() {
  fillRect(0, 0, IW, IH, RGB(28, 32, 40)); char b[96], m[32];
  drawTextSh(IW / 2, fs * 4, "КВАРТИРЫ НА ПРОДАЖУ", fs * 2, RGB(255, 220, 120), 1);
  fmtR(m, sizeof m, money); snprintf(b, sizeof b, "У ТЕБЯ %s   ПРОДАНО КВАРТИР: %d", m, flats); drawText(IW / 2, fs * 22, b, fs, RGB(150, 240, 150), 1);
  int cw = (IW - fs * 16) / 3, ch = IH - fs * 50, y0 = fs * 34;
  for (int i = 0; i < 3; i++) { Offer &o = offers[i]; int x0 = fs * 4 + i * (cw + fs * 4);
    fillRect(x0, y0, cw, ch, RGB(40, 46, 58)); drawText(x0 + cw / 2, y0 + fs * 4, LAYOUTS[o.layout].name, fs, RGB(255, 230, 160), 1);
    snprintf(b, sizeof b, "%s, %s", HOUSEN[o.house], DISTN[o.dist]); { int sc = fs; while (sc > 1 && textW(b, sc) > cw - fs * 4) sc--; drawText(x0 + cw / 2, y0 + fs * 13, b, sc, RGB(190, 200, 220), 1); }
    miniMap(o.layout, x0 + fs * 6, y0 + fs * 22, cw - fs * 12, imax(fs * 8, ch - fs * 62));
    fmtR(m, sizeof m, o.price); drawText(x0 + cw / 2, y0 + ch - fs * 37, m, fs + 1, RGB(150, 240, 150), 1);
    snprintf(b, sizeof b, "%d М2, %d КОМН.  %s", layoutCells(o.layout), layoutRooms(o.layout), CONDN[o.cond]); { int sc = fs; while (sc > 1 && textW(b, sc) > cw - fs * 4) sc--; drawText(x0 + cw / 2, y0 + ch - fs * 24, b, sc, CONDC[o.cond], 1); }
    if (button(x0 + fs * 4, y0 + ch - fs * 14, cw - fs * 8, fs * 12, "КУПИТЬ", money >= o.price, RGB(60, 150, 70))) buyFlat(i); }
  if (msgT > 0) { msgT -= 0.016f; drawText(IW / 2, IH - fs * 8, msg, fs, RGB(255, 200, 160), 1); }
}
static void frameSold() {
  fillRect(0, 0, IW, IH, RGB(28, 36, 30)); char b[96], m[32];
  drawTextSh(IW / 2, IH / 6, "КВАРТИРА ПРОДАНА!", fs * 2, RGB(140, 240, 140), 1);
  fmtR(m, sizeof m, soldPrice); snprintf(b, sizeof b, "ЦЕНА ПРОДАЖИ: %s", m); drawText(IW / 2, IH / 6 + fs * 28, b, fs + 1, RGB(230, 230, 235), 1);
  if (soldBuyer >= 0) { snprintf(b, sizeof b, "ПОКУПАТЕЛЬ: %s", BUYERN[soldBuyer]); drawText(IW / 2, IH / 6 + fs * 18, b, fs, RGB(200, 210, 230), 1); }
  fmtR(m, sizeof m, soldProfit); snprintf(b, sizeof b, "ПРИБЫЛЬ: %s", m); drawText(IW / 2, IH / 6 + fs * 41, b, fs + 2, soldProfit >= 0 ? RGB(140, 240, 140) : RGB(255, 130, 110), 1);
  if (button(IW / 2 - IW / 6, IH * 3 / 5, IW / 3, IH / 7, "К НОВЫМ КВАРТИРАМ", true, RGB(60, 150, 70))) gs = GS_MARKET;
  if (msgT > 0) { msgT -= 0.016f; drawText(IW / 2, IH - fs * 10, msg, fs, RGB(255, 220, 140), 1); }
}
static void frameTitle() {
  fillRect(0, 0, IW, IH, RGB(40, 44, 54));
  for (int i = 0; i < 12; i++) fillRect(i * IW / 12, IH * 2 / 3, IW / 12 - fs, IH / 3, i % 2 ? RGB(200, 185, 140) : RGB(170, 205, 230));   // «обои и краска»
  drawTextSh(IW / 2, IH / 8, "РЕМОНТ КВАРТИР", fs * 3, RGB(255, 210, 120), 1);
  drawText(IW / 2, IH / 8 + fs * 26, "КУПИ УБИТУЮ КВАРТИРУ, ОТРЕМОНТИРУЙ И ПРОДАЙ ДОРОЖЕ", fs, RGB(235, 235, 240), 1);
  int bw = IW / 3, bh = IH / 8, y = IH * 2 / 5;
  if (hasSave) { if (button(IW / 2 - bw / 2, y, bw, bh, "ПРОДОЛЖИТЬ", true, RGB(60, 150, 70))) gs = owned ? GS_FLAT : GS_MARKET; y += bh + fs * 3; }
  { char gb[96]; snprintf(gb, sizeof gb, gGL ? "ВИДЕОКАРТА: ВКЛ" : (gWantGL ? "ВИДЕОКАРТА: НЕ ЗАПУСТИЛАСЬ" : "ВИДЕОКАРТА: ВЫКЛ"));
    if (button(fs * 4, IH - fs * 16, IW / 3, fs * 12, gb, true, gGL ? RGB(60, 120, 170) : RGB(90, 90, 100))) {
      gWantGL = !gWantGL; FILE *g = fopen(pth("remont_gl.cfg"), "w"); if (g) { fprintf(g, "%d\n", gWantGL ? 1 : 0); fclose(g); }
      say(gWantGL ? "ВКЛЮЧЕНО: ПЕРЕЗАПУСТИ ИГРУ" : "ВЫКЛЮЧЕНО: ПЕРЕЗАПУСТИ ИГРУ"); } }
  if (msgT > 0) { msgT -= 0.016f; drawText(IW / 2, IH - fs * 24, msg, fs, RGB(255, 220, 140), 1); }
  if (button(IW / 2 - bw / 2, y, bw, bh, hasSave ? "НОВАЯ ИГРА" : "НАЧАТЬ", true, hasSave ? RGB(120, 80, 60) : RGB(60, 150, 70))) { money = 3600000; flats = 0; owned = false; genOffers(); gs = GS_MARKET; hasSave = true; saveGame(); }
}


// ---------- счётчик FPS и авто-чёткость ----------
static float fpsShow = 0, fpsAcc = 0, fpsWork = 0; static int fpsN = 0;
static void perfFrame(float dt, Uint32 workMs) {   // раз в 2 секунды: FPS; если кадр дольше 33 мс - меньше разрешение
  fpsAcc += dt; fpsWork += workMs; fpsN++;
  if (fpsAcc >= 2.0f) {
    fpsShow = fpsN / fpsAcc; float avg = fpsWork / fpsN;
    if (avg > 33.0f && quality - gResDrop > 0) { gResDrop++; computeSize(); allocBuffers(); }
    fpsAcc = fpsWork = 0; fpsN = 0;
  }
}
static void drawFps() {
  if (fpsShow <= 0) return; char b[32]; snprintf(b, sizeof b, "FPS %d", (int)(fpsShow + 0.5f));
  int sc = imax(1, fs - 1), w = textW(b, sc) + sc * 4;
  fillRectA(IW - w - fs, IH - sc * 12, w, sc * 10, RGB(0, 0, 0), 120);
  drawText(IW - w / 2 - fs, IH - sc * 10, b, sc, fpsShow >= 25 ? RGB(140, 240, 140) : fpsShow >= 15 ? RGB(255, 220, 110) : RGB(255, 120, 110), 1);
}
int main(int argc, char **argv) {
  (void)argc; (void)argv;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  { FILE *g = fopen(pth("remont_gl.cfg"), "r"); if (g) { int v = 1; if (fscanf(g, "%d", &v) == 1) gWantGL = v != 0; fclose(g); } }
  if (!gfxInit()) return 1;
  rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH;
  computeSize(); if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL));
  genOffers(); hasSave = loadGame();
  memset(&P, 0, sizeof P); memset(fing, 0, sizeof fing);
  bool running = true; Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    if (gfxCheckResize()) { rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH; computeSize(); if (!allocBuffers()) break; }
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
#if SDL_MODE == 2
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      int mx, my;
      switch (ev.type) {
        case SDL_QUIT: running = false; break;
        case SDL_APP_WILLENTERBACKGROUND: case SDL_APP_TERMINATING: case SDL_APP_LOWMEMORY: if (gs == GS_FLAT) saveGame(); break;
        case SDL_KEYDOWN: if (ev.key.keysym.sym == SDLK_ESCAPE) { if (gs == GS_FLAT && (showPick || showTasks || showBuyers)) { showPick = showTasks = showBuyers = false; } else if (gs == GS_TITLE) running = false; else { saveGame(); gs = GS_TITLE; } } break;
        case SDL_FINGERDOWN: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerDown((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERMOTION: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerMove((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERUP: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerUp((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_MOUSEBUTTONDOWN: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerDown(-1, mx, my); } break;
        case SDL_MOUSEMOTION: if (ev.motion.which != SDL_TOUCH_MOUSEID) { mapPt(ev.motion.x, ev.motion.y, mx, my); fingerMove(-1, mx, my); } break;
        case SDL_MOUSEBUTTONUP: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerUp(-1, mx, my); } break;
        default: break;
      }
    }
#else
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 4) { if (gs == GS_TITLE) running = false; else { saveGame(); gs = GS_TITLE; } }
      else if (e.type != 5) { int mx, my; mapPt(e.x, e.y, mx, my); if (e.type == 1) fingerDown(-1, mx, my); else if (e.type == 3) fingerMove(-1, mx, my); else fingerUp(-1, mx, my); }
    }
#endif
    if (gs == GS_TITLE) frameTitle(); else if (gs == GS_MARKET) frameMarket(); else if (gs == GS_SOLD) frameSold(); else frameFlat(dt);
    drawFps();
    gfxPresent();
    Uint32 spent2 = SDL_GetTicks() - now; { fpsAcc += dt; fpsN++; if (fpsAcc >= 2.0f) { fpsShow = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; } }
    // авто-скорость: если кадр рисуется дольше 33 мс (меньше 30 FPS), уменьшаем разрешение картинки
    { static float accT = 0, accW = 0; static int accN = 0; accT += dt; accW += spent2; accN++;
      if (accT > 2.0f) { float avg = accW / accN; if (!gGL && gs == GS_FLAT && avg > 33.0f && quality - gResDrop > 0) { gResDrop++; computeSize(); if (!allocBuffers()) break; } accT = accW = 0; accN = 0; } }
    if (spent2 < 16) SDL_Delay(16 - spent2);
  }
  if (gs == GS_FLAT) saveGame();
  gfxQuit();
  return 0;
}
