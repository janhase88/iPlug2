#ifndef _GL_CTX_
#define _GL_CTX_

#include "lice.h"
#include "../../IPlug/InstanceSeparation.h"

#define GLEW_STATIC
#include "glew/include/gl/glew.h"
#include "glew/include/gl/wglew.h"
#include "glew/include/gl/wglext.h"

#define MAX_CACHED_GLYPHS 4096

class LICE_GL_ctx
{
public:
  LICE_GL_ctx();
  ~LICE_GL_ctx();

  bool IsValid(); // GL context is initialized (will be lazy initialized on first call) and valid
  HWND GetWindow(); // Get the window that owns the GL context (one per process)
  void Close();  // Something failed, turn off GL context forever so we don't keep failing

  GLUnurbsObj* GetNurbsObj(int linetol = 8);  // linetol = maximum number of straight-line pixels

  // facility for associating a glyph with a texture
  int GetTexFromGlyph(const unsigned char* glyph, int glyph_w, int glyph_h);
  void ClearTex();

private:
  bool Init();

  bool m_init_tried;
  HINSTANCE m_gldll;
  HWND m_hwnd;
  HGLRC m_glrc;

  GLUnurbsObj* m_nurbs; // keep this here for easy reuse

  struct GlyphCache
  {
    unsigned int tex;
    unsigned char* glyph; // lives on the heap
    int glyph_w, glyph_h;
  };

  GlyphCache m_glyphCache[MAX_CACHED_GLYPHS];
  int m_nCachedGlyphs;
};

// GL context functions
// opening and managing GL context is handled behind the scenes

#if !IPLUG_SEPARATE_GL_CONTEXT
bool LICE_GL_IsValid(); // GL context is initialized (will be lazy initialized on first call) and valid

HWND LICE_GL_GetWindow(); // Get the window that owns the GL context (one per process)

void LICE_GL_CloseCtx();  // Something failed, turn off GL context forever so we don't keep failing

GLUnurbsObj* LICE_GL_GetNurbsObj(int linetol=8);  // linetol = maximum number of straight-line pixels

// facility for associating a glyph with a texture
GLuint LICE_GL_GetTexFromGlyph(const unsigned char* glyph, int glyph_w, int glyph_h);
void LICE_GL_ClearTex();
#endif

#endif
