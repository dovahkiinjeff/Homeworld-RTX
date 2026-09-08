#ifndef HW_LEGACY_D3D12_GL_H
#define HW_LEGACY_D3D12_GL_H

#include <stddef.h>

/*
 * OpenGL-free fixed-function compatibility front end.
 *
 * The Homeworld renderer still speaks the GL 1.x immediate/client-array
 * vocabulary in many subsystems.  In HW_ENABLE_D3D12_NATIVE_RASTER builds
 * these declarations are macro-mapped from the legacy gl* call sites to this
 * implementation; no OpenGL context or OpenGL import library is used.
 */

#ifdef __cplusplus
extern "C" {
#endif

void hwglAlphaFunc(GLenum func, GLclampf ref);
void hwglArrayElement(GLint i);
void hwglBegin(GLenum mode);
void hwglBindBuffer(GLenum target, GLuint buffer);
void hwglBindFramebuffer(GLenum target, GLuint framebuffer);
void hwglBindTexture(GLenum target, GLuint texture);
void hwglBlendFunc(GLenum sfactor, GLenum dfactor);
void hwglBufferData(GLenum target, ptrdiff_t size, const GLvoid *data, GLenum usage);
void hwglBufferSubData(GLenum target, ptrdiff_t offset, ptrdiff_t size, const GLvoid *data);
void hwglClear(GLbitfield mask);
void hwglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void hwglClearDepth(GLclampd depth);
void hwglClearDepthf(GLclampf depth);
void hwglClipPlane(GLenum plane, const GLdouble *equation);
void hwglClipPlanef(GLenum plane, const GLfloat *equation);
void hwglColor3f(GLfloat red, GLfloat green, GLfloat blue);
void hwglColor3ub(GLubyte red, GLubyte green, GLubyte blue);
void hwglColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void hwglColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void hwglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void hwglCullFace(GLenum mode);
void hwglDeleteBuffers(GLsizei n, const GLuint *buffers);
void hwglDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
void hwglDeleteTextures(GLsizei n, const GLuint *textures);
void hwglDepthFunc(GLenum func);
void hwglDepthMask(GLboolean flag);
void hwglDisable(GLenum cap);
void hwglDisableClientState(GLenum array);
void hwglDrawArrays(GLenum mode, GLint first, GLsizei count);
void hwglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void hwglDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void hwglEnable(GLenum cap);
void hwglEnableClientState(GLenum array);
void hwglEnd(void);
void hwglFlush(void);
/* Records the modern fullscreen mission environment in the native command
   stream. Returns zero when the texture/state cannot be represented safely. */
int hwglRecordMissionSky(GLuint texture, GLfloat fade);
/* Native-renderer bridge used by DXR environment lighting. The resource is
   borrowed; callers must not retain or release it. prepare-for-compute moves
   the texture into a combined pixel/non-pixel SRV state and updates the native
   renderer's tracked state so later mission-sky draws remain valid. */
void *hwglGetMissionSkyD3D12Resource(GLuint texture);
int hwglPrepareMissionSkyForCompute(GLuint texture, void *commandList);
void hwglFogf(GLenum pname, GLfloat param);
void hwglFogfv(GLenum pname, const GLfloat *params);
void hwglFogi(GLenum pname, GLint param);
void hwglFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void hwglFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void hwglFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void hwglGenBuffers(GLsizei n, GLuint *buffers);
void hwglGenFramebuffers(GLsizei n, GLuint *framebuffers);
void hwglGenTextures(GLsizei n, GLuint *textures);
void hwglGenerateMipmap(GLenum target);
void hwglGetBooleanv(GLenum pname, GLboolean *params);
GLenum hwglGetError(void);
void hwglGetFloatv(GLenum pname, GLfloat *params);
void hwglGetIntegerv(GLenum pname, GLint *params);
const GLubyte *hwglGetString(GLenum name);
void hwglGetTexEnviv(GLenum target, GLenum pname, GLint *params);
void hwglGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
GLboolean hwglIsEnabled(GLenum cap);
void hwglLightModelf(GLenum pname, GLfloat param);
void hwglLightModelfv(GLenum pname, const GLfloat *params);
void hwglLightfv(GLenum light, GLenum pname, const GLfloat *params);
void hwglLineStipple(GLint factor, GLushort pattern);
void hwglLineWidth(GLfloat width);
void hwglLoadIdentity(void);
void hwglLoadMatrixf(const GLfloat *m);
void hwglLockArrays(GLint first, GLsizei count);
void hwglLockArraysEXT(GLint first, GLsizei count);
void hwglMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void hwglMatrixMode(GLenum mode);
void hwglMultMatrixf(const GLfloat *m);
void hwglNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void hwglNormal3fv(const GLfloat *v);
void hwglNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
void hwglOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void hwglOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void hwglPixelStorei(GLenum pname, GLint param);
void hwglPointSize(GLfloat size);
void hwglPolygonMode(GLenum face, GLenum mode);
void hwglPushAttrib(GLbitfield mask);
void hwglPopAttrib(void);
void hwglPopMatrix(void);
void hwglPushMatrix(void);
void hwglRasterPos2f(GLfloat x, GLfloat y);
void hwglRasterPos2i(GLint x, GLint y);
void hwglReadBuffer(GLenum mode);
void hwglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void hwglRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void hwglScalef(GLfloat x, GLfloat y, GLfloat z);
void hwglScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void hwglShadeModel(GLenum mode);
void hwglTexCoord2f(GLfloat s, GLfloat t);
void hwglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void hwglTexEnvi(GLenum target, GLenum pname, GLint param);
void hwglTexImage2D(GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLint border,
                    GLenum format, GLenum type, const GLvoid *pixels);
void hwglTexParameterf(GLenum target, GLenum pname, GLfloat param);
void hwglTexParameteri(GLenum target, GLenum pname, GLint param);
void hwglTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                      GLsizei width, GLsizei height);
void hwglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLsizei width, GLsizei height, GLenum format, GLenum type,
                       const GLvoid *pixels);
void hwglTranslatef(GLfloat x, GLfloat y, GLfloat z);
void hwglUnlockArrays(void);
void hwglUnlockArraysEXT(void);
void hwglVertex2f(GLfloat x, GLfloat y);
void hwglVertex3f(GLfloat x, GLfloat y, GLfloat z);
void hwglVertex3fv(const GLfloat *v);
void hwglVertex4fv(const GLfloat *v);
void hwglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void hwglViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void hwglCompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                              GLsizei width, GLsizei height, GLint border,
                              GLsizei imageSize, const GLvoid *data);

/* Frame/stage integration used by ModernGraphics. */
enum HWLegacyRasterSnapshot
{
    HW_LEGACY_RASTER_BACKGROUND = 0,
    HW_LEGACY_RASTER_OCCLUDERS = 1,
    HW_LEGACY_RASTER_WORLD = 2,
    HW_LEGACY_RASTER_FINAL = 3
};
void hwglBeginFrame(unsigned int width, unsigned int height, unsigned int frameSlot);
void hwglMarkSnapshot(int snapshot);
size_t hwglSnapshotCommandCount(int snapshot);
void hwglEndFrame(void);
void hwglResetAll(void);

#ifdef __cplusplus
}
#endif

#endif
