// GENERATED from the gl*/egl* symbols switch-mesa's static libEGL.a/libGLESv2.a export
// (aarch64-none-elf-nm -g --defined-only, type T). Regenerate if switch-mesa is upgraded.
//
// Dawn's OpenGL ES backend resolves every EGL/GL entry point by name through a getProc callback
// (normally dlsym on libEGL.so). Switch has no dynamic linking, so this table is that callback:
// aurora passes aurora_switch_gl_get_proc via wgpu::RequestAdapterOptionsGetGLProc.
//
// Deliberately includes no GL/EGL header: each symbol is declared with a dummy prototype only so
// its address can be taken; callers cast back to the real signature, as with dlsym.
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern void eglBindAPI(void);
extern void eglBindTexImage(void);
extern void eglChooseConfig(void);
extern void eglClientWaitSync(void);
extern void eglCopyBuffers(void);
extern void eglCreateContext(void);
extern void eglCreateImage(void);
extern void eglCreatePbufferFromClientBuffer(void);
extern void eglCreatePbufferSurface(void);
extern void eglCreatePixmapSurface(void);
extern void eglCreatePlatformPixmapSurface(void);
extern void eglCreatePlatformWindowSurface(void);
extern void eglCreateSync(void);
extern void eglCreateWindowSurface(void);
extern void eglDestroyContext(void);
extern void eglDestroyImage(void);
extern void eglDestroySurface(void);
extern void eglDestroySync(void);
extern void eglGetConfigAttrib(void);
extern void eglGetConfigs(void);
extern void eglGetCurrentContext(void);
extern void eglGetCurrentDisplay(void);
extern void eglGetCurrentSurface(void);
extern void eglGetDisplay(void);
extern void eglGetError(void);
extern void eglGetPlatformDisplay(void);
extern void eglGetProcAddress(void);
extern void eglGetSyncAttrib(void);
extern void eglInitialize(void);
extern void eglMakeCurrent(void);
extern void eglQueryAPI(void);
extern void eglQueryContext(void);
extern void eglQueryString(void);
extern void eglQuerySurface(void);
extern void eglReleaseTexImage(void);
extern void eglReleaseThread(void);
extern void eglSurfaceAttrib(void);
extern void eglSwapBuffers(void);
extern void eglSwapInterval(void);
extern void eglTerminate(void);
extern void eglWaitClient(void);
extern void eglWaitGL(void);
extern void eglWaitNative(void);
extern void eglWaitSync(void);
extern void glActiveShaderProgram(void);
extern void glActiveTexture(void);
extern void glAttachShader(void);
extern void glBeginQuery(void);
extern void glBeginTransformFeedback(void);
extern void glBindAttribLocation(void);
extern void glBindBuffer(void);
extern void glBindBufferBase(void);
extern void glBindBufferRange(void);
extern void glBindFramebuffer(void);
extern void glBindImageTexture(void);
extern void glBindProgramPipeline(void);
extern void glBindRenderbuffer(void);
extern void glBindSampler(void);
extern void glBindTexture(void);
extern void glBindTransformFeedback(void);
extern void glBindVertexArray(void);
extern void glBindVertexBuffer(void);
extern void glBlendBarrier(void);
extern void glBlendColor(void);
extern void glBlendEquation(void);
extern void glBlendEquationi(void);
extern void glBlendEquationSeparate(void);
extern void glBlendEquationSeparatei(void);
extern void glBlendFunc(void);
extern void glBlendFunci(void);
extern void glBlendFuncSeparate(void);
extern void glBlendFuncSeparatei(void);
extern void glBlitFramebuffer(void);
extern void glBufferData(void);
extern void glBufferSubData(void);
extern void glCheckFramebufferStatus(void);
extern void glClear(void);
extern void glClearBufferfi(void);
extern void glClearBufferfv(void);
extern void glClearBufferiv(void);
extern void glClearBufferuiv(void);
extern void glClearColor(void);
extern void glClearDepthf(void);
extern void glClearStencil(void);
extern void glClientWaitSync(void);
extern void glColorMask(void);
extern void glColorMaski(void);
extern void glCompileShader(void);
extern void glCompressedTexImage2D(void);
extern void glCompressedTexImage3D(void);
extern void glCompressedTexSubImage2D(void);
extern void glCompressedTexSubImage3D(void);
extern void glCopyBufferSubData(void);
extern void glCopyImageSubData(void);
extern void glCopyTexImage2D(void);
extern void glCopyTexSubImage2D(void);
extern void glCopyTexSubImage3D(void);
extern void glCreateProgram(void);
extern void glCreateShader(void);
extern void glCreateShaderProgramv(void);
extern void glCullFace(void);
extern void glDebugMessageCallback(void);
extern void glDebugMessageControl(void);
extern void glDebugMessageInsert(void);
extern void glDeleteBuffers(void);
extern void glDeleteFramebuffers(void);
extern void glDeleteProgram(void);
extern void glDeleteProgramPipelines(void);
extern void glDeleteQueries(void);
extern void glDeleteRenderbuffers(void);
extern void glDeleteSamplers(void);
extern void glDeleteShader(void);
extern void glDeleteSync(void);
extern void glDeleteTextures(void);
extern void glDeleteTransformFeedbacks(void);
extern void glDeleteVertexArrays(void);
extern void glDepthFunc(void);
extern void glDepthMask(void);
extern void glDepthRangef(void);
extern void glDetachShader(void);
extern void glDisable(void);
extern void glDisablei(void);
extern void glDisableVertexAttribArray(void);
extern void glDispatchCompute(void);
extern void glDispatchComputeIndirect(void);
extern void glDrawArrays(void);
extern void glDrawArraysIndirect(void);
extern void glDrawArraysInstanced(void);
extern void glDrawBuffers(void);
extern void glDrawElements(void);
extern void glDrawElementsBaseVertex(void);
extern void glDrawElementsIndirect(void);
extern void glDrawElementsInstanced(void);
extern void glDrawElementsInstancedBaseVertex(void);
extern void glDrawRangeElements(void);
extern void glDrawRangeElementsBaseVertex(void);
extern void glEnable(void);
extern void glEnablei(void);
extern void glEnableVertexAttribArray(void);
extern void glEndQuery(void);
extern void glEndTransformFeedback(void);
extern void glFenceSync(void);
extern void glFinish(void);
extern void glFlush(void);
extern void glFlushMappedBufferRange(void);
extern void glFramebufferParameteri(void);
extern void glFramebufferRenderbuffer(void);
extern void glFramebufferTexture(void);
extern void glFramebufferTexture2D(void);
extern void glFramebufferTextureLayer(void);
extern void glFrontFace(void);
extern void glGenBuffers(void);
extern void glGenerateMipmap(void);
extern void glGenFramebuffers(void);
extern void glGenProgramPipelines(void);
extern void glGenQueries(void);
extern void glGenRenderbuffers(void);
extern void glGenSamplers(void);
extern void glGenTextures(void);
extern void glGenTransformFeedbacks(void);
extern void glGenVertexArrays(void);
extern void glGetActiveAttrib(void);
extern void glGetActiveUniform(void);
extern void glGetActiveUniformBlockiv(void);
extern void glGetActiveUniformBlockName(void);
extern void glGetActiveUniformsiv(void);
extern void glGetAttachedShaders(void);
extern void glGetAttribLocation(void);
extern void glGetBooleani_v(void);
extern void glGetBooleanv(void);
extern void glGetBufferParameteri64v(void);
extern void glGetBufferParameteriv(void);
extern void glGetBufferPointerv(void);
extern void glGetDebugMessageLog(void);
extern void glGetError(void);
extern void glGetFloatv(void);
extern void glGetFragDataLocation(void);
extern void glGetFramebufferAttachmentParameteriv(void);
extern void glGetFramebufferParameteriv(void);
extern void glGetGraphicsResetStatus(void);
extern void glGetInteger64i_v(void);
extern void glGetInteger64v(void);
extern void glGetIntegeri_v(void);
extern void glGetIntegerv(void);
extern void glGetInternalformativ(void);
extern void glGetMultisamplefv(void);
extern void glGetnUniformfv(void);
extern void glGetnUniformiv(void);
extern void glGetnUniformuiv(void);
extern void glGetObjectLabel(void);
extern void glGetObjectPtrLabel(void);
extern void glGetPointerv(void);
extern void glGetProgramBinary(void);
extern void glGetProgramInfoLog(void);
extern void glGetProgramInterfaceiv(void);
extern void glGetProgramiv(void);
extern void glGetProgramPipelineInfoLog(void);
extern void glGetProgramPipelineiv(void);
extern void glGetProgramResourceIndex(void);
extern void glGetProgramResourceiv(void);
extern void glGetProgramResourceLocation(void);
extern void glGetProgramResourceName(void);
extern void glGetQueryiv(void);
extern void glGetQueryObjectuiv(void);
extern void glGetRenderbufferParameteriv(void);
extern void glGetSamplerParameterfv(void);
extern void glGetSamplerParameterIiv(void);
extern void glGetSamplerParameterIuiv(void);
extern void glGetSamplerParameteriv(void);
extern void glGetShaderInfoLog(void);
extern void glGetShaderiv(void);
extern void glGetShaderPrecisionFormat(void);
extern void glGetShaderSource(void);
extern void glGetString(void);
extern void glGetStringi(void);
extern void glGetSynciv(void);
extern void glGetTexLevelParameterfv(void);
extern void glGetTexLevelParameteriv(void);
extern void glGetTexParameterfv(void);
extern void glGetTexParameterIiv(void);
extern void glGetTexParameterIuiv(void);
extern void glGetTexParameteriv(void);
extern void glGetTransformFeedbackVarying(void);
extern void glGetUniformBlockIndex(void);
extern void glGetUniformfv(void);
extern void glGetUniformIndices(void);
extern void glGetUniformiv(void);
extern void glGetUniformLocation(void);
extern void glGetUniformuiv(void);
extern void glGetVertexAttribfv(void);
extern void glGetVertexAttribIiv(void);
extern void glGetVertexAttribIuiv(void);
extern void glGetVertexAttribiv(void);
extern void glGetVertexAttribPointerv(void);
extern void glHint(void);
extern void glInvalidateFramebuffer(void);
extern void glInvalidateSubFramebuffer(void);
extern void glIsBuffer(void);
extern void glIsEnabled(void);
extern void glIsEnabledi(void);
extern void glIsFramebuffer(void);
extern void glIsProgram(void);
extern void glIsProgramPipeline(void);
extern void glIsQuery(void);
extern void glIsRenderbuffer(void);
extern void glIsSampler(void);
extern void glIsShader(void);
extern void glIsSync(void);
extern void glIsTexture(void);
extern void glIsTransformFeedback(void);
extern void glIsVertexArray(void);
extern void glLineWidth(void);
extern void glLinkProgram(void);
extern void glMapBufferRange(void);
extern void glMemoryBarrier(void);
extern void glMemoryBarrierByRegion(void);
extern void glMinSampleShading(void);
extern void glObjectLabel(void);
extern void glObjectPtrLabel(void);
extern void glPatchParameteri(void);
extern void glPauseTransformFeedback(void);
extern void glPixelStorei(void);
extern void glPolygonOffset(void);
extern void glPopDebugGroup(void);
extern void glPrimitiveBoundingBox(void);
extern void glProgramBinary(void);
extern void glProgramParameteri(void);
extern void glProgramUniform1f(void);
extern void glProgramUniform1fv(void);
extern void glProgramUniform1i(void);
extern void glProgramUniform1iv(void);
extern void glProgramUniform1ui(void);
extern void glProgramUniform1uiv(void);
extern void glProgramUniform2f(void);
extern void glProgramUniform2fv(void);
extern void glProgramUniform2i(void);
extern void glProgramUniform2iv(void);
extern void glProgramUniform2ui(void);
extern void glProgramUniform2uiv(void);
extern void glProgramUniform3f(void);
extern void glProgramUniform3fv(void);
extern void glProgramUniform3i(void);
extern void glProgramUniform3iv(void);
extern void glProgramUniform3ui(void);
extern void glProgramUniform3uiv(void);
extern void glProgramUniform4f(void);
extern void glProgramUniform4fv(void);
extern void glProgramUniform4i(void);
extern void glProgramUniform4iv(void);
extern void glProgramUniform4ui(void);
extern void glProgramUniform4uiv(void);
extern void glProgramUniformMatrix2fv(void);
extern void glProgramUniformMatrix2x3fv(void);
extern void glProgramUniformMatrix2x4fv(void);
extern void glProgramUniformMatrix3fv(void);
extern void glProgramUniformMatrix3x2fv(void);
extern void glProgramUniformMatrix3x4fv(void);
extern void glProgramUniformMatrix4fv(void);
extern void glProgramUniformMatrix4x2fv(void);
extern void glProgramUniformMatrix4x3fv(void);
extern void glPushDebugGroup(void);
extern void glReadBuffer(void);
extern void glReadnPixels(void);
extern void glReadPixels(void);
extern void glReleaseShaderCompiler(void);
extern void glRenderbufferStorage(void);
extern void glRenderbufferStorageMultisample(void);
extern void glResumeTransformFeedback(void);
extern void glSampleCoverage(void);
extern void glSampleMaski(void);
extern void glSamplerParameterf(void);
extern void glSamplerParameterfv(void);
extern void glSamplerParameteri(void);
extern void glSamplerParameterIiv(void);
extern void glSamplerParameterIuiv(void);
extern void glSamplerParameteriv(void);
extern void glScissor(void);
extern void glShaderBinary(void);
extern void glShaderSource(void);
extern void glStencilFunc(void);
extern void glStencilFuncSeparate(void);
extern void glStencilMask(void);
extern void glStencilMaskSeparate(void);
extern void glStencilOp(void);
extern void glStencilOpSeparate(void);
extern void glTexBuffer(void);
extern void glTexBufferRange(void);
extern void glTexImage2D(void);
extern void glTexImage3D(void);
extern void glTexParameterf(void);
extern void glTexParameterfv(void);
extern void glTexParameteri(void);
extern void glTexParameterIiv(void);
extern void glTexParameterIuiv(void);
extern void glTexParameteriv(void);
extern void glTexStorage2D(void);
extern void glTexStorage2DMultisample(void);
extern void glTexStorage3D(void);
extern void glTexStorage3DMultisample(void);
extern void glTexSubImage2D(void);
extern void glTexSubImage3D(void);
extern void glTransformFeedbackVaryings(void);
extern void glUniform1f(void);
extern void glUniform1fv(void);
extern void glUniform1i(void);
extern void glUniform1iv(void);
extern void glUniform1ui(void);
extern void glUniform1uiv(void);
extern void glUniform2f(void);
extern void glUniform2fv(void);
extern void glUniform2i(void);
extern void glUniform2iv(void);
extern void glUniform2ui(void);
extern void glUniform2uiv(void);
extern void glUniform3f(void);
extern void glUniform3fv(void);
extern void glUniform3i(void);
extern void glUniform3iv(void);
extern void glUniform3ui(void);
extern void glUniform3uiv(void);
extern void glUniform4f(void);
extern void glUniform4fv(void);
extern void glUniform4i(void);
extern void glUniform4iv(void);
extern void glUniform4ui(void);
extern void glUniform4uiv(void);
extern void glUniformBlockBinding(void);
extern void glUniformMatrix2fv(void);
extern void glUniformMatrix2x3fv(void);
extern void glUniformMatrix2x4fv(void);
extern void glUniformMatrix3fv(void);
extern void glUniformMatrix3x2fv(void);
extern void glUniformMatrix3x4fv(void);
extern void glUniformMatrix4fv(void);
extern void glUniformMatrix4x2fv(void);
extern void glUniformMatrix4x3fv(void);
extern void glUnmapBuffer(void);
extern void glUseProgram(void);
extern void glUseProgramStages(void);
extern void glValidateProgram(void);
extern void glValidateProgramPipeline(void);
extern void glVertexAttrib1f(void);
extern void glVertexAttrib1fv(void);
extern void glVertexAttrib2f(void);
extern void glVertexAttrib2fv(void);
extern void glVertexAttrib3f(void);
extern void glVertexAttrib3fv(void);
extern void glVertexAttrib4f(void);
extern void glVertexAttrib4fv(void);
extern void glVertexAttribBinding(void);
extern void glVertexAttribDivisor(void);
extern void glVertexAttribFormat(void);
extern void glVertexAttribI4i(void);
extern void glVertexAttribI4iv(void);
extern void glVertexAttribI4ui(void);
extern void glVertexAttribI4uiv(void);
extern void glVertexAttribIFormat(void);
extern void glVertexAttribIPointer(void);
extern void glVertexAttribPointer(void);
extern void glVertexBindingDivisor(void);
extern void glViewport(void);
extern void glWaitSync(void);

