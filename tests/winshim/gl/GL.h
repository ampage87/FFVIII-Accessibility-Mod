// tests/winshim/gl/GL.h -- just enough of OpenGL 1.1 for a HOST SYNTAX CHECK of
// src/field_overlay.cpp.
//
// WHY: v0.62.3 and v0.63.3 both reached Aaron's compiler green, because a
// translation unit the host could not parse is a translation unit the host
// cannot check. field_overlay.cpp is the newest of those, and the one that
// draws to the screen -- the place a typo is least visible and most annoying.
// Never linked, never run: the point is that the file is PARSED.
#pragma once
typedef int            GLint;
typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef double         GLdouble;
typedef int            GLsizei;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
#define GL_NO_ERROR          0
#define GL_VIEWPORT          0x0BA2
#define GL_PROJECTION        0x1701
#define GL_MODELVIEW         0x1700
#define GL_DEPTH_TEST        0x0B71
#define GL_TEXTURE_2D        0x0DE1
#define GL_BLEND             0x0BE2
#define GL_LIGHTING          0x0B50
#define GL_SCISSOR_TEST      0x0C11
#define GL_STENCIL_TEST      0x0B90
#define GL_CULL_FACE         0x0B44
#define GL_ALPHA_TEST        0x0BC0
#define GL_ALL_ATTRIB_BITS   0x000FFFFF
#define GL_UNPACK_ALIGNMENT  0x0CF5
#define GL_PACK_ALIGNMENT    0x0D05
#define GL_RGB               0x1907
#define GL_RGBA              0x1908
#define GL_UNSIGNED_BYTE     0x1401
#define GL_QUADS             0x0007
#define GL_FOG               0x0B60
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S    0x2802
#define GL_TEXTURE_WRAP_T    0x2803
#define GL_NEAREST           0x2600
#define GL_VERSION           0x1F02
#define GL_RENDERER          0x1F01
typedef unsigned char GLubyte;
#ifndef APIENTRY
#define APIENTRY
#endif
inline void  glGetIntegerv(GLenum, GLint*) {}
inline void  glPushAttrib(GLbitfield) {}
inline void  glPopAttrib(void) {}
inline void  glMatrixMode(GLenum) {}
inline void  glPushMatrix(void) {}
inline void  glPopMatrix(void) {}
inline void  glLoadIdentity(void) {}
inline void  glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) {}
inline void  glEnable(GLenum) {}
inline void  glDisable(GLenum) {}
inline void  glPixelStorei(GLenum, GLint) {}
inline void  glPixelZoom(GLfloat, GLfloat) {}
inline void  glRasterPos2i(GLint, GLint) {}
inline void  glDrawPixels(GLsizei, GLsizei, GLenum, GLenum, const void*) {}
inline void  glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) {}
inline void  glColor4f(GLfloat, GLfloat, GLfloat, GLfloat) {}
inline void  glBegin(GLenum) {}
inline void  glEnd(void) {}
inline void  glVertex2i(GLint, GLint) {}
inline void  glViewport(GLint, GLint, GLsizei, GLsizei) {}
inline void  glGenTextures(GLsizei, GLuint*) {}
inline void  glDeleteTextures(GLsizei, const GLuint*) {}
inline void  glBindTexture(GLenum, GLuint) {}
inline void  glTexParameteri(GLenum, GLenum, GLint) {}
inline void  glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
inline void  glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) {}
inline void  glTexCoord2f(GLfloat, GLfloat) {}
inline const GLubyte* glGetString(GLenum) { return 0; }
inline GLenum glGetError(void) { return GL_NO_ERROR; }
#ifndef WINSHIM_HAS_WGL
#define WINSHIM_HAS_WGL 1
typedef int (*WINSHIM_PROC)();
inline WINSHIM_PROC wglGetProcAddress(const char*) { return 0; }
#endif
