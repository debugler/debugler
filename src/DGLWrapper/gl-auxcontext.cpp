/* Copyright (C) 2013 Slawomir Cygan <slawomir.cygan@gmail.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "gl-auxcontext.h"
#include "gl-context.h"
#include "display.h"
#include "native-surface.h"
#include "pointers.h"

#include <DGLNet/protocol/pixeltransfer.h>

#include <sstream>
#include <DGLCommon/def.h>

namespace dglState {

GLAuxContextSession::GLAuxContextSession(GLAuxContext* ctx) : m_ctx(ctx) {
    m_ctx->doRefCurrent();
    m_ctx->queries.setupInitialState();
}

GLAuxContextSession::~GLAuxContextSession() {
    if (m_ctx) {
        m_ctx->doUnrefCurrent();
    }
}

void GLAuxContextSession::dispose() {
    if (m_ctx) {
        GLAuxContext* unrefCtx = nullptr;
        std::swap(unrefCtx, m_ctx);        
        if (!unrefCtx->doUnrefCurrent()) {
            throw std::runtime_error(
                "Cannot switch back from auxiliary context.");
        }
    }
}

GLAuxContextSurfaceBase::GLAuxContextSurfaceBase(const DGLDisplayState* display)
        : m_DisplayId(display->getId()), m_Id(0) {}

opaque_id_t GLAuxContextSurfaceBase::getId() const { return m_Id; }


GLAuxEGLContextSurface::GLAuxEGLContextSurface(const DGLDisplayState* display, opaque_id_t pixfmt):
        GLAuxContextSurfaceBase(display) {

    EGLint attributes[] = {
        EGL_HEIGHT, 1,
        EGL_WIDTH, 1,
        EGL_NONE
    };

    m_Id = (opaque_id_t)DIRECT_CALL_CHK(eglCreatePbufferSurface)(
        (EGLDisplay)m_DisplayId, (EGLConfig)pixfmt, attributes);
    if (!m_Id) {
        throw std::runtime_error("Cannot allocate axualiary pbuffer surface");
    }
}

GLAuxEGLContextSurface::~GLAuxEGLContextSurface() {
    if (m_Id) {
        DIRECT_CALL_CHK(eglDestroySurface)((EGLDisplay)m_DisplayId,
            (EGLSurface)m_Id);
    }
}

GLAuxWGLContextSurface::GLAuxWGLContextSurface(const DGLDisplayState* display, opaque_id_t pixfmt):
    GLAuxContextSurfaceBase(display) {

        const int  attributes[] = {
            0, 0,            
        };

        HDC hdc = DIRECT_CALL_CHK(wglGetCurrentDC)();
        m_Id = reinterpret_cast<opaque_id_t>(
            DIRECT_CALL_CHK(wglCreatePbufferARB)(hdc, static_cast<int>(pixfmt), 1, 1, attributes));
}

GLAuxWGLContextSurface::~GLAuxWGLContextSurface() {
    if (m_Id) {
        DIRECT_CALL_CHK(wglDestroyPbufferARB)((HPBUFFERARB)m_Id);
    }
}

GLAuxContext::GLAuxContext(const GLContext* origCtx)
        : queries(this),
          m_Id(0),
          m_PixelFormat(0),
          m_Parrent(origCtx),
          m_MakeCurrentRef(0) {

    if (m_Parrent->getDisplay()->getType() != DGLDisplayState::Type::EGL) {
        throw std::runtime_error("auxaliary contexts implemented only for EGL");
    }

    std::vector<EGLint> eglAttributes(
            m_Parrent->getContextCreationData().getAttribs().size());

    for (size_t i = 0;
         i < m_Parrent->getContextCreationData().getAttribs().size(); i++) {
        eglAttributes[i] =
                (EGLint)m_Parrent->getContextCreationData().getAttribs()[i];
    }
    eglAttributes.push_back(EGL_NONE);

    m_PixelFormat = choosePixelFormat(
            m_Parrent->getContextCreationData().getPixelFormat(),
            m_Parrent->getDisplay()->getId());

    switch (origCtx->getContextCreationData().getEntryPoint()) {
        case eglCreateContext_Call:
            m_Id = (opaque_id_t)DIRECT_CALL_CHK(eglCreateContext)(
                    (EGLDisplay)m_Parrent->getDisplay()->getId(),
                    (EGLConfig)m_PixelFormat, (EGLContext)m_Parrent->getId(),
                    &eglAttributes[0]);
            break;
    }

    if (!m_Id) {
        throw std::runtime_error("Cannot allocate auxaliary context");
    }
}

GLAuxContext::~GLAuxContext() {

    while (m_MakeCurrentRef) {
        doUnrefCurrent();
    }

    if (m_Id) {
        DIRECT_CALL_CHK(eglDestroyContext)(
                (EGLDisplay)m_Parrent->getDisplay()->getId(), (EGLContext)m_Id);
    }
}

GLAuxContextSession GLAuxContext::makeCurrent() {
    return GLAuxContextSession(this);
}

void GLAuxContext::doRefCurrent() {

    if (!m_MakeCurrentRef) {
        if (!m_AuxSurface) {
            m_AuxSurface = std::make_shared<GLAuxEGLContextSurface>(
                    m_Parrent->getDisplay(), m_PixelFormat);
        }
        EGLBoolean status = DIRECT_CALL_CHK(eglMakeCurrent)(
                (EGLDisplay)m_Parrent->getDisplay()->getId(),
                (EGLSurface)m_AuxSurface->getId(),
                (EGLSurface)m_AuxSurface->getId(), (EGLContext)m_Id);

        if (!status) {
            throw std::runtime_error("Cannot switch to auxaliary context.");
        }
    }

    m_MakeCurrentRef++;
}

bool GLAuxContext::doUnrefCurrent() {
    if (m_MakeCurrentRef) {
        m_MakeCurrentRef--;
    }
    if (!m_MakeCurrentRef) {
        EGLBoolean status = DIRECT_CALL_CHK(eglMakeCurrent)(
                (EGLDisplay)m_Parrent->getDisplay()->getId(),
                (EGLSurface)m_Parrent->getNativeDrawSurface()->getId(),
                (EGLSurface)m_Parrent->getNativeReadSurface()->getId(),
                (EGLContext)m_Parrent->getId());
        if (!status) {
            return false;
        }
    }
    return true;
}

opaque_id_t GLAuxContext::choosePixelFormat(opaque_id_t preferred,
                                            opaque_id_t displayId) {
    EGLDisplay eglDpy = (EGLDisplay)displayId;
    EGLConfig preferredConfig = (EGLConfig)preferred;
    EGLint supportedSurfType = 0;

    EGLBoolean status = EGL_TRUE;
    status &= DIRECT_CALL_CHK(eglGetConfigAttrib)(
            eglDpy, preferredConfig, EGL_SURFACE_TYPE, &supportedSurfType);

    if (!status) {
        throw std::runtime_error("Cannot query EGLConfig associated with ctx");
    }

    EGLConfig ret;

    if (supportedSurfType & EGL_PBUFFER_BIT) {
        ret = preferredConfig;
    } else {

        EGLint renderableType = 0;

        if (m_Parrent->getVersion().check(GLContextVersion::Type::DT)) {
            renderableType = EGL_OPENGL_BIT;
        } else if (m_Parrent->getVersion().check(GLContextVersion::Type::ES,
                                                 3)) {
            renderableType = EGL_OPENGL_ES3_BIT_KHR;
        } else if (m_Parrent->getVersion().check(GLContextVersion::Type::ES,
                                                 2)) {
            renderableType = EGL_OPENGL_ES2_BIT;
        } else if (m_Parrent->getVersion().check(GLContextVersion::Type::ES,
                                                 1)) {
            renderableType = EGL_OPENGL_ES_BIT;
        }

        EGLint attributes[] = {EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
                               EGL_RENDERABLE_TYPE, renderableType,
                               EGL_NONE};
        EGLint numConfigs = 0;
        status &= DIRECT_CALL_CHK(eglChooseConfig)(eglDpy, attributes, &ret, 1,
                                                   &numConfigs);
        if ((status != EGL_TRUE) || numConfigs < 1) {
            throw std::runtime_error(
                    "Cannot choose EGLConfig capable of driving auxaliary "
                    "context");
        }
    }
    return (opaque_id_t)ret;
}

GLAuxContext::GLQueries::GLQueries(GLAuxContext* ctx)
        : m_InitialState(false), m_AuxCtx(ctx) {}

const int GLAuxContext::GLQueries::BufferGetterChunkSize = 256;

void GLAuxContext::GLQueries::setupInitialState() {

    if (m_InitialState) return;
    DIRECT_CALL_CHK(glGenFramebuffers)(1, &fbo);
    DIRECT_CALL_CHK(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);

    DIRECT_CALL_CHK(glGenTextures)(1, &rtt);
    DIRECT_CALL_CHK(glBindTexture)(GL_TEXTURE_2D, rtt);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    DIRECT_CALL_CHK(glBindTexture)(GL_TEXTURE_2D, 0);
    DIRECT_CALL_CHK(glFramebufferTexture2D)(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtt, 0);

    if (m_AuxCtx->m_Parrent->getVersion().check(GLContextVersion::Type::ES,
                                                3) ||
        m_AuxCtx->m_Parrent->getVersion().check(GLContextVersion::Type::DT,
                                                3)) {
        DIRECT_CALL_CHK(glGenVertexArrays)(1, &vao);
        DIRECT_CALL_CHK(glBindVertexArray)(vao);
    }

    DIRECT_CALL_CHK(glGenBuffers)(1, &vbo);
    DIRECT_CALL_CHK(glBindBuffer)(GL_ARRAY_BUFFER, vbo);
    GLfloat triangleStrip[] = {-1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f,
                               0.0f,  1.0f,  1.0f, 1.0f, 0.0f,  1.0f,
                               1.0f,  -1.0f, 0.0f, 1.0f};

    GLfloat vertexPos[BufferGetterChunkSize];
    for (int i = 0; i < BufferGetterChunkSize; i++) vertexPos[i] = static_cast<GLfloat>(i)/static_cast<GLfloat>(BufferGetterChunkSize) * 2.0f - 1.0f;

    DIRECT_CALL_CHK(glBufferData)(GL_ARRAY_BUFFER, sizeof(triangleStrip) + sizeof(vertexPos),
                                  NULL, GL_STATIC_DRAW);

    DIRECT_CALL_CHK(glBufferSubData)(GL_ARRAY_BUFFER, 0, sizeof(triangleStrip), triangleStrip);
    DIRECT_CALL_CHK(glBufferSubData)(GL_ARRAY_BUFFER, sizeof(triangleStrip), sizeof(vertexPos), vertexPos);

    DIRECT_CALL_CHK(glVertexAttribPointer)(0, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    DIRECT_CALL_CHK(glEnableVertexAttribArray)(0);
    DIRECT_CALL_CHK(glVertexAttribPointer)(1, 1, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid*>(sizeof(triangleStrip)));
    DIRECT_CALL_CHK(glEnableVertexAttribArray)(1);

    {
        std::string vshTex;

        if (m_AuxCtx->m_Parrent->getVersion().check(GLContextVersion::Type::ES, 3)) {
            vshTex += 
                "#version 300 es\n"
                "in vec4 in_Position;\n"
                "out vec2 texPos;\n";
        } else {
            vshTex += 
                "attribute vec4 in_Position;\n"
                "varying vec2 texPos;\n";
        }
        vshTex += 
            "void main() {\n"
            "   gl_Position = in_Position;\n"
            "   texPos = in_Position.xy * 0.5 + 0.5;\n"
            "}\n";

        vshobjTexture = compileShader(GL_VERTEX_SHADER, vshTex.c_str());
    }


    programGetBuffer = DIRECT_CALL_CHK(glCreateProgram)();
    {
        const char* vsh =
            "attribute float in_VertexId;\n"
            "attribute vec4 in_BufferData;\n"
            "varying vec4 out_Color;\n"
            "void main() {\n"
            "   gl_Position = vec4(in_VertexId, 0.0, 0.0, 1.0);\n"
            "   gl_PointSize = 1.0;\n"
            "   out_Color = in_BufferData;\n"
            "}\n";

        const char* fsh = 
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
            "precision highp float;           \n"
            "#else                            \n"
            "precision mediump float;         \n"
            "#endif                           \n"
            "varying vec4 out_Color;\n"
            "void main() {\n"
            " gl_FragColor = out_Color;\n"
            "}\n";

        GLuint vshObj = compileShader(GL_VERTEX_SHADER, vsh);
        DIRECT_CALL_CHK(glAttachShader)(programGetBuffer, vshObj);
        DIRECT_CALL_CHK(glDeleteShader)(vshObj);
        GLuint fshObj = compileShader(GL_FRAGMENT_SHADER, fsh);
        DIRECT_CALL_CHK(glAttachShader)(programGetBuffer, fshObj);
        DIRECT_CALL_CHK(glDeleteShader)(fshObj);
        
        DIRECT_CALL_CHK(glBindAttribLocation)(programGetBuffer, 1, "in_VertexId");
        DIRECT_CALL_CHK(glBindAttribLocation)(programGetBuffer, 2, "in_BufferData");
        
        linkProgram(programGetBuffer);
    }

    if (DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR) {
        throw std::runtime_error("Got GL error on auxiliary context");
    }

    m_InitialState = true;
}

void GLAuxContext::GLQueries::auxDrawTexture(GLuint name, GLenum target,
                                             GLint level, GLint layer, GLint face,
                                             GLenum textureBaseFormat,
                                             GLenum renderableFormat, int width,
                                             int height) {

    if (!m_AuxCtx->m_Parrent->getVersion().check(GLContextVersion::Type::ES,
                                                 2)) {
        throw std::runtime_error(
                "GLAuxContext::GLQueries::auxGetTexImage not implemented for "
                "this "
                "context version");
    }

    if (!DIRECT_CALL_CHK(glIsTexture)(name)) {
        throw std::runtime_error(
                "Texture object not found in auxaliary context");
    }

    DIRECT_CALL_CHK(glBindTexture)(target, rtt);


    const GLInternalFormat* internalFormatDesc =
        GLFormats::getInternalFormat(renderableFormat);

    if (!m_AuxCtx->m_Parrent->getVersion().check(GLContextVersion::Type::ES, 3)) {
        //On ES2.0 only unsized formats are supported.
        renderableFormat = (GLenum)internalFormatDesc->dataFormat;
    }

    DIRECT_CALL_CHK(glTexImage2D)(GL_TEXTURE_2D, 0, renderableFormat, width, height, 0, (GLenum)internalFormatDesc->dataFormat,  (GLenum)internalFormatDesc->dataType, NULL);

    
    DIRECT_CALL_CHK(glBindTexture)(target, name);

    GLuint program = getTextureShaderProgram(target, textureBaseFormat);
    DIRECT_CALL_CHK(glUseProgram)(program);
    DIRECT_CALL_CHK(glUniform1f)(
            DIRECT_CALL_CHK(glGetUniformLocation)(program, "level"),
            static_cast<GLfloat>(level));


    DIRECT_CALL_CHK(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);
    DIRECT_CALL_CHK(glClear)(GL_COLOR_BUFFER_BIT);
    DIRECT_CALL_CHK(glViewport)(0, 0, width, height);

    DIRECT_CALL_CHK(glEnableVertexAttribArray)(0);
    DIRECT_CALL_CHK(glDisableVertexAttribArray)(1);
    DIRECT_CALL_CHK(glDisableVertexAttribArray)(2);

    DIRECT_CALL_CHK(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);

    DIRECT_CALL_CHK(glDisableVertexAttribArray)(0);

    (void) layer; //no program for 3D textures, yet
    (void) face; //no program for CM textures, yet
}


void GLAuxContext::GLQueries::auxGetBufferData(GLuint name, std::vector<char>& ret) {
    GLint size;

    DIRECT_CALL_CHK(glBindTexture)(GL_TEXTURE_2D, rtt);
    DIRECT_CALL_CHK(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA, BufferGetterChunkSize, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    DIRECT_CALL_CHK(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    DIRECT_CALL_CHK(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);
    DIRECT_CALL_CHK(glViewport)(0, 0, BufferGetterChunkSize, 1);

    DIRECT_CALL_CHK(glBindBuffer)(GL_ARRAY_BUFFER, name);

    DIRECT_CALL_CHK(glGetBufferParameteriv)(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);

    ret.resize(size);

    DIRECT_CALL_CHK(glUseProgram)(programGetBuffer);

    DIRECT_CALL_CHK(glEnableVertexAttribArray)(1);
    DIRECT_CALL_CHK(glEnableVertexAttribArray)(2);
    DIRECT_CALL_CHK(glDisableVertexAttribArray)(0);


    std::vector<char> chunk;

    int offset = 0;

    while (offset < size) {

        int thisChunkSize = std::min(BufferGetterChunkSize, size - offset);

        if (thisChunkSize > 4) {
            //if chunk is larger than element size, blit only full elements.
            thisChunkSize &= -3;
        }

        int elementSize = 4;
        if (thisChunkSize < 4) {
            elementSize = thisChunkSize;
        }

        DIRECT_CALL_CHK(glVertexAttribPointer)(2, elementSize, GL_UNSIGNED_BYTE, GL_TRUE, 0, reinterpret_cast<GLvoid*>(offset));

        DIRECT_CALL_CHK(glClearColor)(1.0, 1.0, 1.0, 1.0);

        DIRECT_CALL_CHK(glClear)(GL_COLOR_BUFFER_BIT);

        DIRECT_CALL_CHK(glDrawArrays)(GL_POINTS, 0, thisChunkSize / elementSize);

        if (thisChunkSize < 4) {
            GLubyte buff[4];
            DIRECT_CALL_CHK(glReadPixels)(0, 0, thisChunkSize / elementSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, &buff);
            for (int i =0; i < thisChunkSize; i++) {
                ret[offset + i] = buff[i];
            }
        } else {
            DIRECT_CALL_CHK(glReadPixels)(0, 0, thisChunkSize / elementSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, &ret[offset]);
        }
        offset += thisChunkSize;
    }

    DIRECT_CALL_CHK(glBindBuffer)(GL_ARRAY_BUFFER, 0);
    DIRECT_CALL_CHK(glDisableVertexAttribArray)(2);

    if (DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR) {
        throw std::runtime_error("Got GL error on auxiliary context");
    }
}

GLuint GLAuxContext::GLQueries::getTextureShaderProgram(
        GLenum target, GLenum textureBaseFormat) {

    std::string suffix, pos;

    if (textureBaseFormat != GL_RGBA && textureBaseFormat != GL_RGB &&
        textureBaseFormat != GL_RG && textureBaseFormat != GL_RED) {
        throw std::runtime_error(
                "GLAuxContext::GLQueries::getTextureShaderProgram: format "
                "unsupported");
    }

    switch (target) {
        case GL_TEXTURE_1D:
            suffix = "1D";
            pos = "texPos.x";
            break;
        case GL_TEXTURE_2D:
            suffix = "2D";
            pos = "vec2(texPos.xy)";
            break;
        case GL_TEXTURE_RECTANGLE:
            suffix = "Rect";
            pos = "vec2(texPos.xy)";
            break;
        case GL_TEXTURE_1D_ARRAY:
            suffix = "1DArray";
            pos = "vec2(texPos.xy)";
            break;
        // case GL_TEXTURE_CUBE_MAP:
        //  suffix = "Cube";
        //  pos =
        //  break;
        default:
            throw std::runtime_error(
                    "cannot generate program for this texture target");
    }

    bool glsl300 = m_AuxCtx->m_Parrent->getVersion().check(
            GLContextVersion::Type::ES, 3);

    std::ostringstream fsh;

    if (glsl300) {
        fsh << "#version 300 es\n"
            << "out vec4 oColor;\n";
    }
    fsh << "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
            "precision highp float;           \n"
            "#else                            \n"
            "precision mediump float;         \n"
            "#endif                           \n"
            "uniform sampler" << suffix << " s;\n"
            "uniform float level;\n";
    if (glsl300) {
        fsh << 
            "in vec2 texPos;\n";
    } else {
        fsh <<
            "varying vec2 texPos;\n";
    }

    fsh << "void main() {\n";

    if (glsl300) {
        fsh << "    oColor = textureLod(s, ";
    } else {
        fsh << "    gl_FragColor = texture" << suffix << "(s, ";
    }
    fsh << pos;
    if (glsl300) {
        fsh << ", level";
    }
    fsh << ");\n"
        << "}\n";

    auto i = programsTexture.find(fsh.str());
    if (i != programsTexture.end()) {
        return i->second;
    } else {

        GLuint program = DIRECT_CALL_CHK(glCreateProgram)();
        DIRECT_CALL_CHK(glAttachShader)(program, vshobjTexture);

        std::string fshStr = fsh.str();
        GLuint fShader = compileShader(GL_FRAGMENT_SHADER, fshStr.c_str());
        DIRECT_CALL_CHK(glAttachShader)(program, fShader);
        DIRECT_CALL_CHK(glDeleteShader)(fShader);
       
        DIRECT_CALL_CHK(glBindAttribLocation)(program, 0, "in_Position");

        linkProgram(program);

        programsTexture[fshStr] = program;
        return program;
    }
}


GLuint GLAuxContext::GLQueries::compileShader(GLenum type, const char* src) {
    GLuint shader = DIRECT_CALL_CHK(glCreateShader)(type);

    const char* csrc[] = { src };

    DIRECT_CALL_CHK(glShaderSource)(shader, 1, csrc, NULL);
    DIRECT_CALL_CHK(glCompileShader)(shader);

    GLint status = 0;
    DIRECT_CALL_CHK(glGetShaderiv)(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1000];
        DIRECT_CALL_CHK(glGetShaderInfoLog)(shader, 1000, NULL, log);
        throw std::runtime_error(
            std::string("Cannot compile shader:") + log);
    }

    return shader;
}

void GLAuxContext::GLQueries::linkProgram(GLuint program) {
    DIRECT_CALL_CHK(glLinkProgram)(program);

    GLint status;
    DIRECT_CALL_CHK(glGetProgramiv)(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char log[10000];
        DIRECT_CALL_CHK(glGetProgramInfoLog)(program, 10000, NULL, log);
        DIRECT_CALL_CHK(glDeleteProgram)(program);
        throw std::runtime_error(std::string("cannot link program: ") +
            log);
    }
}

}    // namespace dglState