typedef struct { const char* name; void (*proc)(void); } ProcEntry;

// Sorted by name for bsearch.
static const ProcEntry kProcs[] = {
    {"eglBindAPI", eglBindAPI},
    {"eglBindTexImage", eglBindTexImage},
    {"eglChooseConfig", eglChooseConfig},
    {"eglClientWaitSync", eglClientWaitSync},
    {"eglCopyBuffers", eglCopyBuffers},
    {"eglCreateContext", eglCreateContext},
    {"eglCreateImage", eglCreateImage},
    {"eglCreatePbufferFromClientBuffer", eglCreatePbufferFromClientBuffer},
    {"eglCreatePbufferSurface", eglCreatePbufferSurface},
    {"eglCreatePixmapSurface", eglCreatePixmapSurface},
    {"eglCreatePlatformPixmapSurface", eglCreatePlatformPixmapSurface},
    {"eglCreatePlatformWindowSurface", eglCreatePlatformWindowSurface},
    {"eglCreateSync", eglCreateSync},
    {"eglCreateWindowSurface", eglCreateWindowSurface},
    {"eglDestroyContext", eglDestroyContext},
    {"eglDestroyImage", eglDestroyImage},
    {"eglDestroySurface", eglDestroySurface},
    {"eglDestroySync", eglDestroySync},
    {"eglGetConfigAttrib", eglGetConfigAttrib},
    {"eglGetConfigs", eglGetConfigs},
    {"eglGetCurrentContext", eglGetCurrentContext},
    {"eglGetCurrentDisplay", eglGetCurrentDisplay},
    {"eglGetCurrentSurface", eglGetCurrentSurface},
    {"eglGetDisplay", eglGetDisplay},
    {"eglGetError", eglGetError},
    {"eglGetPlatformDisplay", eglGetPlatformDisplay},
    {"eglGetProcAddress", eglGetProcAddress},
    {"eglGetSyncAttrib", eglGetSyncAttrib},
    {"eglInitialize", eglInitialize},
    {"eglMakeCurrent", eglMakeCurrent},
    {"eglQueryAPI", eglQueryAPI},
    {"eglQueryContext", eglQueryContext},
    {"eglQueryString", eglQueryString},
    {"eglQuerySurface", eglQuerySurface},
    {"eglReleaseTexImage", eglReleaseTexImage},
    {"eglReleaseThread", eglReleaseThread},
    {"eglSurfaceAttrib", eglSurfaceAttrib},
    {"eglSwapBuffers", eglSwapBuffers},
    {"eglSwapInterval", eglSwapInterval},
    {"eglTerminate", eglTerminate},
    {"eglWaitClient", eglWaitClient},
    {"eglWaitGL", eglWaitGL},
    {"eglWaitNative", eglWaitNative},
    {"eglWaitSync", eglWaitSync},
    {"glActiveShaderProgram", glActiveShaderProgram},
    {"glActiveTexture", glActiveTexture},
    {"glAttachShader", glAttachShader},
    {"glBeginQuery", glBeginQuery},
    {"glBeginTransformFeedback", glBeginTransformFeedback},
    {"glBindAttribLocation", glBindAttribLocation},
    {"glBindBuffer", glBindBuffer},
    {"glBindBufferBase", glBindBufferBase},
    {"glBindBufferRange", glBindBufferRange},
    {"glBindFramebuffer", glBindFramebuffer},
    {"glBindImageTexture", glBindImageTexture},
    {"glBindProgramPipeline", glBindProgramPipeline},
    {"glBindRenderbuffer", glBindRenderbuffer},
    {"glBindSampler", glBindSampler},
    {"glBindTexture", glBindTexture},
    {"glBindTransformFeedback", glBindTransformFeedback},
    {"glBindVertexArray", glBindVertexArray},
    {"glBindVertexBuffer", glBindVertexBuffer},
    {"glBlendBarrier", glBlendBarrier},
    {"glBlendColor", glBlendColor},
    {"glBlendEquation", glBlendEquation},
    {"glBlendEquationSeparate", glBlendEquationSeparate},
    {"glBlendEquationSeparatei", glBlendEquationSeparatei},
    {"glBlendEquationi", glBlendEquationi},
    {"glBlendFunc", glBlendFunc},
    {"glBlendFuncSeparate", glBlendFuncSeparate},
    {"glBlendFuncSeparatei", glBlendFuncSeparatei},
    {"glBlendFunci", glBlendFunci},
    {"glBlitFramebuffer", glBlitFramebuffer},
    {"glBufferData", glBufferData},
    {"glBufferSubData", glBufferSubData},
    {"glCheckFramebufferStatus", glCheckFramebufferStatus},
    {"glClear", glClear},
    {"glClearBufferfi", glClearBufferfi},
    {"glClearBufferfv", glClearBufferfv},
    {"glClearBufferiv", glClearBufferiv},
    {"glClearBufferuiv", glClearBufferuiv},
    {"glClearColor", glClearColor},
    {"glClearDepthf", glClearDepthf},
    {"glClearStencil", glClearStencil},
    {"glClientWaitSync", glClientWaitSync},
    {"glColorMask", glColorMask},
    {"glColorMaski", glColorMaski},
    {"glCompileShader", glCompileShader},
    {"glCompressedTexImage2D", glCompressedTexImage2D},
    {"glCompressedTexImage3D", glCompressedTexImage3D},
    {"glCompressedTexSubImage2D", glCompressedTexSubImage2D},
    {"glCompressedTexSubImage3D", glCompressedTexSubImage3D},
    {"glCopyBufferSubData", glCopyBufferSubData},
    {"glCopyImageSubData", glCopyImageSubData},
    {"glCopyTexImage2D", glCopyTexImage2D},
    {"glCopyTexSubImage2D", glCopyTexSubImage2D},
    {"glCopyTexSubImage3D", glCopyTexSubImage3D},
    {"glCreateProgram", glCreateProgram},
    {"glCreateShader", glCreateShader},
    {"glCreateShaderProgramv", glCreateShaderProgramv},
    {"glCullFace", glCullFace},
    {"glDebugMessageCallback", glDebugMessageCallback},
    {"glDebugMessageControl", glDebugMessageControl},
    {"glDebugMessageInsert", glDebugMessageInsert},
    {"glDeleteBuffers", glDeleteBuffers},
    {"glDeleteFramebuffers", glDeleteFramebuffers},
    {"glDeleteProgram", glDeleteProgram},
    {"glDeleteProgramPipelines", glDeleteProgramPipelines},
    {"glDeleteQueries", glDeleteQueries},
    {"glDeleteRenderbuffers", glDeleteRenderbuffers},
    {"glDeleteSamplers", glDeleteSamplers},
    {"glDeleteShader", glDeleteShader},
    {"glDeleteSync", glDeleteSync},
    {"glDeleteTextures", glDeleteTextures},
    {"glDeleteTransformFeedbacks", glDeleteTransformFeedbacks},
    {"glDeleteVertexArrays", glDeleteVertexArrays},
    {"glDepthFunc", glDepthFunc},
    {"glDepthMask", glDepthMask},
    {"glDepthRangef", glDepthRangef},
    {"glDetachShader", glDetachShader},
    {"glDisable", glDisable},
    {"glDisableVertexAttribArray", glDisableVertexAttribArray},
    {"glDisablei", glDisablei},
    {"glDispatchCompute", glDispatchCompute},
    {"glDispatchComputeIndirect", glDispatchComputeIndirect},
    {"glDrawArrays", glDrawArrays},
    {"glDrawArraysIndirect", glDrawArraysIndirect},
    {"glDrawArraysInstanced", glDrawArraysInstanced},
    {"glDrawBuffers", glDrawBuffers},
    {"glDrawElements", glDrawElements},
    {"glDrawElementsBaseVertex", glDrawElementsBaseVertex},
    {"glDrawElementsIndirect", glDrawElementsIndirect},
    {"glDrawElementsInstanced", glDrawElementsInstanced},
    {"glDrawElementsInstancedBaseVertex", glDrawElementsInstancedBaseVertex},
    {"glDrawRangeElements", glDrawRangeElements},
    {"glDrawRangeElementsBaseVertex", glDrawRangeElementsBaseVertex},
    {"glEnable", glEnable},
    {"glEnableVertexAttribArray", glEnableVertexAttribArray},
    {"glEnablei", glEnablei},
    {"glEndQuery", glEndQuery},
    {"glEndTransformFeedback", glEndTransformFeedback},
    {"glFenceSync", glFenceSync},
    {"glFinish", glFinish},
    {"glFlush", glFlush},
    {"glFlushMappedBufferRange", glFlushMappedBufferRange},
    {"glFramebufferParameteri", glFramebufferParameteri},
    {"glFramebufferRenderbuffer", glFramebufferRenderbuffer},
    {"glFramebufferTexture", glFramebufferTexture},
    {"glFramebufferTexture2D", glFramebufferTexture2D},
    {"glFramebufferTextureLayer", glFramebufferTextureLayer},
    {"glFrontFace", glFrontFace},
    {"glGenBuffers", glGenBuffers},
    {"glGenFramebuffers", glGenFramebuffers},
    {"glGenProgramPipelines", glGenProgramPipelines},
    {"glGenQueries", glGenQueries},
    {"glGenRenderbuffers", glGenRenderbuffers},
    {"glGenSamplers", glGenSamplers},
    {"glGenTextures", glGenTextures},
    {"glGenTransformFeedbacks", glGenTransformFeedbacks},
    {"glGenVertexArrays", glGenVertexArrays},
    {"glGenerateMipmap", glGenerateMipmap},
    {"glGetActiveAttrib", glGetActiveAttrib},
    {"glGetActiveUniform", glGetActiveUniform},
    {"glGetActiveUniformBlockName", glGetActiveUniformBlockName},
    {"glGetActiveUniformBlockiv", glGetActiveUniformBlockiv},
    {"glGetActiveUniformsiv", glGetActiveUniformsiv},
    {"glGetAttachedShaders", glGetAttachedShaders},
    {"glGetAttribLocation", glGetAttribLocation},
    {"glGetBooleani_v", glGetBooleani_v},
    {"glGetBooleanv", glGetBooleanv},
    {"glGetBufferParameteri64v", glGetBufferParameteri64v},
    {"glGetBufferParameteriv", glGetBufferParameteriv},
    {"glGetBufferPointerv", glGetBufferPointerv},
    {"glGetDebugMessageLog", glGetDebugMessageLog},
    {"glGetError", glGetError},
    {"glGetFloatv", glGetFloatv},
    {"glGetFragDataLocation", glGetFragDataLocation},
    {"glGetFramebufferAttachmentParameteriv", glGetFramebufferAttachmentParameteriv},
    {"glGetFramebufferParameteriv", glGetFramebufferParameteriv},
    {"glGetGraphicsResetStatus", glGetGraphicsResetStatus},
    {"glGetInteger64i_v", glGetInteger64i_v},
    {"glGetInteger64v", glGetInteger64v},
    {"glGetIntegeri_v", glGetIntegeri_v},
    {"glGetIntegerv", glGetIntegerv},
    {"glGetInternalformativ", glGetInternalformativ},
    {"glGetMultisamplefv", glGetMultisamplefv},
    {"glGetObjectLabel", glGetObjectLabel},
    {"glGetObjectPtrLabel", glGetObjectPtrLabel},
    {"glGetPointerv", glGetPointerv},
    {"glGetProgramBinary", glGetProgramBinary},
    {"glGetProgramInfoLog", glGetProgramInfoLog},
    {"glGetProgramInterfaceiv", glGetProgramInterfaceiv},
    {"glGetProgramPipelineInfoLog", glGetProgramPipelineInfoLog},
    {"glGetProgramPipelineiv", glGetProgramPipelineiv},
    {"glGetProgramResourceIndex", glGetProgramResourceIndex},
    {"glGetProgramResourceLocation", glGetProgramResourceLocation},
    {"glGetProgramResourceName", glGetProgramResourceName},
    {"glGetProgramResourceiv", glGetProgramResourceiv},
    {"glGetProgramiv", glGetProgramiv},
    {"glGetQueryObjectuiv", glGetQueryObjectuiv},
    {"glGetQueryiv", glGetQueryiv},
    {"glGetRenderbufferParameteriv", glGetRenderbufferParameteriv},
    {"glGetSamplerParameterIiv", glGetSamplerParameterIiv},
    {"glGetSamplerParameterIuiv", glGetSamplerParameterIuiv},
    {"glGetSamplerParameterfv", glGetSamplerParameterfv},
    {"glGetSamplerParameteriv", glGetSamplerParameteriv},
    {"glGetShaderInfoLog", glGetShaderInfoLog},
    {"glGetShaderPrecisionFormat", glGetShaderPrecisionFormat},
    {"glGetShaderSource", glGetShaderSource},
    {"glGetShaderiv", glGetShaderiv},
    {"glGetString", glGetString},
    {"glGetStringi", glGetStringi},
    {"glGetSynciv", glGetSynciv},
    {"glGetTexLevelParameterfv", glGetTexLevelParameterfv},
    {"glGetTexLevelParameteriv", glGetTexLevelParameteriv},
    {"glGetTexParameterIiv", glGetTexParameterIiv},
    {"glGetTexParameterIuiv", glGetTexParameterIuiv},
    {"glGetTexParameterfv", glGetTexParameterfv},
    {"glGetTexParameteriv", glGetTexParameteriv},
    {"glGetTransformFeedbackVarying", glGetTransformFeedbackVarying},
    {"glGetUniformBlockIndex", glGetUniformBlockIndex},
    {"glGetUniformIndices", glGetUniformIndices},
    {"glGetUniformLocation", glGetUniformLocation},
    {"glGetUniformfv", glGetUniformfv},
    {"glGetUniformiv", glGetUniformiv},
    {"glGetUniformuiv", glGetUniformuiv},
    {"glGetVertexAttribIiv", glGetVertexAttribIiv},
    {"glGetVertexAttribIuiv", glGetVertexAttribIuiv},
    {"glGetVertexAttribPointerv", glGetVertexAttribPointerv},
    {"glGetVertexAttribfv", glGetVertexAttribfv},
    {"glGetVertexAttribiv", glGetVertexAttribiv},
    {"glGetnUniformfv", glGetnUniformfv},
    {"glGetnUniformiv", glGetnUniformiv},
    {"glGetnUniformuiv", glGetnUniformuiv},
    {"glHint", glHint},
    {"glInvalidateFramebuffer", glInvalidateFramebuffer},
    {"glInvalidateSubFramebuffer", glInvalidateSubFramebuffer},
    {"glIsBuffer", glIsBuffer},
    {"glIsEnabled", glIsEnabled},
    {"glIsEnabledi", glIsEnabledi},
    {"glIsFramebuffer", glIsFramebuffer},
    {"glIsProgram", glIsProgram},
    {"glIsProgramPipeline", glIsProgramPipeline},
    {"glIsQuery", glIsQuery},
    {"glIsRenderbuffer", glIsRenderbuffer},
    {"glIsSampler", glIsSampler},
    {"glIsShader", glIsShader},
    {"glIsSync", glIsSync},
    {"glIsTexture", glIsTexture},
    {"glIsTransformFeedback", glIsTransformFeedback},
    {"glIsVertexArray", glIsVertexArray},
    {"glLineWidth", glLineWidth},
    {"glLinkProgram", glLinkProgram},
    {"glMapBufferRange", glMapBufferRange},
    {"glMemoryBarrier", glMemoryBarrier},
    {"glMemoryBarrierByRegion", glMemoryBarrierByRegion},
    {"glMinSampleShading", glMinSampleShading},
    {"glObjectLabel", glObjectLabel},
    {"glObjectPtrLabel", glObjectPtrLabel},
    {"glPatchParameteri", glPatchParameteri},
    {"glPauseTransformFeedback", glPauseTransformFeedback},
    {"glPixelStorei", glPixelStorei},
    {"glPolygonOffset", glPolygonOffset},
    {"glPopDebugGroup", glPopDebugGroup},
    {"glPrimitiveBoundingBox", glPrimitiveBoundingBox},
    {"glProgramBinary", glProgramBinary},
    {"glProgramParameteri", glProgramParameteri},
    {"glProgramUniform1f", glProgramUniform1f},
    {"glProgramUniform1fv", glProgramUniform1fv},
    {"glProgramUniform1i", glProgramUniform1i},
    {"glProgramUniform1iv", glProgramUniform1iv},
    {"glProgramUniform1ui", glProgramUniform1ui},
    {"glProgramUniform1uiv", glProgramUniform1uiv},
    {"glProgramUniform2f", glProgramUniform2f},
    {"glProgramUniform2fv", glProgramUniform2fv},
    {"glProgramUniform2i", glProgramUniform2i},
    {"glProgramUniform2iv", glProgramUniform2iv},
    {"glProgramUniform2ui", glProgramUniform2ui},
    {"glProgramUniform2uiv", glProgramUniform2uiv},
    {"glProgramUniform3f", glProgramUniform3f},
    {"glProgramUniform3fv", glProgramUniform3fv},
    {"glProgramUniform3i", glProgramUniform3i},
    {"glProgramUniform3iv", glProgramUniform3iv},
    {"glProgramUniform3ui", glProgramUniform3ui},
    {"glProgramUniform3uiv", glProgramUniform3uiv},
    {"glProgramUniform4f", glProgramUniform4f},
    {"glProgramUniform4fv", glProgramUniform4fv},
    {"glProgramUniform4i", glProgramUniform4i},
    {"glProgramUniform4iv", glProgramUniform4iv},
    {"glProgramUniform4ui", glProgramUniform4ui},
    {"glProgramUniform4uiv", glProgramUniform4uiv},
    {"glProgramUniformMatrix2fv", glProgramUniformMatrix2fv},
    {"glProgramUniformMatrix2x3fv", glProgramUniformMatrix2x3fv},
    {"glProgramUniformMatrix2x4fv", glProgramUniformMatrix2x4fv},
    {"glProgramUniformMatrix3fv", glProgramUniformMatrix3fv},
    {"glProgramUniformMatrix3x2fv", glProgramUniformMatrix3x2fv},
    {"glProgramUniformMatrix3x4fv", glProgramUniformMatrix3x4fv},
    {"glProgramUniformMatrix4fv", glProgramUniformMatrix4fv},
    {"glProgramUniformMatrix4x2fv", glProgramUniformMatrix4x2fv},
    {"glProgramUniformMatrix4x3fv", glProgramUniformMatrix4x3fv},
    {"glPushDebugGroup", glPushDebugGroup},
    {"glReadBuffer", glReadBuffer},
    {"glReadPixels", glReadPixels},
    {"glReadnPixels", glReadnPixels},
    {"glReleaseShaderCompiler", glReleaseShaderCompiler},
    {"glRenderbufferStorage", glRenderbufferStorage},
    {"glRenderbufferStorageMultisample", glRenderbufferStorageMultisample},
    {"glResumeTransformFeedback", glResumeTransformFeedback},
    {"glSampleCoverage", glSampleCoverage},
    {"glSampleMaski", glSampleMaski},
    {"glSamplerParameterIiv", glSamplerParameterIiv},
    {"glSamplerParameterIuiv", glSamplerParameterIuiv},
    {"glSamplerParameterf", glSamplerParameterf},
    {"glSamplerParameterfv", glSamplerParameterfv},
    {"glSamplerParameteri", glSamplerParameteri},
    {"glSamplerParameteriv", glSamplerParameteriv},
    {"glScissor", glScissor},
    {"glShaderBinary", glShaderBinary},
    {"glShaderSource", glShaderSource},
    {"glStencilFunc", glStencilFunc},
    {"glStencilFuncSeparate", glStencilFuncSeparate},
    {"glStencilMask", glStencilMask},
    {"glStencilMaskSeparate", glStencilMaskSeparate},
    {"glStencilOp", glStencilOp},
    {"glStencilOpSeparate", glStencilOpSeparate},
    {"glTexBuffer", glTexBuffer},
    {"glTexBufferRange", glTexBufferRange},
    {"glTexImage2D", glTexImage2D},
    {"glTexImage3D", glTexImage3D},
    {"glTexParameterIiv", glTexParameterIiv},
    {"glTexParameterIuiv", glTexParameterIuiv},
    {"glTexParameterf", glTexParameterf},
    {"glTexParameterfv", glTexParameterfv},
    {"glTexParameteri", glTexParameteri},
    {"glTexParameteriv", glTexParameteriv},
    {"glTexStorage2D", glTexStorage2D},
    {"glTexStorage2DMultisample", glTexStorage2DMultisample},
    {"glTexStorage3D", glTexStorage3D},
    {"glTexStorage3DMultisample", glTexStorage3DMultisample},
    {"glTexSubImage2D", glTexSubImage2D},
    {"glTexSubImage3D", glTexSubImage3D},
    {"glTransformFeedbackVaryings", glTransformFeedbackVaryings},
    {"glUniform1f", glUniform1f},
    {"glUniform1fv", glUniform1fv},
    {"glUniform1i", glUniform1i},
    {"glUniform1iv", glUniform1iv},
    {"glUniform1ui", glUniform1ui},
    {"glUniform1uiv", glUniform1uiv},
    {"glUniform2f", glUniform2f},
    {"glUniform2fv", glUniform2fv},
    {"glUniform2i", glUniform2i},
    {"glUniform2iv", glUniform2iv},
    {"glUniform2ui", glUniform2ui},
    {"glUniform2uiv", glUniform2uiv},
    {"glUniform3f", glUniform3f},
    {"glUniform3fv", glUniform3fv},
    {"glUniform3i", glUniform3i},
    {"glUniform3iv", glUniform3iv},
    {"glUniform3ui", glUniform3ui},
    {"glUniform3uiv", glUniform3uiv},
    {"glUniform4f", glUniform4f},
    {"glUniform4fv", glUniform4fv},
    {"glUniform4i", glUniform4i},
    {"glUniform4iv", glUniform4iv},
    {"glUniform4ui", glUniform4ui},
    {"glUniform4uiv", glUniform4uiv},
    {"glUniformBlockBinding", glUniformBlockBinding},
    {"glUniformMatrix2fv", glUniformMatrix2fv},
    {"glUniformMatrix2x3fv", glUniformMatrix2x3fv},
    {"glUniformMatrix2x4fv", glUniformMatrix2x4fv},
    {"glUniformMatrix3fv", glUniformMatrix3fv},
    {"glUniformMatrix3x2fv", glUniformMatrix3x2fv},
    {"glUniformMatrix3x4fv", glUniformMatrix3x4fv},
    {"glUniformMatrix4fv", glUniformMatrix4fv},
    {"glUniformMatrix4x2fv", glUniformMatrix4x2fv},
    {"glUniformMatrix4x3fv", glUniformMatrix4x3fv},
    {"glUnmapBuffer", glUnmapBuffer},
    {"glUseProgram", glUseProgram},
    {"glUseProgramStages", glUseProgramStages},
    {"glValidateProgram", glValidateProgram},
    {"glValidateProgramPipeline", glValidateProgramPipeline},
    {"glVertexAttrib1f", glVertexAttrib1f},
    {"glVertexAttrib1fv", glVertexAttrib1fv},
    {"glVertexAttrib2f", glVertexAttrib2f},
    {"glVertexAttrib2fv", glVertexAttrib2fv},
    {"glVertexAttrib3f", glVertexAttrib3f},
    {"glVertexAttrib3fv", glVertexAttrib3fv},
    {"glVertexAttrib4f", glVertexAttrib4f},
    {"glVertexAttrib4fv", glVertexAttrib4fv},
    {"glVertexAttribBinding", glVertexAttribBinding},
    {"glVertexAttribDivisor", glVertexAttribDivisor},
    {"glVertexAttribFormat", glVertexAttribFormat},
    {"glVertexAttribI4i", glVertexAttribI4i},
    {"glVertexAttribI4iv", glVertexAttribI4iv},
    {"glVertexAttribI4ui", glVertexAttribI4ui},
    {"glVertexAttribI4uiv", glVertexAttribI4uiv},
    {"glVertexAttribIFormat", glVertexAttribIFormat},
    {"glVertexAttribIPointer", glVertexAttribIPointer},
    {"glVertexAttribPointer", glVertexAttribPointer},
    {"glVertexBindingDivisor", glVertexBindingDivisor},
    {"glViewport", glViewport},
    {"glWaitSync", glWaitSync},
};

static int CompareEntry(const void* key, const void* entry) {
    return strcmp((const char*)key, ((const ProcEntry*)entry)->name);
}

void (*aurora_switch_gl_get_proc(const char* name))(void) {
    const ProcEntry* entry = (const ProcEntry*)bsearch(
        name, kProcs, sizeof(kProcs) / sizeof(kProcs[0]), sizeof(kProcs[0]), CompareEntry);
    if (entry != NULL) {
        return entry->proc;
    }
    // Mesa's (non-glvnd) static libEGL keeps EGL extension entry points file-static - e.g.
    // eglGetPlatformDisplayEXT, eglCreateSyncKHR - reachable only through its own
    // eglGetProcAddress, which also resolves GL dispatch entries.
    typedef void (*Proc)(void);
    typedef Proc (*GetProcAddressFn)(const char*);
    return ((GetProcAddressFn)eglGetProcAddress)(name);
}
