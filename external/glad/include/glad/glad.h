#ifndef __glad_h_
#define __glad_h_

/* Pre-empt the system/legacy GL headers (GL/gl.h uses the include guard
 * __gl_h_, GL/glext.h uses __glext_h_) so that anything which conditionally
 * `#include <GL/gl.h>` when it thinks no GL loader/header has been included
 * yet (e.g. GLFW's glfw3.h) ends up including an empty, already-guarded
 * file instead of one with declarations that conflict with the function
 * pointers/macros below. This mirrors the trick real generated GLAD headers
 * use for the same reason -- see README.md's "GL loader" section.
 */
#ifndef __gl_h_
#define __gl_h_
#endif
#ifndef __gl_glext_h_
#define __gl_glext_h_
#endif
#ifndef __glext_h_
#define __glext_h_
#endif
#ifndef __GL_H__
#define __GL_H__
#endif

/*
 * Minimal, hand-written OpenGL 3.3 core-profile function loader, exposing
 * the same public API shape as the real GLAD (gladLoadGL / gladLoadGLLoader,
 * GLADloadproc, functions accessed via plain gl* names) so application code
 * looks identical to what a real generated GLAD would produce.
 *
 * See README.md ("GL loader") for why this was hand-written instead of
 * vendoring a tool-generated GLAD for this phase, and what to do if a later
 * phase needs broader GL coverage.
 *
 * Covers: core state, shaders/programs, VAOs/VBOs/EBOs, textures,
 * framebuffers/renderbuffers -- enough for early engine phases (triangle,
 * shaders, camera/transform uniforms, basic textures, basic render targets).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "glad/khrplatform.h"

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GLAPI
#define GLAPI extern
#endif

typedef void GLvoid;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef khronos_int8_t GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef khronos_uint8_t GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef khronos_float_t GLfloat;
typedef khronos_float_t GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
typedef khronos_intptr_t GLintptr;
typedef khronos_ssize_t GLsizeiptr;

typedef void* (*GLADloadproc)(const char* name);

/* ---- enums (standard OpenGL / GL 3.3 core values) ---------------------- */

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006

#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406

#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505

#define GL_CW 0x0900
#define GL_CCW 0x0901

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408

#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_MULTISAMPLE 0x809D

// Queried via glGetIntegerv (both already declared/loaded below) to confirm
// a multisample-capable default framebuffer actually got created -- see
// Window's constructor (Phase 6): GLFW_SAMPLES is a window *hint*, not a
// guarantee, so the app checks the real GL_SAMPLES value it got back rather
// than assuming the hint took effect.
#define GL_SAMPLE_BUFFERS 0x80A8
#define GL_SAMPLES 0x80A9

#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C

#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F

#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3

#define GL_TEXTURE_2D 0x0DE1
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_RED 0x1903
#define GL_RG 0x8227
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

/* ---- function pointer typedefs ----------------------------------------- */

typedef void (APIENTRY* PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY* PFNGLCLEARCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY* PFNGLCLEARPROC)(GLbitfield);
typedef void (APIENTRY* PFNGLENABLEPROC)(GLenum);
typedef void (APIENTRY* PFNGLDISABLEPROC)(GLenum);
typedef void (APIENTRY* PFNGLDEPTHFUNCPROC)(GLenum);
typedef void (APIENTRY* PFNGLCULLFACEPROC)(GLenum);
typedef void (APIENTRY* PFNGLFRONTFACEPROC)(GLenum);
typedef void (APIENTRY* PFNGLPOLYGONMODEPROC)(GLenum, GLenum);
typedef void (APIENTRY* PFNGLBLENDFUNCPROC)(GLenum, GLenum);
typedef void (APIENTRY* PFNGLLINEWIDTHPROC)(GLfloat);
typedef void (APIENTRY* PFNGLPOINTSIZEPROC)(GLfloat);
typedef void (APIENTRY* PFNGLPIXELSTOREIPROC)(GLenum, GLint);
typedef GLenum(APIENTRY* PFNGLGETERRORPROC)(void);
typedef const GLubyte* (APIENTRY* PFNGLGETSTRINGPROC)(GLenum);
typedef const GLubyte* (APIENTRY* PFNGLGETSTRINGIPROC)(GLenum, GLuint);
typedef void (APIENTRY* PFNGLGETINTEGERVPROC)(GLenum, GLint*);

