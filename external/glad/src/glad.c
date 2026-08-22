#include "glad/glad.h"

#include <string.h>

PFNGLVIEWPORTPROC glad_glViewport = NULL;
PFNGLCLEARCOLORPROC glad_glClearColor = NULL;
PFNGLCLEARPROC glad_glClear = NULL;
PFNGLENABLEPROC glad_glEnable = NULL;
PFNGLDISABLEPROC glad_glDisable = NULL;
PFNGLDEPTHFUNCPROC glad_glDepthFunc = NULL;
PFNGLCULLFACEPROC glad_glCullFace = NULL;
PFNGLFRONTFACEPROC glad_glFrontFace = NULL;
PFNGLPOLYGONMODEPROC glad_glPolygonMode = NULL;
PFNGLBLENDFUNCPROC glad_glBlendFunc = NULL;
PFNGLLINEWIDTHPROC glad_glLineWidth = NULL;
PFNGLPOINTSIZEPROC glad_glPointSize = NULL;
PFNGLPIXELSTOREIPROC glad_glPixelStorei = NULL;
PFNGLDRAWBUFFERPROC glad_glDrawBuffer = NULL;
PFNGLREADBUFFERPROC glad_glReadBuffer = NULL;
PFNGLGETERRORPROC glad_glGetError = NULL;
PFNGLGETSTRINGPROC glad_glGetString = NULL;
PFNGLGETSTRINGIPROC glad_glGetStringi = NULL;
PFNGLGETINTEGERVPROC glad_glGetIntegerv = NULL;
PFNGLGETFLOATVPROC glad_glGetFloatv = NULL;

PFNGLCREATESHADERPROC glad_glCreateShader = NULL;
PFNGLSHADERSOURCEPROC glad_glShaderSource = NULL;
PFNGLCOMPILESHADERPROC glad_glCompileShader = NULL;
PFNGLGETSHADERIVPROC glad_glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog = NULL;
PFNGLDELETESHADERPROC glad_glDeleteShader = NULL;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glad_glAttachShader = NULL;
PFNGLDETACHSHADERPROC glad_glDetachShader = NULL;
PFNGLLINKPROGRAMPROC glad_glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog = NULL;
PFNGLUSEPROGRAMPROC glad_glUseProgram = NULL;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram = NULL;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = NULL;
PFNGLUNIFORM1IPROC glad_glUniform1i = NULL;
PFNGLUNIFORM1FPROC glad_glUniform1f = NULL;
PFNGLUNIFORM2FPROC glad_glUniform2f = NULL;
PFNGLUNIFORM3FPROC glad_glUniform3f = NULL;
PFNGLUNIFORM4FPROC glad_glUniform4f = NULL;
PFNGLUNIFORMMATRIX3FVPROC glad_glUniformMatrix3fv = NULL;
PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv = NULL;
PFNGLGETATTRIBLOCATIONPROC glad_glGetAttribLocation = NULL;
PFNGLBINDATTRIBLOCATIONPROC glad_glBindAttribLocation = NULL;

PFNGLGENBUFFERSPROC glad_glGenBuffers = NULL;
PFNGLBINDBUFFERPROC glad_glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glad_glBufferData = NULL;
PFNGLBUFFERSUBDATAPROC glad_glBufferSubData = NULL;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers = NULL;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray = NULL;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray = NULL;
PFNGLDRAWARRAYSPROC glad_glDrawArrays = NULL;
PFNGLDRAWELEMENTSPROC glad_glDrawElements = NULL;

PFNGLGENTEXTURESPROC glad_glGenTextures = NULL;
PFNGLBINDTEXTUREPROC glad_glBindTexture = NULL;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D = NULL;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri = NULL;
PFNGLTEXPARAMETERFPROC glad_glTexParameterf = NULL;
PFNGLGENERATEMIPMAPPROC glad_glGenerateMipmap = NULL;
PFNGLACTIVETEXTUREPROC glad_glActiveTexture = NULL;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures = NULL;

PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers = NULL;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus = NULL;
PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers = NULL;
PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers = NULL;
PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer = NULL;
PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage = NULL;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer = NULL;
PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers = NULL;

static void* load_required(GLADloadproc load, const char* name, int* ok) {
    void* ptr = load(name);
    if (!ptr) {
        *ok = 0;
    }
    return ptr;
}

int gladLoadGLLoader(GLADloadproc load) {
    int ok = 1;
    if (!load) {
        return 0;
    }

#define LOAD(var, name) (var = (void*)load_required(load, name, &ok))

    LOAD(glad_glViewport, "glViewport");
    LOAD(glad_glClearColor, "glClearColor");
    LOAD(glad_glClear, "glClear");
    LOAD(glad_glEnable, "glEnable");
    LOAD(glad_glDisable, "glDisable");
    LOAD(glad_glDepthFunc, "glDepthFunc");
    LOAD(glad_glCullFace, "glCullFace");
    LOAD(glad_glFrontFace, "glFrontFace");
    LOAD(glad_glPolygonMode, "glPolygonMode");
    LOAD(glad_glBlendFunc, "glBlendFunc");
    LOAD(glad_glLineWidth, "glLineWidth");
    LOAD(glad_glPointSize, "glPointSize");
    LOAD(glad_glPixelStorei, "glPixelStorei");
    LOAD(glad_glDrawBuffer, "glDrawBuffer");
    LOAD(glad_glReadBuffer, "glReadBuffer");
    LOAD(glad_glGetError, "glGetError");
    LOAD(glad_glGetString, "glGetString");
    LOAD(glad_glGetStringi, "glGetStringi");
    LOAD(glad_glGetIntegerv, "glGetIntegerv");
    LOAD(glad_glGetFloatv, "glGetFloatv");

    LOAD(glad_glCreateShader, "glCreateShader");
    LOAD(glad_glShaderSource, "glShaderSource");
    LOAD(glad_glCompileShader, "glCompileShader");
    LOAD(glad_glGetShaderiv, "glGetShaderiv");
    LOAD(glad_glGetShaderInfoLog, "glGetShaderInfoLog");
    LOAD(glad_glDeleteShader, "glDeleteShader");
    LOAD(glad_glCreateProgram, "glCreateProgram");
    LOAD(glad_glAttachShader, "glAttachShader");
    LOAD(glad_glDetachShader, "glDetachShader");
    LOAD(glad_glLinkProgram, "glLinkProgram");
    LOAD(glad_glGetProgramiv, "glGetProgramiv");
    LOAD(glad_glGetProgramInfoLog, "glGetProgramInfoLog");
    LOAD(glad_glUseProgram, "glUseProgram");
    LOAD(glad_glDeleteProgram, "glDeleteProgram");
    LOAD(glad_glGetUniformLocation, "glGetUniformLocation");
    LOAD(glad_glUniform1i, "glUniform1i");
    LOAD(glad_glUniform1f, "glUniform1f");
    LOAD(glad_glUniform2f, "glUniform2f");
    LOAD(glad_glUniform3f, "glUniform3f");
    LOAD(glad_glUniform4f, "glUniform4f");
    LOAD(glad_glUniformMatrix3fv, "glUniformMatrix3fv");
    LOAD(glad_glUniformMatrix4fv, "glUniformMatrix4fv");
    LOAD(glad_glGetAttribLocation, "glGetAttribLocation");
    LOAD(glad_glBindAttribLocation, "glBindAttribLocation");

    LOAD(glad_glGenBuffers, "glGenBuffers");
    LOAD(glad_glBindBuffer, "glBindBuffer");
    LOAD(glad_glBufferData, "glBufferData");
    LOAD(glad_glBufferSubData, "glBufferSubData");
    LOAD(glad_glDeleteBuffers, "glDeleteBuffers");
    LOAD(glad_glGenVertexArrays, "glGenVertexArrays");
    LOAD(glad_glBindVertexArray, "glBindVertexArray");
    LOAD(glad_glDeleteVertexArrays, "glDeleteVertexArrays");
    LOAD(glad_glVertexAttribPointer, "glVertexAttribPointer");
    LOAD(glad_glEnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(glad_glDisableVertexAttribArray, "glDisableVertexAttribArray");
    LOAD(glad_glDrawArrays, "glDrawArrays");
    LOAD(glad_glDrawElements, "glDrawElements");

    LOAD(glad_glGenTextures, "glGenTextures");
    LOAD(glad_glBindTexture, "glBindTexture");
    LOAD(glad_glTexImage2D, "glTexImage2D");
    LOAD(glad_glTexParameteri, "glTexParameteri");
    LOAD(glad_glTexParameterf, "glTexParameterf");
    LOAD(glad_glGenerateMipmap, "glGenerateMipmap");
    LOAD(glad_glActiveTexture, "glActiveTexture");
    LOAD(glad_glDeleteTextures, "glDeleteTextures");

    LOAD(glad_glGenFramebuffers, "glGenFramebuffers");
    LOAD(glad_glBindFramebuffer, "glBindFramebuffer");
    LOAD(glad_glFramebufferTexture2D, "glFramebufferTexture2D");
    LOAD(glad_glCheckFramebufferStatus, "glCheckFramebufferStatus");
    LOAD(glad_glDeleteFramebuffers, "glDeleteFramebuffers");
    LOAD(glad_glGenRenderbuffers, "glGenRenderbuffers");
    LOAD(glad_glBindRenderbuffer, "glBindRenderbuffer");
    LOAD(glad_glRenderbufferStorage, "glRenderbufferStorage");
    LOAD(glad_glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    LOAD(glad_glDeleteRenderbuffers, "glDeleteRenderbuffers");

#undef LOAD

    return ok;
}