typedef GLuint(APIENTRY* PFNGLCREATESHADERPROC)(GLenum);
typedef void (APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (APIENTRY* PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY* PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint(APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY* PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (APIENTRY* PFNGLDETACHSHADERPROC)(GLuint, GLuint);
typedef void (APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (APIENTRY* PFNGLDELETEPROGRAMPROC)(GLuint);
typedef GLint(APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void (APIENTRY* PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (APIENTRY* PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void (APIENTRY* PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY* PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY* PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY* PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY* PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef GLint(APIENTRY* PFNGLGETATTRIBLOCATIONPROC)(GLuint, const GLchar*);
typedef void (APIENTRY* PFNGLBINDATTRIBLOCATIONPROC)(GLuint, GLuint, const GLchar*);

typedef void (APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (APIENTRY* PFNGLBUFFERSUBDATAPROC)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void (APIENTRY* PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (APIENTRY* PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint*);
typedef void (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRY* PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRY* PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
typedef void (APIENTRY* PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const void*);

typedef void (APIENTRY* PFNGLGENTEXTURESPROC)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNGLBINDTEXTUREPROC)(GLenum, GLuint);
typedef void (APIENTRY* PFNGLTEXIMAGE2DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (APIENTRY* PFNGLTEXPARAMETERIPROC)(GLenum, GLenum, GLint);
typedef void (APIENTRY* PFNGLGENERATEMIPMAPPROC)(GLenum);
typedef void (APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void (APIENTRY* PFNGLDELETETEXTURESPROC)(GLsizei, const GLuint*);

typedef void (APIENTRY* PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRY* PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void (APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void (APIENTRY* PFNGLGENRENDERBUFFERSPROC)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNGLBINDRENDERBUFFERPROC)(GLenum, GLuint);
typedef void (APIENTRY* PFNGLRENDERBUFFERSTORAGEPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY* PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void (APIENTRY* PFNGLDELETERENDERBUFFERSPROC)(GLsizei, const GLuint*);

/* ---- function pointer storage (defined in glad.c) ---------------------- */

GLAPI PFNGLVIEWPORTPROC glad_glViewport;
GLAPI PFNGLCLEARCOLORPROC glad_glClearColor;
GLAPI PFNGLCLEARPROC glad_glClear;
GLAPI PFNGLENABLEPROC glad_glEnable;
GLAPI PFNGLDISABLEPROC glad_glDisable;
GLAPI PFNGLDEPTHFUNCPROC glad_glDepthFunc;
GLAPI PFNGLCULLFACEPROC glad_glCullFace;
GLAPI PFNGLFRONTFACEPROC glad_glFrontFace;
GLAPI PFNGLPOLYGONMODEPROC glad_glPolygonMode;
GLAPI PFNGLBLENDFUNCPROC glad_glBlendFunc;
GLAPI PFNGLLINEWIDTHPROC glad_glLineWidth;
GLAPI PFNGLPOINTSIZEPROC glad_glPointSize;
GLAPI PFNGLPIXELSTOREIPROC glad_glPixelStorei;
GLAPI PFNGLGETERRORPROC glad_glGetError;
GLAPI PFNGLGETSTRINGPROC glad_glGetString;
GLAPI PFNGLGETSTRINGIPROC glad_glGetStringi;
GLAPI PFNGLGETINTEGERVPROC glad_glGetIntegerv;

GLAPI PFNGLCREATESHADERPROC glad_glCreateShader;
GLAPI PFNGLSHADERSOURCEPROC glad_glShaderSource;
GLAPI PFNGLCOMPILESHADERPROC glad_glCompileShader;
GLAPI PFNGLGETSHADERIVPROC glad_glGetShaderiv;
GLAPI PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
GLAPI PFNGLDELETESHADERPROC glad_glDeleteShader;
GLAPI PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
GLAPI PFNGLATTACHSHADERPROC glad_glAttachShader;
GLAPI PFNGLDETACHSHADERPROC glad_glDetachShader;
GLAPI PFNGLLINKPROGRAMPROC glad_glLinkProgram;
GLAPI PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
GLAPI PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
GLAPI PFNGLUSEPROGRAMPROC glad_glUseProgram;
GLAPI PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
GLAPI PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
GLAPI PFNGLUNIFORM1IPROC glad_glUniform1i;
GLAPI PFNGLUNIFORM1FPROC glad_glUniform1f;
GLAPI PFNGLUNIFORM2FPROC glad_glUniform2f;
GLAPI PFNGLUNIFORM3FPROC glad_glUniform3f;
GLAPI PFNGLUNIFORM4FPROC glad_glUniform4f;
GLAPI PFNGLUNIFORMMATRIX3FVPROC glad_glUniformMatrix3fv;
GLAPI PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv;
GLAPI PFNGLGETATTRIBLOCATIONPROC glad_glGetAttribLocation;
GLAPI PFNGLBINDATTRIBLOCATIONPROC glad_glBindAttribLocation;

GLAPI PFNGLGENBUFFERSPROC glad_glGenBuffers;
GLAPI PFNGLBINDBUFFERPROC glad_glBindBuffer;
GLAPI PFNGLBUFFERDATAPROC glad_glBufferData;
GLAPI PFNGLBUFFERSUBDATAPROC glad_glBufferSubData;
GLAPI PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
GLAPI PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
GLAPI PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
GLAPI PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;
GLAPI PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
GLAPI PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
GLAPI PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;
GLAPI PFNGLDRAWARRAYSPROC glad_glDrawArrays;
GLAPI PFNGLDRAWELEMENTSPROC glad_glDrawElements;

GLAPI PFNGLGENTEXTURESPROC glad_glGenTextures;
GLAPI PFNGLBINDTEXTUREPROC glad_glBindTexture;
GLAPI PFNGLTEXIMAGE2DPROC glad_glTexImage2D;
GLAPI PFNGLTEXPARAMETERIPROC glad_glTexParameteri;
GLAPI PFNGLGENERATEMIPMAPPROC glad_glGenerateMipmap;
GLAPI PFNGLACTIVETEXTUREPROC glad_glActiveTexture;
GLAPI PFNGLDELETETEXTURESPROC glad_glDeleteTextures;

GLAPI PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
GLAPI PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
GLAPI PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;
GLAPI PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus;
GLAPI PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers;
GLAPI PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers;
GLAPI PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer;
GLAPI PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage;
GLAPI PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;
GLAPI PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers;

/* ---- gl* macros, so call sites read like normal OpenGL code ------------ */

#define glViewport glad_glViewport
#define glClearColor glad_glClearColor
#define glClear glad_glClear
#define glEnable glad_glEnable
#define glDisable glad_glDisable
#define glDepthFunc glad_glDepthFunc
#define glCullFace glad_glCullFace
#define glFrontFace glad_glFrontFace
#define glPolygonMode glad_glPolygonMode
#define glBlendFunc glad_glBlendFunc
#define glLineWidth glad_glLineWidth
#define glPointSize glad_glPointSize
#define glPixelStorei glad_glPixelStorei
#define glGetError glad_glGetError
#define glGetString glad_glGetString
#define glGetStringi glad_glGetStringi
#define glGetIntegerv glad_glGetIntegerv

#define glCreateShader glad_glCreateShader
#define glShaderSource glad_glShaderSource
#define glCompileShader glad_glCompileShader
#define glGetShaderiv glad_glGetShaderiv
#define glGetShaderInfoLog glad_glGetShaderInfoLog
#define glDeleteShader glad_glDeleteShader
#define glCreateProgram glad_glCreateProgram
#define glAttachShader glad_glAttachShader
#define glDetachShader glad_glDetachShader
#define glLinkProgram glad_glLinkProgram
#define glGetProgramiv glad_glGetProgramiv
#define glGetProgramInfoLog glad_glGetProgramInfoLog
#define glUseProgram glad_glUseProgram
#define glDeleteProgram glad_glDeleteProgram
#define glGetUniformLocation glad_glGetUniformLocation
#define glUniform1i glad_glUniform1i
#define glUniform1f glad_glUniform1f
#define glUniform2f glad_glUniform2f
#define glUniform3f glad_glUniform3f
#define glUniform4f glad_glUniform4f
#define glUniformMatrix3fv glad_glUniformMatrix3fv
#define glUniformMatrix4fv glad_glUniformMatrix4fv
#define glGetAttribLocation glad_glGetAttribLocation
#define glBindAttribLocation glad_glBindAttribLocation

#define glGenBuffers glad_glGenBuffers
#define glBindBuffer glad_glBindBuffer
#define glBufferData glad_glBufferData
#define glBufferSubData glad_glBufferSubData
#define glDeleteBuffers glad_glDeleteBuffers
#define glGenVertexArrays glad_glGenVertexArrays
#define glBindVertexArray glad_glBindVertexArray
#define glDeleteVertexArrays glad_glDeleteVertexArrays
#define glVertexAttribPointer glad_glVertexAttribPointer
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glDisableVertexAttribArray glad_glDisableVertexAttribArray
#define glDrawArrays glad_glDrawArrays
#define glDrawElements glad_glDrawElements

#define glGenTextures glad_glGenTextures
#define glBindTexture glad_glBindTexture
#define glTexImage2D glad_glTexImage2D
#define glTexParameteri glad_glTexParameteri
#define glGenerateMipmap glad_glGenerateMipmap
#define glActiveTexture glad_glActiveTexture
#define glDeleteTextures glad_glDeleteTextures

#define glGenFramebuffers glad_glGenFramebuffers
#define glBindFramebuffer glad_glBindFramebuffer
#define glFramebufferTexture2D glad_glFramebufferTexture2D
#define glCheckFramebufferStatus glad_glCheckFramebufferStatus
#define glDeleteFramebuffers glad_glDeleteFramebuffers
#define glGenRenderbuffers glad_glGenRenderbuffers
#define glBindRenderbuffer glad_glBindRenderbuffer
#define glRenderbufferStorage glad_glRenderbufferStorage
#define glFramebufferRenderbuffer glad_glFramebufferRenderbuffer
#define glDeleteRenderbuffers glad_glDeleteRenderbuffers

/* ---- loader entry points ------------------------------------------------ */

/* Load every function pointer above using `load` (typically
 * glfwGetProcAddress). Returns non-zero on success, matching the real
 * GLAD's gladLoadGLLoader contract. */
int gladLoadGLLoader(GLADloadproc load);

#ifdef __cplusplus
}
#endif

#endif /* __glad_h_ */
