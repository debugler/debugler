/* Copyright (C) 2014 Slawomir Cygan <slawomir.cygan@gmail.com>
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

#include "gl-context.h"

#include <DGLCommon/os.h>
#include <DGLNet/protocol/resource.h>
#include <DGLNet/protocol/pixeltransfer.h>

#include "api-loader.h"
#include "display.h"
#include "native-surface.h"
#include "pointers.h"
#include "gl-utils.h"
#include "gl-auxcontext.h"
#include "tls.h"

#include <cstring>
#include <sstream>

namespace dglState {

GLContextVersion::GLContextVersion(Type type, int majorVersion, int minorVersion)
        : m_Initialized(false), m_MajorVersion(majorVersion), m_MinorVersion(minorVersion), m_Type(type) {}

bool GLContextVersion::check(Type type, int majorVersion,
                             int minorVersion) const {

    if (type != m_Type) {
        return false;
    }

    if (!m_Initialized) return false;

    if (majorVersion > m_MajorVersion) {
        return false;
    }

    // TODO make ES1 uncompatible with ES2

    if (majorVersion == m_MajorVersion && minorVersion > m_MinorVersion) {
        return false;
    }

    return true;
}

void GLContextVersion::initialize(const char* cVersion) {
    // It is safe to assume OpenGL ES 1.1 / OpenGL 1.1 is supported, if version
    // string
    // parsing fails.
    m_MajorVersion = m_MinorVersion = 1;

    DGL_ASSERT(cVersion);

    if (cVersion == NULL) {
        return;
    }

    std::string version = cVersion;

    Type parsedType = m_Type;

    size_t vOffset = 0;

    //ES versions usually have an prefix.
    if (version.substr(0, strlen("OpenGL ES ")) == "OpenGL ES ") {
        vOffset = strlen("OpenGL ES ");
        parsedType = Type::ES;
    } else if (version.substr(0, strlen("OpenGL ES-CM ")) == "OpenGL ES-CM ") {
        vOffset = strlen("OpenGL ES-CM ");
        parsedType = Type::ES;
    } else if (version.substr(0, strlen("OpenGL ES-CL ")) == "OpenGL ES-CL ") {
        vOffset = strlen("OpenGL ES-CL ");
        parsedType = Type::ES;
    } else if (version.substr(0, strlen("OpenGL ES-")) == "OpenGL ES-") {
        vOffset = strlen("OpenGL ES-");
        parsedType = Type::ES;
    }

    DGL_ASSERT (parsedType == m_Type);

    m_Type = parsedType;

    if (vOffset + 2 <= version.length() &&
        version[vOffset] >= '0' && version[vOffset] <= '9' &&
        version[vOffset + 1] == '.' &&
        version[vOffset + 2] >= '0' && version[vOffset + 2] <= '9') {
        m_MajorVersion = version[vOffset] - '0';
        m_MinorVersion = version[vOffset + 2] - '0';
    } else {
        DGL_ASSERT(!"cannot reliably detect OpenGL version");
        //this is rather serious, so mark ctx as not supported.
        m_Type = Type::UNSUPPORTED;
    }

    OS_DEBUG("Context version: %d.%d (SUPPORTED == %d, ES == %d)",
        m_MajorVersion,
        m_MinorVersion,
        (int)(m_Type != Type::UNSUPPORTED), 
        (int)(m_Type == Type::ES));

    m_Initialized = true;
}

int GLContextVersion::getMajor() const { return m_MajorVersion; }

int GLContextVersion::getNeededApiLibraries(const DGLDisplayState* display) {
    if (m_Type == Type::UNSUPPORTED) {
        return LIBRARY_NONE;
    }
    if (display->getType() != DGLDisplayState::Type::EGL || m_Type == Type::DT) {
        return LIBRARY_GL;
    }

#ifdef __ANDROID__
    //On android load also ext symbols defined in system libraries.
    const int es2Libs = LIBRARY_ES2 | LIBRARY_ES2_ANDROID;
    const int es1Libs = LIBRARY_ES1 | LIBRARY_ES1_ANDROID;
#else
    const int es2Libs = LIBRARY_ES2;
    const int es1Libs = LIBRARY_ES1;
#endif

    if (display->getType() == DGLDisplayState::Type::EGL && m_Type == Type::ES) {
        switch (m_MajorVersion) {
            case 3:
                return es2Libs | LIBRARY_ES3;
            case 2:
                return es2Libs;
            case 1:
                return es1Libs;
        }
    }
    DGL_ASSERT(0);
    return LIBRARY_NONE;
}

GLContextCreationData::GLContextCreationData()
        : m_Entrypoint(NO_ENTRYPOINT), m_pixelFormat(0) {}

GLContextCreationData::GLContextCreationData(Entrypoint entryp,
                                             gl_t pixelFormat,
                                             const std::vector<gl_t>& attribs)
        : m_Entrypoint(entryp),
          m_pixelFormat(pixelFormat),
          m_attribs(attribs) {}

Entrypoint GLContextCreationData::getEntryPoint() const { return m_Entrypoint; }

opaque_id_t GLContextCreationData::getPixelFormat() const {
    return m_pixelFormat;
}

const std::vector<gl_t>& GLContextCreationData::getAttribs() const {
    return m_attribs;
}

GLContext::GLContext(const DGLDisplayState* display, GLContextVersion version,
                     opaque_id_t id, const GLContextCreationData& creationData)
        : m_Version(version),
          m_Id(id),
          m_NativeReadSurface(NULL),
          m_NativeDrawSurface(NULL),
          m_HasNVXMemoryInfo(false),
          m_HasDebugOutput(false),
          m_DebugOutputCallback(NULL),
          m_EverBound(false),
          m_RefCount(0),
          m_ToBeDeleted(false),
          m_InQuery(false),
          m_CreationData(creationData),
          m_Display(display) {}


GLContext::~GLContext() {
    ns().clear();
}


dglnet::message::utils::ContextReport GLContext::describe() {
    dglnet::message::utils::ContextReport ret(m_Id);
    ret.m_TextureSpace      = ns().getShared()->get().m_Textures.getReport(m_Id);
    ret.m_BufferSpace       = ns().getShared()->get().m_Buffers.getReport(m_Id);
    ret.m_ShaderSpace       = ns().m_Shaders.getReport(m_Id);
    ret.m_ProgramSpace      = ns().m_Programs.getReport(m_Id);
    ret.m_FBOSpace          = ns().m_FBOs.getReport(m_Id);
    ret.m_RenderbufferSpace = ns().m_Renderbuffers.getReport(m_Id);

    if (m_NativeReadSurface) {
        if (m_NativeReadSurface->isStereo()) {
            if (m_NativeReadSurface->isDoubleBuffered()) {
                ret.m_FramebufferSpace.insert(
                        dglnet::ContextObjectName(m_Id, GL_BACK_RIGHT));
            }
            ret.m_FramebufferSpace.insert(
                    dglnet::ContextObjectName(m_Id, GL_FRONT_RIGHT));
        }
        if (hasCapability(ContextCap::ReadBuffersFrontBuffer)) {
            // we have glReadBuffer, so we can read from front/back
            if (m_NativeReadSurface->isDoubleBuffered()) {
                ret.m_FramebufferSpace.insert(
                        dglnet::ContextObjectName(m_Id, GL_BACK));
            }
            ret.m_FramebufferSpace.insert(
                    dglnet::ContextObjectName(m_Id, GL_FRONT));
        } else {
            ret.m_FramebufferSpace.insert(
                    dglnet::ContextObjectName(m_Id, GL_BACK));
        }
    }

    ret.m_TextureUnitSpace = shadow().getTexUnits().report(m_Id);

    {
        //first get report of all PPO names
        std::set<dglnet::ContextObjectName> barePPOs = ns().m_ProgramPipelines.getReport(m_Id);

        //for each reported name get PPO contents report
        for (std::set<dglnet::ContextObjectName>::iterator it = barePPOs.begin();
            it != barePPOs.end(); ++it) {

            GLProgramPipelineObj* ppo = ns().m_ProgramPipelines.getObject(static_cast<GLuint>(it->m_Name));

            ret.m_ProgramPipelineSpace.insert(
                std::pair<dglnet::ContextObjectName, std::set<dglnet::ContextObjectName> >(
                    // PPO name:
                    *it, 
                    // PPO contents:
                    ppo->getReport(m_Id)));
        }

    }
    
    return ret;
}

NativeSurfaceBase* GLContext::getNativeReadSurface() const {
    return m_NativeReadSurface;
}

NativeSurfaceBase* GLContext::getNativeDrawSurface() const {
    return m_NativeDrawSurface;
}

void GLContext::setNativeSurfaces(NativeSurfaceBase* read,
                                  NativeSurfaceBase* draw) {
    m_NativeReadSurface = read;
    m_NativeDrawSurface = draw;
}

std::pair<bool, GLenum> GLContext::getPokedError() {
    std::pair<bool, GLenum> ret;
    if (m_PokedErrorQueue.size()) {
        ret.first = true;
        ret.second = m_PokedErrorQueue.front();
        m_PokedErrorQueue.pop();
    } else {
        ret.first = false;
    }
    return ret;
}

GLenum GLContext::peekError() {

    if (shadow().inImmediateMode())
        return GL_NO_ERROR;    // we cannot get erros after glBegin()

    GLenum ret = DIRECT_CALL_CHK(glGetError)();
    if (ret != GL_NO_ERROR && m_PokedErrorQueue.size() < 1000) {
        GLenum error = ret;
        int retries = 16;
        do {
            m_PokedErrorQueue.push(error);
            error = DIRECT_CALL_CHK(glGetError)();
            retries--;
        } while (error != GL_NO_ERROR && retries);
    }
    return ret;
}

void GLContext::setDebugOutput(GLenum source, GLenum type, GLuint id,
                               GLenum severity, GLsizei length,
                               const GLchar* message, const GLvoid* userParam) {

    if (m_InQuery) return;

    m_HasDebugOutput = true;
    m_DebugOutput = std::string(message, static_cast<size_t>(length));

    if (m_DebugOutputCallback) {
        m_DebugOutputCallback(source, type, id, severity, length, message,
                              userParam);
    }
}

bool GLContext::hasDebugOutput() { return m_HasDebugOutput; }

const std::string& GLContext::popDebugOutput() {
    m_HasDebugOutput = false;
    return m_DebugOutput;
}

void GLContext::setCustomDebugOutputCallback(GLDEBUGPROC callback) {
    m_DebugOutputCallback = callback;
}

void GLContext::startQuery() {

    m_InQuery = true;

    if (m_Version.check(GLContextVersion::Type::UNSUPPORTED)) {
        throw std::runtime_error("Context version is not supported");
    }

    peekError();
    if (shadow().inImmediateMode()) {
        throw std::runtime_error(
                "OpenGL is currently in immediate mode (after glBegin,  before "
                "glEnd) - cannot issue query");
    }
}

void GLContext::queryCheckError() {
    GLenum error;
    if ((error = DIRECT_CALL_CHK(glGetError)()) != GL_NO_ERROR) {
        throw std::runtime_error(
                std::string("Query failed: got OpenGL error (") +
                GetGLEnumName(error, GLEnumGroup::ErrorCode) + ")");
    }
}

bool GLContext::endQuery(std::string& message) {
    bool ret = true;
    GLenum error;
    if (shadow().inImmediateMode() &&
        (error = DIRECT_CALL_CHK(glGetError)()) != GL_NO_ERROR) {
        message = std::string("Query failed: got OpenGL error (") +
                  GetGLEnumName(error, GLEnumGroup::ErrorCode) + ")";
        ret = false;
    }
    while (!shadow().inImmediateMode() && DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR)
        ;

    m_InQuery = false;

    return ret;
}

void GLContext::bound() {
    if (!m_EverBound) {
        m_EverBound = true;
        firstUse();
    }
    m_RefCount++;
}

bool GLContext::markForDeletionMayDelete() {
    m_ToBeDeleted = true;
    return m_RefCount <= 0;
}

bool GLContext::unboundMayDelete() {
    m_RefCount--;
    DGL_ASSERT(m_RefCount >= 0);
    return m_ToBeDeleted && m_RefCount <= 0;
}

const GLContextVersion& GLContext::getVersion() const { return m_Version; }

std::shared_ptr<dglnet::DGLResource> GLContext::queryTexture(gl_t _name) {

    GLuint name = static_cast<GLuint>(_name);

    dglnet::resource::DGLResourceTexture* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceTexture());

    // check if GL knows about a texture
    if (DIRECT_CALL_CHK(glIsTexture)(name) != GL_TRUE) {
        throw std::runtime_error("Texture does not exist");
    }

    // check if we know about a texture target
    GLTextureObj* tex = ns().getShared()->get().m_Textures.getObject(name);

    resource->m_Target = tex->getTarget();

    if (resource->m_Target == 0) {
        throw std::runtime_error("Texture target is unknown");
    } else if (resource->m_Target != GL_TEXTURE_1D &&
               resource->m_Target != GL_TEXTURE_2D &&
               resource->m_Target != GL_TEXTURE_2D_MULTISAMPLE &&
               resource->m_Target != GL_TEXTURE_3D &&
               resource->m_Target != GL_TEXTURE_2D_ARRAY &&
               resource->m_Target != GL_TEXTURE_2D_MULTISAMPLE_ARRAY &&
               resource->m_Target != GL_TEXTURE_RECTANGLE &&
               resource->m_Target != GL_TEXTURE_1D_ARRAY &&
               resource->m_Target != GL_TEXTURE_CUBE_MAP) {
        throw std::runtime_error("Texture target is unsupported");
    }

    // disconnect PBO if it exists
    state_setters::DefaultPBO defPBO(this);
    state_setters::PixelStoreAlignment defAlignment(this);

    GLuint lastTexture;
    if (!glutils::getBoundTexture(tex->getTarget(), lastTexture)) {
        throw std::runtime_error("Cannot get currently bound texture");
    }

    // rebind texture, so we can access it
    if (lastTexture != tex->getName()) {
        DIRECT_CALL_CHK(glBindTexture)(tex->getTarget(), tex->getName());
    }

    if (tex->getTarget() == GL_TEXTURE_CUBE_MAP) {
        resource->m_FacesLevelsLayers.resize(6);
    } else {
        resource->m_FacesLevelsLayers.resize(1);
    }

    for (size_t face = 0; face < resource->m_FacesLevelsLayers.size(); face++) {
        for (int level = 0;; level++) {

            std::vector<dglnet::resource::DGLResourceTexture::TextureLayer> currentLevel;

            GLint samples, internalFormat;
            tex->getFormat(this, level, tex->getTextureLevelTarget(face), internalFormat, samples);

            for (int layer = 0;; layer++) {

                std::shared_ptr<dglnet::resource::DGLPixelRectangle> rect =
                    queryTextureLevel(tex, level, layer, face, defAlignment);

                if (!rect) {
                    break;
                } else {
                    dglnet::resource::DGLResourceTexture::TextureLayer currentLayer;

                    currentLayer.m_Samples = samples;
                    currentLayer.m_InternalFormat = internalFormat;

                    currentLayer.m_PixelRectangle = rect;
                    currentLevel.push_back((currentLayer));
                }
            }

            if (currentLevel.size()) {
                resource->m_FacesLevelsLayers[face].push_back(currentLevel);
            } else {
                break;
            }
        }
    }

    // restore state
    if (lastTexture != tex->getName()) {
        DIRECT_CALL_CHK(glBindTexture)(tex->getTarget(), lastTexture);
    }
    return ret;
}

bool GLContext::isTexture1Dim(GLenum target) { return target == GL_TEXTURE_1D; }

bool GLContext::isTexture2Dim(GLenum target) {
    return (target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_2D ||
            target == GL_TEXTURE_2D_MULTISAMPLE ||
            target == GL_TEXTURE_RECTANGLE || target == GL_TEXTURE_CUBE_MAP ||
            target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
            target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
            target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
            target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
            target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
            target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z ||
            target == GL_TEXTURE_EXTERNAL_OES);
}

void GLContext::queryTextureLevelSize(const GLTextureObj* tex, GLuint level,
                                      GLint* width, GLint* height,
                                      GLint* depth) {

    // face does not matter here. all cube map faces have same size.
    GLenum levelTarget = tex->getTextureLevelTarget(0);

    if (hasCapability(ContextCap::TextureGetters)) {

        if (width) {
            DIRECT_CALL_CHK(glGetTexLevelParameteriv)(levelTarget, level,
                                                      GL_TEXTURE_WIDTH, width);
        }

        if (height) {
            if (isTexture1Dim(levelTarget)) {
                *height = 1;
            } else {
                DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
                        levelTarget, level, GL_TEXTURE_HEIGHT, height);
            }
        }

        if (depth) {
            if (isTexture2Dim(levelTarget)) {
                *depth = 1;
            } else {
                DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
                        levelTarget, level, GL_TEXTURE_DEPTH, depth);
            }
        }
    } else {
        
        // We cannot easily get texture size without level getters.
        // At first try to bisect the texture size using TexSubImage. If it
        // fails (bisection fails,
        // when TexImage does not set proper errors, so maxSize is returned), go
        // with requested texture sizes.

        GLint maxSize = 16384;
        DIRECT_CALL_CHK(glGetIntegerv)(GL_MAX_TEXTURE_SIZE, &maxSize);

        GLenum formatToRequest = GL_RGBA;
        GLenum typeToRequest = GL_UNSIGNED_BYTE;

        const GLTextureObj::GLTextureLevel* requestedLevel =
                tex->getRequestedLevel(level);

        if (requestedLevel) {
            const GLInternalFormat* internalFormatDesc =
                GLFormats::getInternalFormat(
                requestedLevel->m_RequestedInternalFormat);
            if (internalFormatDesc) {
                formatToRequest = static_cast<GLenum>(internalFormatDesc->dataFormat);
                typeToRequest =   static_cast<GLenum>(internalFormatDesc->dataType);
            }
        }

        const GLTextureObj::GLTextureLevel* requestedLevelMinusOne = nullptr;

        if (level) {
            requestedLevelMinusOne = tex->getRequestedLevel(level - 1);
        }

        if (width) {
            *width = textureBisectSizeES(levelTarget, level, 0, maxSize, formatToRequest, typeToRequest);
            if (*width == maxSize || *width == 0) {
                OS_DEBUG("Texture width bisection failed: got width %d\n.",
                          *width);
                if (requestedLevel) {
                    OS_DEBUG("Will use width requested by TexImage.");
                    *width = requestedLevel->m_Width;
                } else if (requestedLevelMinusOne && requestedLevelMinusOne->m_Width == 1) {
                     OS_DEBUG("Will use width requested by TexImage.");
                    *width = 0;
                } else {
                    OS_DEBUG(
                            "Cannot use width requested by TexImage: level was "
                            "not requested?.");
                }
            }
        }
        if (height) {
            *height = textureBisectSizeES(levelTarget, level, 1, maxSize, formatToRequest, typeToRequest);
            if (*height == maxSize || *height == 0) {
                OS_DEBUG("Texture height bisection failed: got height %d\n.",
                          *height);
                if (requestedLevel) {
                    OS_DEBUG("Will use height requested by TexImage.");
                    *height = requestedLevel->m_Height;
                } else if (requestedLevelMinusOne && requestedLevelMinusOne->m_Height == 1) {
                    OS_DEBUG("Will use width requested by TexImage.");
                    *height = 0;
                } else {
                    OS_DEBUG(
                            "Cannot use height requested by TexImage: level "
                            "was not requested?.");
                }
            }
        }

        if (depth) {
            *depth = textureBisectSizeES(levelTarget, level, 2, maxSize, formatToRequest, typeToRequest);
            if (*depth == maxSize || *depth == 0) {
                OS_DEBUG("Texture depth bisection failed: got depth %d\n.",
                          *depth);
                if (requestedLevel) {
                    OS_DEBUG("Will use depth requested by TexImage.");
                    *depth = requestedLevel->m_Depth;
                } else if (requestedLevelMinusOne && requestedLevelMinusOne->m_Depth == 1) {
                    OS_DEBUG("Will use width requested by TexImage.");
                    *depth = 0;
                } else {
                    OS_DEBUG(
                            "Cannot use depth requested by TexImage: level was "
                            "not requested?.");
                }
            }
        }
    }
}

std::shared_ptr<dglnet::resource::DGLPixelRectangle>
GLContext::queryTextureLevel(const GLTextureObj* tex, int level, int layer, size_t face,
                             state_setters::PixelStoreAlignment& defAlignment) {
    if (hasCapability(ContextCap::TextureGetters)) {
        return queryTextureLevelGetters(tex, level, layer, face, defAlignment);
    } else {
        return queryTextureLevelAuxCtx(tex, level, layer, face);
    }
}

bool GLContext::textureProbeSizeES(GLenum levelTarget, int level, GLenum formatToRequest, GLenum typeToRequest,
                                   const int sizes[3]) {

    int nothing = 0;

    if (isTexture1Dim(levelTarget)) {

        DIRECT_CALL_CHK(glTexSubImage1D)(levelTarget, level, sizes[0], 0,
                                         formatToRequest, typeToRequest, &nothing);

    } else if (isTexture2Dim(levelTarget)) {

        DIRECT_CALL_CHK(glTexSubImage2D)(levelTarget, level, sizes[0], sizes[1],
                                         0, 0, formatToRequest, typeToRequest,
                                         &nothing);

    } else {

        DIRECT_CALL_CHK(glTexSubImage3D)(levelTarget, level, sizes[0], sizes[1],
                                         sizes[2], 0, 0, 0, formatToRequest,
                                         typeToRequest, &nothing);
    }

    GLenum error = DIRECT_CALL_CHK(glGetError)();
    if (error == GL_NO_ERROR) {
        return true;
    }
    while (DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR) {
    }

    return false;
}

int GLContext::textureBisectSizeES(GLenum levelTarget, int level, int coord,
                                   int maxSize, GLenum formatToRequest, GLenum typeToRequest) {
    int sizes[3] = {0, 0, 0};

    int minSize = 0;

    // must check for texture dimensionality here, because
    // bisecting for not existent coord will always return max.
    if (isTexture1Dim(levelTarget) && coord > 0) {
        return 1;
    }
    if (isTexture2Dim(levelTarget) && coord > 1) {
        return 1;
    }

    while (true) {
        int middle = (minSize + maxSize) / 2;
        if (maxSize - minSize <= 1) {
            sizes[coord] = maxSize;
            if (textureProbeSizeES(levelTarget, level, formatToRequest, typeToRequest, sizes))
                return maxSize;
            else
                return minSize;
        }
        sizes[coord] = middle;
        if (textureProbeSizeES(levelTarget, level, formatToRequest, typeToRequest, sizes)) {
            minSize = middle;
        } else {
            maxSize = middle;
        }
    }
}

std::shared_ptr<dglnet::resource::DGLPixelRectangle>
GLContext::queryTextureLevelAuxCtx(const GLTextureObj* tex, int level,
                                   int layer, size_t face) {

    std::shared_ptr<dglnet::resource::DGLPixelRectangle> ret;

    queryCheckError();

    GLint width, height, depth;
    queryTextureLevelSize(tex, level, &width, &height, &depth);

    if (!width || !height || depth <= layer) {
        return nullptr;
    }

    try {
        GLAuxContext* auxCtx = getAuxContext();
        {
            GLAuxContextSession auxsess = auxCtx->createAuxCtxSession();

            //the format of RT, where texture will be rendered to now
            GLenum renderableFormat = GL_RGBA4;

            //the base format of queried texture
            GLenum textureBaseFormat = GL_RGBA;

            const GLTextureObj::GLTextureLevel* levelDesc =
                    tex->getRequestedLevel(level);

            if (levelDesc) {

                renderableFormat = static_cast<GLenum>(
                        GLFormats::getBestColorRenderableFormatES(
                                levelDesc->m_RequestedInternalFormat,
                                levelDesc->m_RequestedDataType,
                                getVersion().getMajor()));
                const GLInternalFormat* internalFormatDesc =
                        GLFormats::getInternalFormat(
                                levelDesc->m_RequestedInternalFormat);
                if (internalFormatDesc) {
                    textureBaseFormat =
                            static_cast<GLenum>(internalFormatDesc->dataFormat);
                } else {
                    OS_DEBUG(
                            "Cannot recognize texture internalFormat: %d. "
                            "Assuming texture base format is GL_RGBA.",
                            levelDesc->m_RequestedInternalFormat);
                }
            } else {
                OS_DEBUG(
                        "Cannot get requested texture level parameters: level "
                        "was not requested via TexImage? Assuming texture is "
                        "RGBA, will render it to RGBA4 attachment.");
            }

            auxCtx->queries.auxDrawTexture(
                    (GLuint)tex->getName(), tex->getTarget(), level, layer, face,
                    textureBaseFormat, renderableFormat, width, height);

            DGLPixelTransfer transfer;
            if (getVersion().check(GLContextVersion::Type::ES)) {
                GLint implReadFormat, implTypeType;
                DIRECT_CALL_CHK(glGetIntegerv)(
                        GL_IMPLEMENTATION_COLOR_READ_FORMAT, &implReadFormat);
                DIRECT_CALL_CHK(glGetIntegerv)(
                        GL_IMPLEMENTATION_COLOR_READ_TYPE, &implTypeType);
                transfer.initializeOGLES(renderableFormat, implReadFormat,
                                         implTypeType);
            } else {
                transfer.initializeOGL(renderableFormat);
            }

            ret = std::shared_ptr<dglnet::resource::DGLPixelRectangle>(
                new dglnet::resource::DGLPixelRectangle(
                    width, height, DGL_ALIGNED(width * transfer.getPixelSize(), 4),
                    transfer.getFormat(), transfer.getType()));

            DIRECT_CALL_CHK(glReadPixels)(
                    0, 0, width, height, (GLenum)transfer.getFormat(),
                    (GLenum)transfer.getType(), ret->getPtr());

            if (DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR) {
                throw std::runtime_error("Got GL error on auxiliary context");
            }
            auxsess.dispose();
        }
    }
    catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(
                                         "Texture query : Got error on "
                                         "auxiliary context operation:\n") +
                                 e.what());
    }

    return ret;
}

std::shared_ptr<dglnet::resource::DGLPixelRectangle>
GLContext::queryTextureLevelGetters(
        const GLTextureObj* tex, int level, int layer, size_t face,
        state_setters::PixelStoreAlignment& defAlignment) {

    GLint height, width, depth;

    GLenum levelTarget = tex->getTextureLevelTarget(face);

    std::shared_ptr<dglnet::resource::DGLPixelRectangle> ret;

    queryTextureLevelSize(tex, level, &width, &height, &depth);

    if (!width || !height || depth <= layer || DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR) {
        return nullptr;
    }

    std::vector<GLint> rgbaSizes(GLFormats::kNumChannelsRGBA, 0);
    DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
            levelTarget, level, GL_TEXTURE_RED_SIZE, &rgbaSizes[0]);
    DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
            levelTarget, level, GL_TEXTURE_GREEN_SIZE, &rgbaSizes[1]);
    DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
            levelTarget, level, GL_TEXTURE_BLUE_SIZE, &rgbaSizes[2]);
    DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
            levelTarget, level, GL_TEXTURE_ALPHA_SIZE, &rgbaSizes[3]);

    std::vector<GLint> deptStencilSizes(GLFormats::kNumChannelsDS, 0);
    DIRECT_CALL_CHK(glGetTexLevelParameteriv)(
            levelTarget, level, GL_TEXTURE_DEPTH_SIZE, &deptStencilSizes[0]);

    if (hasCapability(ContextCap::TextureQueryStencilBits)) {
        DIRECT_CALL_CHK(glGetTexLevelParameteriv)(levelTarget, level,
                                                  GL_TEXTURE_STENCIL_SIZE,
                                                  &deptStencilSizes[1]);
    }

    queryCheckError();

    GLint internalFormat, samples;
    tex->getFormat(this, level, levelTarget, internalFormat, samples);

    

    bool multisampled = (levelTarget == GL_TEXTURE_2D_MULTISAMPLE || levelTarget == GL_TEXTURE_2D_MULTISAMPLE_ARRAY);

    if (!multisampled) {

        //glGetTexImage path
        DGLPixelTransfer transfer;
        if (getVersion().check(GLContextVersion::Type::ES)) {
            GLint implReadFormat, implTypeType;
            DIRECT_CALL_CHK(glGetIntegerv)(GL_IMPLEMENTATION_COLOR_READ_FORMAT,
                &implReadFormat);
            DIRECT_CALL_CHK(glGetIntegerv)(GL_IMPLEMENTATION_COLOR_READ_TYPE,
                &implTypeType);
            transfer.initializeOGLES(internalFormat, implReadFormat, implTypeType);
        } else {
            transfer.initializeOGL(internalFormat, rgbaSizes, deptStencilSizes);
        }

        ret = std::shared_ptr<dglnet::resource::DGLPixelRectangle>(
            new dglnet::resource::DGLPixelRectangle(
            width, height,
            defAlignment.getAligned(width * transfer.getPixelSize()),
            transfer.getFormat(), transfer.getType()));

        GLvoid* ptr;
        if ((ptr = ret->getPtr()) != NULL) {

            std::vector<uint8_t> tmpBuffer;
            uint8_t* readPtr; 

            const size_t layerSize = ret->getSize();

            if (depth == 1) {
                //only one 2D layer, read it in place
                readPtr = reinterpret_cast<uint8_t*>(ptr); 
            } else {
                //more 2D layers present, prepare a tmp buffer for them
                tmpBuffer.resize(layerSize * static_cast<size_t>(depth));
                readPtr = &tmpBuffer[0];
            }

            DIRECT_CALL_CHK(glGetTexImage)(levelTarget, level,
                (GLenum)transfer.getFormat(),
                (GLenum)transfer.getType(), readPtr);

            if (readPtr != ptr) {
                memcpy(ptr, &readPtr[static_cast<size_t>(layer) * layerSize], layerSize);
            }

        }
    } else {
        //downsample MSAA && glReadPixels path.

        //save fbo bindings
        state_setters::ReadBuffer readBuffer(this);
        state_setters::DrawBuffers drawBuffers(this);
        

        GLuint fbo;
        DIRECT_CALL_CHK(glGenFramebuffers)(1, &fbo);
        try {
            state_setters::CurrentFramebuffer currentFBO(this, fbo);

            DIRECT_CALL_CHK(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, fbo);
            if (levelTarget == GL_TEXTURE_2D_MULTISAMPLE) {
                DIRECT_CALL_CHK(glFramebufferTexture2D)(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, levelTarget, tex->getName(), level);
            } else if (levelTarget == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
                DIRECT_CALL_CHK(glFramebufferTextureLayer)(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex->getName(), level, layer);
            } else {
                DGL_ASSERT(0);
            }

            DGLPixelTransfer downsamplerTransfer;
            downsamplerTransfer.initializeOGL(internalFormat, rgbaSizes,
                deptStencilSizes);
            glutils::MSAADownSampler downSampler(this, levelTarget, GL_COLOR_ATTACHMENT0, fbo, internalFormat,
                &downsamplerTransfer, width, height);

            DIRECT_CALL_CHK(glBindFramebuffer)(
                GL_READ_FRAMEBUFFER, downSampler.getDownsampledFBO());

            DGLPixelTransfer transfer;
            if (getVersion().check(GLContextVersion::Type::ES)) {
                GLint implReadFormat, implTypeType;
                DIRECT_CALL_CHK(glGetIntegerv)(
                    GL_IMPLEMENTATION_COLOR_READ_FORMAT, &implReadFormat);
                DIRECT_CALL_CHK(glGetIntegerv)(
                    GL_IMPLEMENTATION_COLOR_READ_TYPE, &implTypeType);
                transfer.initializeOGLES(
                    internalFormat, implReadFormat, implTypeType);
            } else {
                transfer.initializeOGL(internalFormat, rgbaSizes,
                    deptStencilSizes);
            }

            ret = std::make_shared<dglnet::resource::DGLPixelRectangle>(
                width, height, defAlignment.getAligned(
                width * transfer.getPixelSize()),
                transfer.getFormat(), transfer.getType());

            GLvoid* ptr = ret->getPtr();
            if (ptr) {
                DIRECT_CALL_CHK(glReadPixels)(0, 0, width, height,
                    (GLenum)transfer.getFormat(),
                    (GLenum)transfer.getType(), ptr);
            }
        } catch (const std::runtime_error& e) {
            DIRECT_CALL_CHK(glDeleteFramebuffers)(1, &fbo);
            throw e;
        }
        DIRECT_CALL_CHK(glDeleteFramebuffers)(1, &fbo);
    }
    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryBufferGetters(GLBufferObj* buff) {

    dglnet::resource::DGLResourceBuffer* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
        resource = new dglnet::resource::DGLResourceBuffer());

    //Bindging point target to use for reading buffer
    //May be the same as last buffer target (to avoid redbinds), 
    //but if uknown can be different
    GLenum targetToUse = buff->getTarget();
    
    // rebind buffer, so we can access it
    GLint i;
    switch (buff->getTarget()) {
        case GL_ARRAY_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_ARRAY_BUFFER_BINDING, &i);
            break;
        case GL_ATOMIC_COUNTER_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_ATOMIC_COUNTER_BUFFER_BINDING,
                                           &i);
            break;
        case GL_COPY_READ_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_COPY_READ_BUFFER_BINDING, &i);
            break;
        case GL_COPY_WRITE_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_COPY_WRITE_BUFFER_BINDING, &i);
            break;
        case GL_DRAW_INDIRECT_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_DRAW_INDIRECT_BUFFER_BINDING, &i);
            break;
        case GL_DISPATCH_INDIRECT_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_DISPATCH_INDIRECT_BUFFER_BINDING,
                                           &i);
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_ELEMENT_ARRAY_BUFFER_BINDING, &i);
            break;
        case GL_PIXEL_PACK_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_PIXEL_PACK_BUFFER_BINDING, &i);
            break;
        case GL_PIXEL_UNPACK_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_PIXEL_UNPACK_BUFFER_BINDING, &i);
            break;
        case GL_SHADER_STORAGE_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_SHADER_STORAGE_BUFFER_BINDING,
                                           &i);
            break;
        /*case GL_TEXTURE_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_NO_IDEA_WHAT_SHOULD_BE_HERE,
           &lastBuffer);
            break;*/
        case GL_TRANSFORM_FEEDBACK_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,
                                           &i);
            break;
        case GL_UNIFORM_BUFFER:
            DIRECT_CALL_CHK(glGetIntegerv)(GL_UNIFORM_BUFFER_BINDING, &i);
            break;
        default:
            //Last buffer target was not recognized. GL_ARRAY_BUFFER will be used as target
            targetToUse = GL_ARRAY_BUFFER;
            DIRECT_CALL_CHK(glGetIntegerv)(GL_ARRAY_BUFFER_BINDING, &i);
    }
    GLuint lastBuffer = static_cast<GLuint>(i);
    if (lastBuffer != buff->getName()) {
        DIRECT_CALL_CHK(glBindBuffer)(targetToUse, buff->getName());
    }

    queryCheckError();

    GLint size = 0;
    DIRECT_CALL_CHK(glGetBufferParameteriv)(targetToUse,
                                            GL_BUFFER_SIZE, &size);

    queryCheckError();

    if (!size) {
        throw std::runtime_error("Buffer empty (GL_BUFFER_SIZE is 0)");
    } else {
        resource->m_Data.resize(static_cast<size_t>(size));

        //Set to 1 if buffer is currently mapped
        GLint mapped = 0;

        //True if buffer is mapped and no other GL operations
        //can be performed because of the mapping.
        bool mappedNonPersitently = false;

        //True if buffer is mapped, and it's whole data is visible
        //in the mapping.
        bool canReadFromMapping = false;

        if (hasCapability(ContextCap::MapBuffer)) {

            //Check if buffer is mapped
            DIRECT_CALL_CHK(glGetBufferParameteriv)(targetToUse, GL_BUFFER_MAPPED,
                    &mapped);

            if (mapped) {
                //Check how the buffer is mapped.
                GLint   accessFlags = 0;
                DIRECT_CALL_CHK(glGetBufferParameteriv)(targetToUse, GL_BUFFER_ACCESS_FLAGS, &accessFlags);

                GLint64 mapOffset = 0;
                DIRECT_CALL_CHK(glGetBufferParameteri64v)(targetToUse, GL_BUFFER_MAP_OFFSET, &mapOffset);

                GLint64 mapLength = 0;
                DIRECT_CALL_CHK(glGetBufferParameteri64v)(targetToUse, GL_BUFFER_MAP_LENGTH, &mapLength);

                mappedNonPersitently = !(accessFlags & GL_MAP_PERSISTENT_BIT);
                canReadFromMapping = ( (mapOffset == 0) &&
                                       (mapLength == size) &&
                                       (accessFlags && GL_MAP_READ_BIT));
            }
        }

        if (hasCapability(ContextCap::GetBufferSubData) && !mappedNonPersitently) {

            //This is preferred. If GetBufferSubdata is supported, just use it.
            //However it buffer has been mapped without persistent flag, we cannot take this branch.

            DIRECT_CALL_CHK(glGetBufferSubData)(targetToUse, 0, static_cast<GLsizeiptr>(size),
                &resource->m_Data[0]);

        } else if (canReadFromMapping) {

            //This is used if buffer is mapped and the mapping is usable.
            //
            GLvoid* ptr = nullptr;
            DIRECT_CALL_CHK(glGetBufferPointerv)(targetToUse, GL_BUFFER_MAP_POINTER, &ptr);
            if (!ptr) {
                throw std::runtime_error("Cannot perform query - GL_BUFFER_MAP_POINTER is null");
            }

            GLchar* ptrC = reinterpret_cast<GLchar*>(ptr);

            //Just get the data from mapping.
            std::copy(ptrC, ptrC + static_cast<size_t>(size), resource->m_Data.begin());

        } else if (!mapped && hasCapability(ContextCap::MapBuffer)) {

            //This is used if buffer is not  mapped.
            const char* ptr = reinterpret_cast<const char*>(
                DIRECT_CALL_CHK(glMapBufferRange)(targetToUse, 0, static_cast<GLsizeiptr>(size), GL_MAP_READ_BIT));
            std::copy(ptr, ptr + static_cast<size_t>(size), resource->m_Data.begin());
            DIRECT_CALL_CHK(glUnmapBuffer)(targetToUse);

        } else {
            throw std::runtime_error("Cannot perform query - operation unsupported on current buffer state (mapping)");
        }
    }

    // restore state
    if (lastBuffer != buff->getName()) {
        DIRECT_CALL_CHK(glBindBuffer)(targetToUse, lastBuffer);
    }

    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryBufferAuxCtx(GLBufferObj* buff) { 

    dglnet::resource::DGLResourceBuffer* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
        resource = new dglnet::resource::DGLResourceBuffer());

    GLAuxContext* auxCtx = getAuxContext();
    {
        GLAuxContextSession auxsess = auxCtx->createAuxCtxSession();

        auxCtx->queries.auxGetBufferData(buff->getName(), resource->m_Data);

        auxsess.dispose();
    }


    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryBuffer(gl_t _name) {

    GLuint name = static_cast<GLuint>(_name);

    // check if GL knows about a texture
    if (DIRECT_CALL_CHK(glIsBuffer)(name) != GL_TRUE) {
        throw std::runtime_error("Buffer does not exist");
    }

    // check if we know about a texture target
    std::unique_ptr<GLShareableObjectsAccessor> accessor  = ns().getShared();
    GLBufferObj* buff = accessor->get().m_Buffers.getOrCreateObject<void>(name);

    if (hasCapability(ContextCap::GetBufferSubData) || hasCapability(ContextCap::MapBuffer) ) {
        return queryBufferGetters(buff);

    } else {
        return queryBufferAuxCtx(buff);
    }
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryFramebuffer(
        gl_t _bufferEnum) {

    GLuint bufferEnum = static_cast<GLuint>(_bufferEnum);

    dglnet::resource::DGLResourceFramebuffer* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceFramebuffer);

    if (!m_NativeReadSurface) {
        throw std::runtime_error("Buffer does not exist");
    }
    state_setters::ReadBuffer readBuffer(this);
    state_setters::DefaultPBO defPBO(this);
    state_setters::CurrentFramebuffer currentFramebuffer(this, 0);
    state_setters::PixelStoreAlignment defAlignment(this);

    if (!hasCapability(ContextCap::ReadBuffersFrontBuffer) && bufferEnum != GL_BACK) {
        throw std::runtime_error("Current context does not have front buffer read capability.");
    }

    // select read buffer
    if (hasCapability(ContextCap::ReadBuffer)) {
        DIRECT_CALL_CHK(glReadBuffer)(bufferEnum);
    }

    std::vector<GLint> rgbaSizes(m_NativeReadSurface->getRGBASizes(),
                                 m_NativeReadSurface->getRGBASizes() + 4);
    std::vector<GLint> deptStencilSizes;

    deptStencilSizes.push_back(m_NativeReadSurface->getDepthSize());
    deptStencilSizes.push_back(m_NativeReadSurface->getStencilSize());

    int width = m_NativeReadSurface->getWidth();
    int height = m_NativeReadSurface->getHeight();

    // we cannot reliably get internalformat for default framebuffer, so it is 0
    // here.
    DGLPixelTransfer transfer;
    if (getVersion().check(GLContextVersion::Type::ES)) {
        GLint implReadFormat, implTypeType;
        DIRECT_CALL_CHK(glGetIntegerv)(GL_IMPLEMENTATION_COLOR_READ_FORMAT,
                                       &implReadFormat);
        DIRECT_CALL_CHK(glGetIntegerv)(GL_IMPLEMENTATION_COLOR_READ_TYPE,
                                       &implTypeType);
        transfer.initializeOGLES(0, implReadFormat, implTypeType);
    } else {
        transfer.initializeOGL(0, rgbaSizes, deptStencilSizes);
    }

    resource->m_PixelRectangle =
            std::shared_ptr<dglnet::resource::DGLPixelRectangle>(
                new dglnet::resource::DGLPixelRectangle(
                    width, height,
                    defAlignment.getAligned(width * transfer.getPixelSize()),
                    transfer.getFormat(), transfer.getType()));
#pragma message("GLContext::queryFramebuffer: query MSAA")

    GLvoid* ptr;
    if ((ptr = resource->m_PixelRectangle->getPtr()) != NULL)
        DIRECT_CALL_CHK(glReadPixels)(0, 0, width, height,
                                      (GLenum)transfer.getFormat(),
                                      (GLenum)transfer.getType(), ptr);

    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryFBO(gl_t _name) {

    GLuint name = static_cast<GLuint>(_name);

    dglnet::resource::DGLResourceFBO* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceFBO());


    // switch to queried FBO
    state_setters::CurrentFramebuffer currentFBO(this, name);

    // get maximum number of color attachments
    GLint maxColorAttachments;

    if (hasCapability(ContextCap::MultipleFramebufferAttachments)) {
        DIRECT_CALL_CHK(glGetIntegerv)(GL_MAX_COLOR_ATTACHMENTS,
                                       &maxColorAttachments);
    } else {
        maxColorAttachments = 1;
    }

    // fill table with color attachments to look for
    std::vector<GLenum> attachments(maxColorAttachments);
    for (size_t i = 0; i < static_cast<size_t>(maxColorAttachments); i++) {
        attachments[i] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
    }

    // additionally we will check some non-color attachments

    // it is crucial, that depth_stencil is checked first (we will break is
    // succeed)
    attachments.push_back(GL_DEPTH_STENCIL_ATTACHMENT);

    // if depth_stencil fails, we will also check these:
    attachments.push_back(GL_DEPTH_ATTACHMENT);
    attachments.push_back(GL_STENCIL_ATTACHMENT);

    bool hasDepthStencilAttachment = false;

    resource->m_CompletenessStatus =
        DIRECT_CALL_CHK(glCheckFramebufferStatus)(GL_FRAMEBUFFER);

    queryCheckError();

    for (size_t i = 0; i < attachments.size(); i++) {

        if ((attachments[i] == GL_DEPTH_ATTACHMENT || 
             attachments[i] == GL_STENCIL_ATTACHMENT ) && hasDepthStencilAttachment) {

            //skip filling in DEPTH & SENCIL attachments, if DEPTH_STENCIL was already read.
            continue;
        }

        // check attached object type
        GLint type;
        DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                GL_FRAMEBUFFER, attachments[i],
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);

        if (DIRECT_CALL_CHK(glGetError)() != GL_NO_ERROR) {
            // sometimes the query above may fail, for example when asking about
            // depth_stencil attachment, when depth attachment != stencil
            // attachment
            continue;
        }

        if (type != GL_TEXTURE && type != GL_RENDERBUFFER) {
            // no object attached:  skip
            continue;
        }

        // it looks, like there is an attachment. Add it to returned list
        resource->m_Attachments.push_back(
                dglnet::resource::DGLResourceFBO::FBOAttachment(
                        attachments[i]));

        bool isDSQueryOnES = getVersion().check(GLContextVersion::Type::ES) &&
            ( attachments[i] == GL_DEPTH_ATTACHMENT      ||
              attachments[i] == GL_STENCIL_ATTACHMENT    ||
              attachments[i] == GL_DEPTH_STENCIL_ATTACHMENT);


        try {

            //read framebuffer contents only if it is complete.
            //Otherwise INVALID_OP may happen.
            bool complete = (resource->m_CompletenessStatus == GL_FRAMEBUFFER_COMPLETE);

            bool queryPixels = (complete && !isDSQueryOnES);

            GLint samples = 0;
            GLint internalFormat = 0;

            std::shared_ptr<dglnet::resource::DGLPixelRectangle> pixelRectangle = 
                    queryFramebufferAttachment(
                    name,                      // FBO name
                    attachments[i],            // attachment point
                    type,                      // attachment type (texture or renderbuffer)
                    queryPixels,               // true if pixels should/can be queried
                    &samples,                  // sample count is returned here
                    &internalFormat            // internalFormat is returned here
                );

            dglnet::resource::DGLResourceFBO::FBOAttachment& attachment = resource->m_Attachments.back();

            attachment.m_PixelRectangle = pixelRectangle;
               

            DGL_ASSERT(queryPixels == (attachment.m_PixelRectangle != nullptr));

            attachment.m_Samples = samples;
            attachment.m_Internalformat = internalFormat;

            if (!complete) {
                attachment.error(
                    "Cannot query pixels of incomplete FBO");
            }

            if (isDSQueryOnES) {
                attachment.error(
                    "Cannot query pixels of depth or stencil buffers on OpenGL ES");
            }

            if (attachments[i] == GL_DEPTH_STENCIL_ATTACHMENT) {

                //will prevent further DEPTH & STENCIL attachments processing.
                hasDepthStencilAttachment = true; 
            }
        } catch (const std::runtime_error& e) {
            resource->m_Attachments.back().error(e.what());
        }
        
    }

    return ret;
}

std::shared_ptr<dglnet::resource::DGLPixelRectangle> GLContext::queryFramebufferAttachment(
        GLuint fboObject,
        GLenum attachment,
        GLenum attachmentType,
        bool queryPixels,
        GLint* outSamples,
        GLint* outInternalFormat) {

     DGL_ASSERT(outSamples);
     DGL_ASSERT(outInternalFormat);

     //switch to queried FBO
     state_setters::CurrentFramebuffer currentFBO(this, fboObject);
          
     GLint attachmentObject;

     DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
         GL_FRAMEBUFFER, attachment,
         GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachmentObject);

     // now look for the attached object and query internal format and
     // dimensions
     GLint width = 0, height = 0;
     GLenum attTarget = 0;
     bool multisampled = false;

     std::vector<GLint> rgbaSizes(GLFormats::kNumChannelsRGBA, 0);
     std::vector<GLint> deptStencilSizes(2, 0);

     if (attachmentType == GL_TEXTURE) {

         if (!DIRECT_CALL_CHK(glIsTexture)(attachmentObject)) {
             throw std::runtime_error("Attached texture object does not exist");
         }

         GLTextureObj* tex = ns().getShared()->get().m_Textures.getObject(attachmentObject);
         attTarget = tex->getTarget();

         GLenum bindableTarget =
             glutils::textTargetToBindableTarget(attTarget);

         if (attTarget == GL_TEXTURE_2D_MULTISAMPLE ||
             attTarget == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
             multisampled = true;

         GLint level;
         DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
             GL_FRAMEBUFFER, attachment,
             GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);

         GLuint lastTexture;
         if (!glutils::getBoundTexture(bindableTarget, lastTexture)) {
             throw std::runtime_error(
                 "Cannot get actually bound texture name");
         }
         DIRECT_CALL_CHK(glBindTexture)(bindableTarget, tex->getName());

         queryTextureLevelSize(tex, level, &width, &height, NULL);

         {
             tex->getFormat(this, level, attTarget, *outInternalFormat, *outSamples);

             // if internalFormat is returned as 0 (happens if not traced on ES), query will
             // probably fail (no way to discover format, or bit sizes).
             DGL_ASSERT(*outInternalFormat);
         }

         DIRECT_CALL_CHK(glBindTexture)(bindableTarget, lastTexture);

     } else if (attachmentType == GL_RENDERBUFFER) {

         attTarget = GL_RENDERBUFFER;



         if (!DIRECT_CALL_CHK(glIsRenderbuffer)(attachmentObject)) {
             throw std::runtime_error(
                 "Attached renderbuffer object does not exist");
         }

         state_setters::RenderBuffer renderBuffer;


         DIRECT_CALL_CHK(glBindRenderbuffer)(GL_RENDERBUFFER, attachmentObject);

         DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
             GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
         DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
             GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
         DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
             GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT,
             outInternalFormat);

         if (hasCapability(ContextCap::RenderBufferMultisample)) {
             DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                 GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, outSamples);
         }

         if (outSamples) {
             multisampled = true;
         }

         if (!hasCapability(
             ContextCap::CanQueryFramebufferAttachmentBitSize)) {
                 if (attachment == GL_DEPTH_ATTACHMENT) {

                     DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                         GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE,
                         &deptStencilSizes[0]);

                 } else if (attachment == GL_STENCIL_ATTACHMENT) {

                     DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                         GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE,
                         &deptStencilSizes[1]);

                 } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {

                     DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                         GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE,
                         &deptStencilSizes[0]);
                     DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                         GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE,
                         &deptStencilSizes[1]);
                 } else {
                     //color attachment

                    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                        GL_RENDERBUFFER, GL_RENDERBUFFER_RED_SIZE,
                        &rgbaSizes[0]);
                    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                        GL_RENDERBUFFER, GL_RENDERBUFFER_GREEN_SIZE,
                        &rgbaSizes[1]);
                    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                        GL_RENDERBUFFER, GL_RENDERBUFFER_BLUE_SIZE,
                        &rgbaSizes[2]);
                    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
                        GL_RENDERBUFFER, GL_RENDERBUFFER_ALPHA_SIZE,
                        &rgbaSizes[3]);
                 } 
         }
     } else {
         DGL_ASSERT(0);
     }

     // there should be no errors. Otherwise something nasty happened
     queryCheckError();

     if (hasCapability(ContextCap::CanQueryFramebufferAttachmentBitSize)) {
         if (attachment == GL_DEPTH_ATTACHMENT) {

             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,
                 &deptStencilSizes[0]);

         } else if (attachment == GL_STENCIL_ATTACHMENT) {

             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
                 &deptStencilSizes[1]);

         } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {

             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,
                 &deptStencilSizes[0]);
             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
                 &deptStencilSizes[1]);
         } else {

             //color attachment
             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &rgbaSizes[0]);
             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &rgbaSizes[1]);
             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &rgbaSizes[2]);
             DIRECT_CALL_CHK(glGetFramebufferAttachmentParameteriv)(
                 GL_FRAMEBUFFER, attachment,
                 GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &rgbaSizes[3]);
         }
     }


     if (queryPixels && width && height) {

         state_setters::DefaultPBO defPBO(this);

         // we may touch draw buffer when downsampling MSAA buffers
         state_setters::DrawBuffers drawBuffers(this);
         
         // we need to pixeltransfer alignmnet to default
         state_setters::PixelStoreAlignment defAlignment(this);

         //Internal format should be adjusted to cover only buffers attached to fbo.
         GLenum transferInternalFormat = *outInternalFormat;
         if (attachment == GL_DEPTH_ATTACHMENT) {
             const GLInternalFormat* internalFormatDescr = GLFormats::getInternalFormat(*outInternalFormat);
             if (internalFormatDescr && internalFormatDescr->dataFormat == GL_DEPTH_STENCIL) {
                 transferInternalFormat = GLFormats::getDepthInternalformatFromDepthStencil(internalFormatDescr);
             }
         } else if (attachment == GL_STENCIL_ATTACHMENT) {
             const GLInternalFormat* internalFormatDescr = GLFormats::getInternalFormat(*outInternalFormat);
             if (internalFormatDescr && internalFormatDescr->dataFormat == GL_DEPTH_STENCIL) {
                 transferInternalFormat = GLFormats::getStencilInternalformatFromDepthStencil(internalFormatDescr);
             }
         }

         std::shared_ptr<glutils::MSAADownSampler> downSampler;
         if (multisampled) {

             DGLPixelTransfer downsamplerTransfer;
             if (getVersion().check(GLContextVersion::Type::ES)) {
                 GLint implReadFormat, implTypeType;
                 DIRECT_CALL_CHK(glGetIntegerv)(
                     GL_IMPLEMENTATION_COLOR_READ_FORMAT, &implReadFormat);
                 DIRECT_CALL_CHK(glGetIntegerv)(
                     GL_IMPLEMENTATION_COLOR_READ_TYPE, &implTypeType);
                 downsamplerTransfer.initializeOGLES(
                     transferInternalFormat, implReadFormat, implTypeType);
             } else {
                 downsamplerTransfer.initializeOGL(transferInternalFormat, rgbaSizes,
                     deptStencilSizes);
             }

             downSampler = std::shared_ptr<glutils::MSAADownSampler>(
                 new glutils::MSAADownSampler(
                 this, attTarget, attachment, fboObject, transferInternalFormat,
                 &downsamplerTransfer, width, height));
             DIRECT_CALL_CHK(glBindFramebuffer)(
                 GL_READ_FRAMEBUFFER, downSampler->getDownsampledFBO());
         }

         if (attachment != GL_DEPTH_ATTACHMENT &&
             attachment != GL_STENCIL_ATTACHMENT &&
             attachment != GL_DEPTH_STENCIL_ATTACHMENT) {
                 // select color attachment
                 if (hasCapability(ContextCap::ReadBuffer)) {
                     DIRECT_CALL_CHK(glReadBuffer)(attachment);
                 }
         }

         DGLPixelTransfer transfer;
         if (getVersion().check(GLContextVersion::Type::ES)) {
             GLint implReadFormat, implTypeType;
             DIRECT_CALL_CHK(glGetIntegerv)(GL_IMPLEMENTATION_COLOR_READ_FORMAT,
                 &implReadFormat);
             DIRECT_CALL_CHK(glGetIntegerv)(GL_IMPLEMENTATION_COLOR_READ_TYPE,
                 &implTypeType);
             transfer.initializeOGLES(transferInternalFormat, implReadFormat,
                 implTypeType);
         } else {
             transfer.initializeOGL(transferInternalFormat, rgbaSizes, deptStencilSizes);
         }

         std::shared_ptr<dglnet::resource::DGLPixelRectangle> ret = 
             std::make_shared<dglnet::resource::DGLPixelRectangle>(
             width, height, defAlignment.getAligned(
             width * transfer.getPixelSize()),
             transfer.getFormat(), transfer.getType());

         GLvoid* ptr = ret->getPtr();
         if (ptr) {
             DIRECT_CALL_CHK(glReadPixels)(0, 0, width, height,
                 (GLenum)transfer.getFormat(),
                 (GLenum)transfer.getType(), ptr);
         }

         // there should be no errors. Otherwise something nasty happened
         queryCheckError();

         return ret;

     } else { //!queryPixels && width && height
         return std::shared_ptr<dglnet::resource::DGLPixelRectangle>();
     }
}


std::shared_ptr<dglnet::DGLResource> GLContext::queryRenderbuffer(gl_t name) {

    dglnet::resource::DGLResourceRenderbuffer* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
        resource = new dglnet::resource::DGLResourceRenderbuffer);

    //we are going to change renderbuffer binding
    state_setters::RenderBuffer renderBuffer;

    if (!DIRECT_CALL_CHK(glIsRenderbuffer)(static_cast<GLuint>(name))) {
        throw std::runtime_error("Renderbuffer does not exist");
    }

    DIRECT_CALL_CHK(glBindRenderbuffer)(GL_RENDERBUFFER, static_cast<GLuint>(name));

    GLint width, height, internalFormat;

    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
        GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
        GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
        GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT,
        &internalFormat);

    // there should be no errors. Otherwise something nasty happened
    queryCheckError();

    GLenum attachment = GL_COLOR_ATTACHMENT0;
    
    const GLInternalFormat* iFormat  = GLFormats::getInternalFormat(internalFormat);

    bool isDSQuery = true;

    if (iFormat) {

        //Internal format recognized. Set attachment point basing on it.

        if (iFormat->dataFormat == GL_DEPTH_COMPONENT) {
            attachment = GL_DEPTH_ATTACHMENT;
        } else if (iFormat->dataFormat == GL_STENCIL_INDEX)  {
            attachment = GL_STENCIL_ATTACHMENT;
        } else if (iFormat->dataFormat == GL_DEPTH_STENCIL) {
            attachment = GL_DEPTH_STENCIL_ATTACHMENT;
        } else {
            isDSQuery = false;
        }
    } else {

        //Internalformat not recognized

        //Set attachment basing on bit sizes
        std::vector<GLint> deptStencilSizes(2, 0);
        DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
            GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &deptStencilSizes[0]);
        DIRECT_CALL_CHK(glGetRenderbufferParameteriv)(
            GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &deptStencilSizes[1]);

        if (deptStencilSizes[0] > 0 && deptStencilSizes[1] > 0) {
            attachment = GL_DEPTH_STENCIL_ATTACHMENT;
        } else if (deptStencilSizes[1] > 0)  {
            attachment = GL_STENCIL_ATTACHMENT;
        } else if (deptStencilSizes[0] > 0) {
            attachment = GL_DEPTH_ATTACHMENT;
        } else {
            isDSQuery = false;
        }
    }   
    

    if (isDSQuery && getVersion().check(GLContextVersion::Type::ES)) {
        throw std::runtime_error("Cannot query pixels of depth or stencil buffers on OpenGL ES");
    }

    GLuint dummyFBO;

    try {

        //Setup the dummy FBO
        DIRECT_CALL_CHK(glGenFramebuffers)(1, &dummyFBO);
        state_setters::CurrentFramebuffer currentFramebuffer(this, dummyFBO);
        DIRECT_CALL_CHK(glFramebufferRenderbuffer)(GL_FRAMEBUFFER,
                                                   attachment,
                                                   GL_RENDERBUFFER,
                                                   static_cast<GLuint>(name));
        GLint samples;

        // there should be no errors. Otherwise something nasty happened
        queryCheckError();

        std::shared_ptr<dglnet::resource::DGLPixelRectangle> pixelRectangle = 
                queryFramebufferAttachment(
                dummyFBO,             // id of fbo containing queried RB
                attachment,           // attachment point, as above
                GL_RENDERBUFFER,      // it is a renderbuffer attachment
                true,                 // true (always query pixels)
                &samples,             // sample count is returned here
                &internalFormat       // internalFormat is returned here
            );

        resource->m_PixelRectangle = pixelRectangle;
            

        resource->m_Internalformat = internalFormat;
        resource->m_Samples = samples;

        
    } catch (const std::runtime_error& e) {

        DIRECT_CALL_CHK(glDeleteFramebuffers)(1, &dummyFBO);

        throw e;
    }

    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryShader(gl_t _name) {

    GLuint name = static_cast<GLuint>(_name);

    dglnet::resource::DGLResourceShader* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceShader);

    GLShaderObj* shader = ns().m_Shaders.getObject(name);
    if (!shader) {
        throw std::runtime_error("Shader does not exist");
    }

    resource->m_CompileStatus =
            std::pair<std::string, gl_t>(shader->queryCompilationInfoLog(),
                                         shader->queryCompilationStatus());

    resource->m_Source = shader->querySource();

    resource->m_IsESSLDefault = getVersion().check(GLContextVersion::Type::ES);

    resource->m_IsShaderSourceEdited = shader->queryIsShaderSourceEdited();

    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryProgram(gl_t _name) {

    GLuint name = static_cast<GLuint>(_name);

    dglnet::resource::DGLResourceProgram* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceProgram);

    GLProgramObj* program = ns().m_Programs.getObject(name);
    if (!program) {
        throw std::runtime_error("Shader Program does not exist");
    }

    std::set<GLShaderObj*> attachedShaders = program->getAttachedShaders();
    for (std::set<GLShaderObj*>::iterator it = attachedShaders.begin();
        it != attachedShaders.end(); ++it) {
        resource->m_AttachedShaders.push_back(
                std::pair<gl_t, gl_t>((*it)->getName(), (*it)->getTarget()));
    }

    GLint linkStatus, infoLogLength;
    DIRECT_CALL_CHK(glGetProgramiv)(name, GL_LINK_STATUS, &linkStatus);
    DIRECT_CALL_CHK(glGetProgramiv)(name, GL_INFO_LOG_LENGTH, &infoLogLength);

    queryCheckError();

    std::string infoLog;
    if (infoLogLength) {
        infoLog.resize(static_cast<size_t>(infoLogLength));
        GLint realInfoLogLength;
        DIRECT_CALL_CHK(glGetProgramInfoLog)(
                program->getName(), static_cast<GLsizei>(infoLog.size()),
                &realInfoLogLength, &infoLog[0]);
        if (realInfoLogLength < infoLogLength) {
            // highly unlikely, only on buggy drivers
            infoLog.resize(static_cast<size_t>(realInfoLogLength));
        }
    }

    resource->mLinkStatus = std::pair<std::string, gl_t>(infoLog, linkStatus);

    resource->m_EmbeddedSSOSource = program->getEmbeddedSSOSource();

    resource->m_EmbeddedSSOSourceIsESSL = getVersion().check(GLContextVersion::Type::ES);


    if (resource->mLinkStatus.second) {

        // if the program is linked, acquire it's uniform values

        GLint activeUniforms, activeUniformsMaxNameLength;
        DIRECT_CALL_CHK(glGetProgramiv)(name, GL_ACTIVE_UNIFORMS,
                                        &activeUniforms);
        DIRECT_CALL_CHK(glGetProgramiv)(name, GL_ACTIVE_UNIFORM_MAX_LENGTH,
                                        &activeUniformsMaxNameLength);
        std::vector<GLchar> nameBuffer(activeUniformsMaxNameLength);
        for (int idx = 0; idx < activeUniforms; idx++) {
            dglnet::resource::DGLResourceProgram::Uniform uniform;
            uniform.m_supportedType = false;

            GLsizei length;
            GLint size;
            GLenum type;
            DIRECT_CALL_CHK(glGetActiveUniform)(
                    name, idx, activeUniformsMaxNameLength, &length, &size,
                    &type, &nameBuffer[0]);

            nameBuffer[static_cast<size_t>(length)] = 0;
            uniform.m_name = &nameBuffer[0];
            uniform.m_type = type;

            uniform.m_location = DIRECT_CALL_CHK(glGetUniformLocation)(
                    name, uniform.m_name.c_str());
            if (uniform.m_location >= 0) {
                uniform.m_supportedType = true;
                GLenum baseType = 0;
                size_t typeSize = 0;
                switch (type) {
                    case GL_FLOAT:
                    case GL_FLOAT_VEC2:
                    case GL_FLOAT_VEC3:
                    case GL_FLOAT_VEC4:
                    case GL_FLOAT_MAT2:
                    case GL_FLOAT_MAT3:
                    case GL_FLOAT_MAT4:
                    case GL_FLOAT_MAT2x3:
                    case GL_FLOAT_MAT2x4:
                    case GL_FLOAT_MAT3x2:
                    case GL_FLOAT_MAT3x4:
                    case GL_FLOAT_MAT4x2:
                    case GL_FLOAT_MAT4x3:
                        baseType = GL_FLOAT;
                        break;
                    case GL_DOUBLE:
                    case GL_DOUBLE_VEC2:
                    case GL_DOUBLE_VEC3:
                    case GL_DOUBLE_VEC4:
                    case GL_DOUBLE_MAT2:
                    case GL_DOUBLE_MAT3:
                    case GL_DOUBLE_MAT4:
                    case GL_DOUBLE_MAT2x3:
                    case GL_DOUBLE_MAT2x4:
                    case GL_DOUBLE_MAT3x2:
                    case GL_DOUBLE_MAT3x4:
                    case GL_DOUBLE_MAT4x2:
                    case GL_DOUBLE_MAT4x3:
                        baseType = GL_DOUBLE;
                        break;
                    case GL_INT:
                    case GL_INT_VEC2:
                    case GL_INT_VEC3:
                    case GL_INT_VEC4:
                        /*samplers hold also one INT */
                    case GL_SAMPLER_BUFFER:
                    case GL_SAMPLER_1D:
                    case GL_SAMPLER_2D:
                    case GL_SAMPLER_2D_RECT:
                    case GL_SAMPLER_3D:
                    case GL_SAMPLER_CUBE:
                    case GL_SAMPLER_1D_ARRAY:
                    case GL_SAMPLER_2D_ARRAY:
                    case GL_SAMPLER_CUBE_MAP_ARRAY:
                    case GL_SAMPLER_2D_MULTISAMPLE:
                    case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
                    case GL_SAMPLER_1D_SHADOW:
                    case GL_SAMPLER_2D_SHADOW:
                    case GL_SAMPLER_2D_RECT_SHADOW:
                    case GL_SAMPLER_CUBE_SHADOW:
                    case GL_SAMPLER_1D_ARRAY_SHADOW:
                    case GL_SAMPLER_2D_ARRAY_SHADOW:
                    case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW:
                    case GL_IMAGE_1D:
                    case GL_IMAGE_2D:
                    case GL_IMAGE_3D:
                    case GL_IMAGE_2D_RECT:
                    case GL_IMAGE_CUBE:
                    case GL_IMAGE_BUFFER:
                    case GL_IMAGE_1D_ARRAY:
                    case GL_IMAGE_2D_ARRAY:
                    case GL_IMAGE_CUBE_MAP_ARRAY:
                    case GL_IMAGE_2D_MULTISAMPLE:
                    case GL_IMAGE_2D_MULTISAMPLE_ARRAY:
                    case GL_INT_IMAGE_1D:
                    case GL_INT_IMAGE_2D:
                    case GL_INT_IMAGE_3D:
                    case GL_INT_IMAGE_2D_RECT:
                    case GL_INT_IMAGE_CUBE:
                    case GL_INT_IMAGE_BUFFER:
                    case GL_INT_IMAGE_1D_ARRAY:
                    case GL_INT_IMAGE_2D_ARRAY:
                    case GL_INT_IMAGE_CUBE_MAP_ARRAY:
                    case GL_INT_IMAGE_2D_MULTISAMPLE:
                    case GL_INT_IMAGE_2D_MULTISAMPLE_ARRAY:
                    case GL_UNSIGNED_INT_IMAGE_1D:
                    case GL_UNSIGNED_INT_IMAGE_2D:
                    case GL_UNSIGNED_INT_IMAGE_3D:
                    case GL_UNSIGNED_INT_IMAGE_2D_RECT:
                    case GL_UNSIGNED_INT_IMAGE_CUBE:
                    case GL_UNSIGNED_INT_IMAGE_BUFFER:
                    case GL_UNSIGNED_INT_IMAGE_1D_ARRAY:
                    case GL_UNSIGNED_INT_IMAGE_2D_ARRAY:
                    case GL_UNSIGNED_INT_IMAGE_CUBE_MAP_ARRAY:
                    case GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE:
                    case GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY:
                        baseType = GL_INT;
                        break;
                    case GL_UNSIGNED_INT:
                    case GL_UNSIGNED_INT_VEC2:
                    case GL_UNSIGNED_INT_VEC3:
                    case GL_UNSIGNED_INT_VEC4:
                        baseType = GL_UNSIGNED_INT;
                        break;
                    case GL_BOOL:
                    case GL_BOOL_VEC2:
                    case GL_BOOL_VEC3:
                    case GL_BOOL_VEC4:
                        baseType = GL_BOOL;
                        break;
                    default:
                        uniform.m_supportedType = false;
                }

                if (uniform.m_supportedType) {
                    switch (type) {
                        case GL_FLOAT:
                        case GL_DOUBLE:
                        case GL_INT:
                        case GL_UNSIGNED_INT:
                        case GL_BOOL:
                            /*samplers also hold 1 element */
                        case GL_SAMPLER_BUFFER:
                        case GL_SAMPLER_1D:
                        case GL_SAMPLER_2D:
                        case GL_SAMPLER_2D_RECT:
                        case GL_SAMPLER_3D:
                        case GL_SAMPLER_CUBE:
                        case GL_SAMPLER_1D_ARRAY:
                        case GL_SAMPLER_2D_ARRAY:
                        case GL_SAMPLER_CUBE_MAP_ARRAY:
                        case GL_SAMPLER_2D_MULTISAMPLE:
                        case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
                        case GL_SAMPLER_1D_SHADOW:
                        case GL_SAMPLER_2D_SHADOW:
                        case GL_SAMPLER_2D_RECT_SHADOW:
                        case GL_SAMPLER_CUBE_SHADOW:
                        case GL_SAMPLER_1D_ARRAY_SHADOW:
                        case GL_SAMPLER_2D_ARRAY_SHADOW:
                        case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW:
                        case GL_IMAGE_1D:
                        case GL_IMAGE_2D:
                        case GL_IMAGE_3D:
                        case GL_IMAGE_2D_RECT:
                        case GL_IMAGE_CUBE:
                        case GL_IMAGE_BUFFER:
                        case GL_IMAGE_1D_ARRAY:
                        case GL_IMAGE_2D_ARRAY:
                        case GL_IMAGE_CUBE_MAP_ARRAY:
                        case GL_IMAGE_2D_MULTISAMPLE:
                        case GL_IMAGE_2D_MULTISAMPLE_ARRAY:
                        case GL_INT_IMAGE_1D:
                        case GL_INT_IMAGE_2D:
                        case GL_INT_IMAGE_3D:
                        case GL_INT_IMAGE_2D_RECT:
                        case GL_INT_IMAGE_CUBE:
                        case GL_INT_IMAGE_BUFFER:
                        case GL_INT_IMAGE_1D_ARRAY:
                        case GL_INT_IMAGE_2D_ARRAY:
                        case GL_INT_IMAGE_CUBE_MAP_ARRAY:
                        case GL_INT_IMAGE_2D_MULTISAMPLE:
                        case GL_INT_IMAGE_2D_MULTISAMPLE_ARRAY:
                        case GL_UNSIGNED_INT_IMAGE_1D:
                        case GL_UNSIGNED_INT_IMAGE_2D:
                        case GL_UNSIGNED_INT_IMAGE_3D:
                        case GL_UNSIGNED_INT_IMAGE_2D_RECT:
                        case GL_UNSIGNED_INT_IMAGE_CUBE:
                        case GL_UNSIGNED_INT_IMAGE_BUFFER:
                        case GL_UNSIGNED_INT_IMAGE_1D_ARRAY:
                        case GL_UNSIGNED_INT_IMAGE_2D_ARRAY:
                        case GL_UNSIGNED_INT_IMAGE_CUBE_MAP_ARRAY:
                        case GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE:
                        case GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY:
                            typeSize = 1;
                            uniform.m_rowSize = 1;
                            break;
                        case GL_FLOAT_VEC2:
                        case GL_DOUBLE_VEC2:
                        case GL_INT_VEC2:
                        case GL_UNSIGNED_INT_VEC2:
                        case GL_BOOL_VEC2:
                            typeSize = 2;
                            uniform.m_rowSize = 2;
                            break;
                        case GL_FLOAT_VEC3:
                        case GL_DOUBLE_VEC3:
                        case GL_INT_VEC3:
                        case GL_UNSIGNED_INT_VEC3:
                        case GL_BOOL_VEC3:
                            typeSize = 3;
                            uniform.m_rowSize = 3;
                            break;
                        case GL_FLOAT_VEC4:
                        case GL_DOUBLE_VEC4:
                        case GL_INT_VEC4:
                        case GL_UNSIGNED_INT_VEC4:
                        case GL_BOOL_VEC4:
                            typeSize = 4;
                            uniform.m_rowSize = 4;
                            break;
                        case GL_FLOAT_MAT2:
                        case GL_DOUBLE_MAT2:
                            typeSize = 4;
                            uniform.m_rowSize = 2;
                            break;
                        case GL_FLOAT_MAT3:
                        case GL_DOUBLE_MAT3:
                            typeSize = 9;
                            uniform.m_rowSize = 3;
                            break;
                        case GL_FLOAT_MAT4:
                        case GL_DOUBLE_MAT4:
                            typeSize = 16;
                            uniform.m_rowSize = 4;
                            break;
                        case GL_FLOAT_MAT2x3:
                        case GL_DOUBLE_MAT2x3:
                            typeSize = 6;
                            uniform.m_rowSize = 3;
                            break;
                        case GL_FLOAT_MAT2x4:
                        case GL_DOUBLE_MAT2x4:
                            typeSize = 8;
                            uniform.m_rowSize = 4;
                            break;
                        case GL_FLOAT_MAT3x2:
                        case GL_DOUBLE_MAT3x2:
                            typeSize = 6;
                            uniform.m_rowSize = 2;
                            break;
                        case GL_FLOAT_MAT3x4:
                        case GL_DOUBLE_MAT3x4:
                            typeSize = 12;
                            uniform.m_rowSize = 4;
                            break;
                        case GL_FLOAT_MAT4x2:
                        case GL_DOUBLE_MAT4x2:
                            typeSize = 8;
                            uniform.m_rowSize = 2;
                            break;
                        case GL_FLOAT_MAT4x3:
                        case GL_DOUBLE_MAT4x3:
                            typeSize = 12;
                            uniform.m_rowSize = 3;
                            break;
                        default:
                            DGL_ASSERT(0);
                    }

                    // size is 1 for scalars and > 1 for arrays of uniform
                    // scalars (see glGetActiveUniform)

                    // typeSize is size of uniform type in terms of baseType
                    // elements

                    uniform.m_value.resize(static_cast<size_t>(size) * typeSize);

                    if (baseType == GL_FLOAT) {
                        std::vector<GLfloat> value(uniform.m_value.size());
                        for (size_t loc = 0; loc < static_cast<size_t>(size); loc++) {
                            DIRECT_CALL_CHK(glGetUniformfv)(
                                    name, uniform.m_location + static_cast<GLint>(loc),
                                    &value[loc * typeSize]);
                        }
                        std::copy(value.begin(), value.end(),
                                  uniform.m_value.begin());
                    } else if (baseType == GL_DOUBLE) {
                        std::vector<GLdouble> value(uniform.m_value.size());
                        for (size_t loc = 0; loc < static_cast<size_t>(size); loc++) {
                            DIRECT_CALL_CHK(glGetUniformdv)(
                                    name, uniform.m_location + static_cast<GLint>(loc),
                                    &value[loc * typeSize]);
                        }
                        std::copy(value.begin(), value.end(),
                                  uniform.m_value.begin());
                    } else if (baseType == GL_INT) {
                        std::vector<GLint> value(uniform.m_value.size());
                        for (size_t loc = 0; loc < static_cast<size_t>(size); loc++) {
                            DIRECT_CALL_CHK(glGetUniformiv)(
                                    name, uniform.m_location + static_cast<GLint>(loc),
                                    &value[loc * typeSize]);
                        }
                        std::copy(value.begin(), value.end(),
                                  uniform.m_value.begin());
                    } else if (baseType == GL_UNSIGNED_INT ||
                               baseType == GL_BOOL) {
                        std::vector<GLuint> value(uniform.m_value.size());
                        for (size_t loc = 0; loc < static_cast<size_t>(size); loc++) {
                            DIRECT_CALL_CHK(glGetUniformuiv)(
                                    name, uniform.m_location + static_cast<GLint>(loc),
                                    &value[loc * typeSize]);
                        }
                        std::copy(value.begin(), value.end(),
                                  uniform.m_value.begin());
                    } else {
                        DGL_ASSERT(0);
                    }
                }
            }
            resource->m_Uniforms.push_back(uniform);
        }
    }
    return ret;
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryGPU() {

    dglnet::resource::DGLResourceGPU* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceGPU);

    // Forgot about:
    // GL_EXTENSIONS

    const char* value;

    if ((value = (const char*)DIRECT_CALL_CHK(glGetString)(GL_RENDERER)) !=
        NULL) {
        resource->m_Renderer = value;
    }
    if ((value = (const char*)DIRECT_CALL_CHK(glGetString)(GL_VENDOR)) !=
        NULL) {
        resource->m_Vendor = value;
    }
    if ((value = (const char*)DIRECT_CALL_CHK(glGetString)(GL_VERSION)) !=
        NULL) {
        resource->m_Version = value;
    }

    if (hasCapability(ContextCap::GLSLShaders)) {
        if ((value = (const char*)DIRECT_CALL_CHK(glGetString)(
            GL_SHADING_LANGUAGE_VERSION)) != NULL) {
                resource->m_GLSL = value;
        }
    } else {
        resource->m_GLSL = "<unavaliable>";
    }

    

    if ((resource->m_hasNVXGPUMemoryInfo = m_HasNVXMemoryInfo) != false) {
        GLint val;

#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#define GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX 0x904A
#define GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX 0x904B

        DIRECT_CALL_CHK(glGetIntegerv)(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX,
                                       &val);
        resource->m_nvidiaMemory.memInfoDedidactedVidMem = val;
        DIRECT_CALL_CHK(glGetIntegerv)(
                GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &val);
        resource->m_nvidiaMemory.memInfoTotalAvailMem = val;
        DIRECT_CALL_CHK(glGetIntegerv)(
                GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &val);
        resource->m_nvidiaMemory.memInfoCurrentAvailVidMem = val;
        DIRECT_CALL_CHK(glGetIntegerv)(GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX,
                                       &val);
        resource->m_nvidiaMemory.memInfoEvictionCount = val;
        DIRECT_CALL_CHK(glGetIntegerv)(GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX,
                                       &val);
        resource->m_nvidiaMemory.memInfoEvictedMem = val;
    }

    return ret;
}

void GLContext::getStateIntegerv(
        const char* name, GLenum value, size_t length, 
        dglnet::resource::utils::StateItem* ret) {
    ret->m_Name = name;
    std::vector<GLint> val(length, 0);

    DIRECT_CALL_CHK(glGetIntegerv)(value, &val[0]);

    if (DIRECT_CALL_CHK(glGetError)() == GL_NO_ERROR) {
        ret->m_Values.resize(val.size());
        for (size_t i = 0; i < length; i++) {
            ret->m_Values[i] = val[i];
        }
    }
}

void GLContext::getStateInteger64v(
        const char* name, GLenum value, size_t length,
        dglnet::resource::utils::StateItem* ret) {
    ret->m_Name = name;
    std::vector<GLint64> val(length, 0);

    if (hasCapability(ContextCap::Has64BitGetters)) {
        DIRECT_CALL_CHK(glGetInteger64v)(value, &val[0]);
    } else {
        std::vector<GLint> valInt(length, 0);
        DIRECT_CALL_CHK(glGetIntegerv)(value, &valInt[0]);
        std::copy(valInt.begin(), valInt.end(), val.begin());
    }

    if (DIRECT_CALL_CHK(glGetError)() == GL_NO_ERROR) {
        ret->m_Values.resize(val.size());
        for (size_t i = 0; i < length; i++) {
            ret->m_Values[i] = val[i];
        }
    }
}

void GLContext::getStateFloatv(
        const char* name, GLenum value, size_t length,
        dglnet::resource::utils::StateItem* ret) {
    ret->m_Name = name;
    std::vector<GLfloat> val(length, 0);

    DIRECT_CALL_CHK(glGetFloatv)(value, &val[0]);

    if (DIRECT_CALL_CHK(glGetError)() == GL_NO_ERROR) {
        ret->m_Values.resize(val.size());
        for (size_t i = 0; i < length; i++) {
            ret->m_Values[i] = val[i];
        }
    }
}

void GLContext::getStateDoublev(
        const char* name, GLenum value, size_t length,
        dglnet::resource::utils::StateItem* ret) {
    ret->m_Name = name;
    std::vector<GLdouble> val(length, 0);

    DIRECT_CALL_CHK(glGetDoublev)(value, &val[0]);

    if (DIRECT_CALL_CHK(glGetError)() == GL_NO_ERROR) {
        ret->m_Values.resize(val.size());
        for (size_t i = 0; i < length; i++) {
            ret->m_Values[i] = val[i];
        }
    }
}

void GLContext::getStateBooleanv(
        const char* name, GLenum value, size_t length,
        dglnet::resource::utils::StateItem* ret) {
    ret->m_Name = name;
    std::vector<GLboolean> val(length, 0);

    DIRECT_CALL_CHK(glGetBooleanv)(value, &val[0]);

    if (DIRECT_CALL_CHK(glGetError)() == GL_NO_ERROR) {
        ret->m_Values.resize(val.size());
        for (size_t i = 0; i < length; i++) {
            ret->m_Values[i] = val[i];
        }
    }
}

void GLContext::getStateIsEnabled(
        const char* name, GLenum value, size_t,
        dglnet::resource::utils::StateItem* ret) {
    ret->m_Name = name;
    GLboolean val = DIRECT_CALL_CHK(glIsEnabled)(value);
    if (DIRECT_CALL_CHK(glGetError)() == GL_NO_ERROR) {
        ret->m_Values.resize(1, val);
    }
}

std::shared_ptr<dglnet::DGLResource> GLContext::queryState(gl_t) {

    dglnet::resource::DGLResourceState* resource;
    std::shared_ptr<dglnet::DGLResource> ret(
            resource = new dglnet::resource::DGLResourceState);

#if DGL_HAVE_WA(ES_QUERY_STATE)
    if (!getVersion().check(GLContextVersion::Type::DT))
        return ret;    // not really supported on non-DT
#endif


    for (int store = 0; store <=1; store++) {
        
        //this points to location in returned resource.
        size_t loc = 0;

#define STATE_BASE(SETTER, NAME, VALUE, LENGTH)                         \
        if (store) {                                                    \
            SETTER(NAME, VALUE, LENGTH, &resource->m_Items[loc]);       \
        }                                                               \
        loc++;                                                          \


#define STATE_INTEGERV(NAME, LENGTH)     STATE_BASE(getStateIntegerv,   #NAME, NAME, LENGTH)
#define STATE_INTEGER64V(NAME, LENGTH)   STATE_BASE(getStateInteger64v, #NAME, NAME, LENGTH)
#define STATE_INTEGERENUMV(NAME, LENGTH) STATE_BASE(getStateIntegerv,   #NAME, NAME, LENGTH)
#define STATE_FLOATV(NAME, LENGTH)       STATE_BASE(getStateFloatv,     #NAME, NAME, LENGTH)
#define STATE_DOUBLEV(NAME, LENGTH)      STATE_BASE(getStateDoublev,    #NAME, NAME, LENGTH)
#define STATE_BOOLEANV(NAME, LENGTH)     STATE_BASE(getStateBooleanv,   #NAME, NAME, LENGTH)
#define STATE_ISENABLED(NAME)            STATE_BASE(getStateIsEnabled,  #NAME, NAME, 1)


    // Current Values and Associated Data                //Compat!
    // GL_CURRENT COLOR                                  //Compat!
    // GL_CURRENT_SECONDARY COLOR                        //Compat!
    // GL_CURRENT_INDEX                                  //Compat!
    // GL_CURRENT_TEXTURE COORDS                         //Compat!
    // GL_CURRENT_NORMAL                                 //Compat!
    // GL_CURRENT_FOG COORD                              //Compat!
    // GL_CURRENT_RASTER POSITION                        //Compat!
    // GL_CURRENT_RASTER DISTANCE                        //Compat!
    // GL_CURRENT_RASTER COLOR                           //Compat!
    // GL_CURRENT_RASTER SECONDARY COLOR                 //Compat!
    // GL_CURRENT_RASTER INDEX                           //Compat!
    // GL_CURRENT_RASTER TEXTURE COORDS                  //Compat!
    // GL_CURRENT_RASTER POSITION VALID                  //Compat!
    // GL_EDGE_FLAG                                      //Compat!
    STATE_INTEGERV(GL_PATCH_VERTICES, 1);
    STATE_FLOATV(GL_PATCH_DEFAULT_OUTER_LEVEL, 4);
    STATE_FLOATV(GL_PATCH_DEFAULT_INNER_LEVEL, 2);

    // Legacy Vertex Array State                         //COmpat!
    // GL_VERTEX_ARRAY                                     //COmpat!
    // GL_VERTEX_ARRAY_SIZE                                //COmpat!
    // GL_VERTEX_ARRAY_TYPE                                //COmpat!
    // GL_VERTEX_ARRAY_STRIDE                              //COmpat!
    // GL_VERTEX_ARRAY_POINTER                             //COmpat!
    // GL_NORMAL_ARRAY                                     //COmpat!
    // GL_NORMAL_ARRAY_TYPE                                //COmpat!
    // GL_NORMAL_ARRAY_STRIDE                              //COmpat!
    // GL_NORMAL_ARRAY_POINTER                             //COmpat!
    // GL_FOG_COORD_ARRAY                                  //COmpat!
    // GL_FOG_COORD_ARRAY_TYPE                             //COmpat!
    // GL_FOG_COORD_ARRAY_STRIDE                           //COmpat!
    // GL_FOG_COORD_ARRAY_POINTER                          //COmpat!
    // GL_COLOR_ARRAY                                      //COmpat!
    // GL_COLOR_ARRAY_SIZE                                 //COmpat!
    // GL_COLOR_ARRAY_TYPE                                 //COmpat!
    // GL_COLOR_ARRAY_STRIDE                               //COmpat!
    // GL_COLOR_ARRAY_POINTER                              //COmpat!
    // GL_SECONDARY_COLOR_ARRAY                            //COmpat!
    // GL_SECONDARY_COLOR_ARRAY_SIZE                       //COmpat!
    // GL_SECONDARY_COLOR_ARRAY_TYPE                       //COmpat!
    // GL_SECONDARY_COLOR_ARRAY_STRIDE                     //COmpat!
    // GL_SECONDARY_COLOR_ARRAY_POINTER                    //COmpat!
    // GL_INDEX_ARRAY                                      //COmpat!
    // GL_INDEX_ARRAY_TYPE                                 //COmpat!
    // GL_INDEX_ARRAY_STRIDE                               //COmpat!
    // GL_INDEX_ARRAY_POINTER                              //COmpat!
    // GL_TEXTURE_COORD_ARRAY                              //COmpat!
    // GL_TEXTURE_COORD_ARRAY_SIZE                         //COmpat!
    // GL_TEXTURE_COORD_ARRAY_TYPE                         //COmpat!
    // GL_TEXTURE_COORD_ARRAY_STRIDE                       //COmpat!
    // GL_TEXTURE_COORD_ARRAY_POINTER                      //COmpat!

    // VAO State
    // VERTEX ATTRIB ARRAY ENABLED
    // VERTEX ATTRIB ARRAY SIZE
    // VERTEX ATTRIB ARRAY STRIDE
    // VERTEX ATTRIB ARRAY TYPE
    // VERTEX ATTRIB ARRAY NORMALIZED
    // VERTEX ATTRIB ARRAY INTEGER
    // VERTEX ATTRIB ARRAY LONG
    // VERTEX ATTRIB ARRAY DIVISOR
    // VERTEX ATTRIB ARRAY POINTER
    // EDGE FLAG ARRAY                                   //COmpat!
    // EDGE FLAG ARRAY STRIDE                            //COmpat!
    // EDGE FLAG ARRAY POINTER                           //COmpat!
    // LABEL
    // VERTEX ARRAY BUFFER BINDING                       //COmpat!
    // NORMAL ARRAY BUFFER BINDING                       //COmpat!
    // COLOR ARRAY BUFFER BINDING                        //COmpat!
    // INDEX ARRAY BUFFER BINDING                        //COmpat!
    // TEXTURE COORD ARRAY BUFFER BINDING                //COmpat!
    // EDGE FLAG ARRAY BUFFER BINDING                    //COmpat!
    // SECONDARY COLOR ARRAY BUFFER BINDING              //COmpat!
    // FOG COORD ARRAY BUFFER BINDING                    //COmpat!
    STATE_INTEGERV(GL_ELEMENT_ARRAY_BUFFER_BINDING, 1);
    // VERTEX ATTRIB ARRAY BUFFER BINDING
    // VERTEX ATTRIB BINDING
    // VERTEX ATTRIB RELATIVE OFFSET
    // VERTEX BINDING OFFSET
    // VERTEX BINDING STRIDE

    // Vertex Array Data
    // CLIENT ACTIVE TEXTURE
    STATE_INTEGERV(GL_ARRAY_BUFFER_BINDING, 1);
    STATE_INTEGERV(GL_DRAW_INDIRECT_BUFFER_BINDING, 1);
    STATE_INTEGERV(GL_VERTEX_ARRAY_BINDING, 1);
    STATE_ISENABLED(GL_PRIMITIVE_RESTART);
    STATE_INTEGERV(GL_PRIMITIVE_RESTART_INDEX, 1);

    // Buffer Object state
    // BUFFER SIZE
    // BUFFER USAGE
    // BUFFER ACCESS
    // BUFFER ACCESS FLAGS
    // BUFFER MAPPED
    // BUFFER MAP POINTER
    // BUFFER MAP OFFSET
    // BUFFER MAP LENGTH
    // LABEL

    // Transformation State                         //COmpat!
    // COLOR MATRIX                                 //COmpat!
    //(TRANSPOSE COLOR MATRIX)                     //COmpat!
    // MODELVIEW MATRIX                             //COmpat!
    //(TRANSPOSE MODELVIEW MATRIX)                 //COmpat!
    // PROJECTION MATRIX                            //COmpat!
    //(TRANSPOSE PROJECTION MATRIX)                //COmpat!
    // TEXTURE MATRIX                               //COmpat!
    //(TRANSPOSE TEXTURE MATRIX)                   //COmpat!
    STATE_INTEGERV(GL_VIEWPORT, 4);
    if (getVersion().check(GLContextVersion::Type::DT)) {
        STATE_DOUBLEV(GL_DEPTH_RANGE, 2);
    } else {
        STATE_FLOATV(GL_DEPTH_RANGE, 2);
    }
    // COLOR MATRIX STACK DEPTH                    //Compat!
    // MODELVIEW STACK DEPTH                       //Compat!
    // PROJECTION STACK DEPTH                      //Compat!
    // TEXTURE STACK DEPTH                         //Compat!
    // MATRIX MODE                                 //Compat!
    // NORMALIZE                                   //Compat!
    // RESCALE NORMAL                              //Compat!
    // CLIP PLANEi                                 //Compat!
    STATE_ISENABLED(GL_CLIP_DISTANCE0);
    STATE_ISENABLED(GL_CLIP_DISTANCE1);
    STATE_ISENABLED(GL_CLIP_DISTANCE2);
    STATE_ISENABLED(GL_CLIP_DISTANCE3);
    STATE_ISENABLED(GL_CLIP_DISTANCE4);
    STATE_ISENABLED(GL_CLIP_DISTANCE5);
    STATE_ISENABLED(GL_DEPTH_CLAMP);
    STATE_INTEGERV(GL_TRANSFORM_FEEDBACK_BINDING, 1);

    // Coloring
    // FOG COLOR                                  //COmpat
    // FOG INDEX                                  //COmpat
    // FOG DENSITY                                //COmpat
    // FOG START                                  //COmpat
    // FOG END                                    //COmpat
    // FOG MODE                                   //COmpat
    // FOG                                        //COmpat
    // FOG COORD SRC                              //COmpat
    // COLOR SUM                                  //COmpat
    // SHADE MODEL                                //COmpat
    // CLAMP VERTEX COLOR                         //COmpat
    // CLAMP FRAGMENT COLOR                       //COmpat
    STATE_INTEGERENUMV(GL_CLAMP_READ_COLOR, 1);
    STATE_INTEGERENUMV(GL_PROVOKING_VERTEX, 1);

    // Lighting
    // LIGHTING                                    //Compat
    // COLOR MATERIAL                              //Compat
    // COLOR MATERIAL PARAMETER                    //Compat
    // COLOR MATERIAL FACE                         //Compat
    // AMBIENT                                     //Compat
    // DIFFUSE                                     //Compat
    // SPECULAR                                    //Compat
    // EMISSION                                    //Compat
    // SHININESS                                   //Compat
    // LIGHT MODEL AMBIENT                         //Compat
    // LIGHT MODEL LOCAL VIEWER                    //Compat
    // LIGHT MODEL TWO SIDE                        //Compat
    // LIGHT MODEL COLOR CONTROL                   //Compat
    // AMBIENT                                     //COmpat
    // DIFFUSE                                     //COmpat
    // SPECULAR                                    //COmpat
    // POSITION                                    //COmpat
    // CONSTANT ATTENUATION                        //COmpat
    // LINEAR ATTENUATION                          //COmpat
    // QUADRATIC ATTENUATION                       //COmpat
    // SPOT DIRECTION                              //COmpat
    // SPOT EXPONENT                               //COmpat
    // SPOT CUTOFF                                 //COmpat
    // LIGHTi                                      //COmpat
    // COLOR INDEXES                               //COmpat

    // Rasterization
    STATE_ISENABLED(GL_RASTERIZER_DISCARD);
    STATE_FLOATV(GL_POINT_SIZE, 1);
    // POINT SMOOTH                                //Compat
    // POINT SPRITE                                //Compat
    // POINT SIZE MIN                              //Compat
    // POINT SIZE MAX                              //Compat
    STATE_FLOATV(GL_POINT_FADE_THRESHOLD_SIZE, 1);
    // POINT DISTANCE ATTENUATION                  //Compat
    STATE_INTEGERENUMV(GL_POINT_SPRITE_COORD_ORIGIN, 1);
    STATE_FLOATV(GL_LINE_WIDTH, 1);
    STATE_ISENABLED(GL_LINE_SMOOTH);
    // LINE STIPPLE PATTERN                      //COmpat
    // LINE STIPPLE REPEAT                       //COmpat
    // LINE STIPPLE                              //COmpat
    STATE_ISENABLED(GL_CULL_FACE);
    STATE_INTEGERENUMV(GL_CULL_FACE_MODE, 1);
    STATE_INTEGERENUMV(GL_FRONT_FACE, 1);
    STATE_ISENABLED(GL_POLYGON_SMOOTH);
    STATE_INTEGERENUMV(GL_POLYGON_MODE, 2);
    STATE_FLOATV(GL_POLYGON_OFFSET_FACTOR, 1);
    STATE_FLOATV(GL_POLYGON_OFFSET_UNITS, 1);
    STATE_ISENABLED(GL_POLYGON_OFFSET_POINT);
    STATE_ISENABLED(GL_POLYGON_OFFSET_LINE);
    STATE_ISENABLED(GL_POLYGON_OFFSET_FILL);
    // POLYGON STIPPLE                          //COmpat

    // Multisampling
    STATE_ISENABLED(GL_MULTISAMPLE);
    STATE_ISENABLED(GL_SAMPLE_ALPHA_TO_COVERAGE);
    STATE_ISENABLED(GL_SAMPLE_ALPHA_TO_ONE);
    STATE_ISENABLED(GL_SAMPLE_COVERAGE);
    STATE_FLOATV(GL_SAMPLE_COVERAGE_VALUE, 1);
    STATE_BOOLEANV(GL_SAMPLE_COVERAGE_INVERT, 1);
    STATE_ISENABLED(GL_SAMPLE_SHADING);
    STATE_FLOATV(GL_MIN_SAMPLE_SHADING_VALUE, 1);
    STATE_ISENABLED(GL_SAMPLE_MASK);
    // SAMPLE MASK VALUE

    // Texturing (per texture unit)
    // TEXTURE xD                                //COmpat
    // TEXTURE CUBE MAP                          //COmpat
    STATE_INTEGERV(GL_TEXTURE_BINDING_1D, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_2D, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_3D, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_1D_ARRAY, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_2D_ARRAY, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_CUBE_MAP_ARRAY, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_RECTANGLE, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_BUFFER, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_CUBE_MAP, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_2D_MULTISAMPLE, 1);
    STATE_INTEGERV(GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY, 1);
    STATE_INTEGERV(GL_SAMPLER_BINDING, 1);
    // TEXTURE xD
    // TEXTURE 1D ARRAY
    // TEXTURE 2D ARRAY
    // TEXTURE CUBE MAP ARRAY
    // TEXTURE RECTANGLE
    // TEXTURE CUBE MAP POSITIVE X
    // TEXTURE CUBE MAP NEGATIVE X
    // TEXTURE CUBE MAP POSITIVE Y
    // TEXTURE CUBE MAP NEGATIVE Y
    // TEXTURE CUBE MAP POSITIVE Z
    // TEXTURE CUBE MAP NEGATIVE Z

    // Texturing (per texture object)
    // TEXTURE SWIZZLE R
    // TEXTURE SWIZZLE G
    // TEXTURE SWIZZLE B
    // TEXTURE SWIZZLE A
    // TEXTURE BORDER COLOR
    // TEXTURE MIN FILTER
    // TEXTURE MAG FILTER
    // TEXTURE WRAP S
    // TEXTURE WRAP T
    // TEXTURE WRAP R
    // TEXTURE PRIORITY                  //Compat
    // TEXTURE RESIDENT                  //Compat
    // TEXTURE MIN LOD
    // TEXTURE MAX LOD
    // TEXTURE BASE LEVEL
    // TEXTURE MAX LEVEL
    // TEXTURE LOD BIAS
    // DEPTH TEXTURE MODE   //Compat
    // DEPTH STENCIL TEXTURE
    // TEXTURE COMPARE MODE
    // TEXTURE COMPARE FUNC
    // GENERATE MIPMAP             //COmpat
    // IMAGE FORMAT COMPATIBILITY
    // TEXTURE IMMUTABLE
    // TEXTURE IMMUTABLE
    // TEXTURE VIEW MIN LEVEL
    // TEXTURE VIEW NUM LEVELS
    // TEXTURE VIEW MIN LAYER
    // TEXTURE VIEW NUM LAYERS
    // LABEL

    // Texturing (per texture image)
    // TEXTURE WIDTH
    // TEXTURE HEIGHT
    // TEXTURE DEPTH
    // TEXTURE BORDER       //COmpat
    // TEXTURE SAMPLES
    // TEXTURE FIXED SAMPLE
    // TEXTURE INTERNAL FORMAT
    // TEXTURE COMPONENTS)
    // TEXTURE x SIZE
    // TEXTURE SHARED SIZE
    // TEXTURE x TYPE
    // TEXTURE COMPRESSED
    // TEXTURE COMPRESSED IMAGE
    // TEXTURE BUFFER DATA STORE BINDING
    // TEXTURE BUFFER OFFSET
    // TEXTURE BUFFER SIZE

    // Texturing (per sampler objects)
    // TEXTURE BORDER COLOR
    // TEXTURE COMPARE FUNC
    // TEXTURE COMPARE MODE
    // TEXTURE LOD BIAS
    // TEXTURE MAX LOD
    // TEXTURE MAG FILTER
    // TEXTURE MIN FILTER
    // TEXTURE MIN LOD
    // TEXTURE WRAP S
    // TEXTURE WRAP T
    // TEXTURE WRAP R
    // LABEL

    // Texture Environment and Generation
    STATE_INTEGERV(GL_ACTIVE_TEXTURE, 1);
    // COORD REPLACE                      //COmpat!
    // TEXTURE ENV MODE                   //COmpat!
    // TEXTURE ENV COLOR                  //COmpat!
    // TEXTURE LOD BIAS                   //COmpat!
    // TEXTURE GEN x                      //COmpat!
    // EYE PLANE                          //COmpat!
    // OBJECT PLANE                       //COmpat!
    // TEXTURE GEN MODE                   //COmpat!
    // COMBINE RGB                        //COmpat!
    // COMBINE ALPHA                      //COmpat!
    // SRC0 RGB                           //COmpat!
    // SRC1 RGB                           //COmpat!
    // SRC2 RGB                           //COmpat!
    // SRC0 ALPHA                         //COmpat!
    // SRC1 ALPHA                         //COmpat!
    // SRC2 ALPHA                         //COmpat!
    // OPERAND0 RGB                       //COmpat!
    // OPERAND1 RGB                       //COmpat!
    // OPERAND2 RGB                       //COmpat!
    // OPERAND0 ALPHA                     //COmpat!
    // OPERAND1 ALPHA                     //COmpat!
    // OPERAND2 ALPHA                     //COmpat!
    // RGB SCALE                          //COmpat!
    // ALPHA SCALE                        //COmpat!

    // Pixel Operations
    STATE_ISENABLED(GL_SCISSOR_TEST);
    STATE_FLOATV(GL_SCISSOR_BOX, 4);
    // ALPHA TEST                        //Compat!
    // ALPHA TEST FUNC                   //Compat!
    // ALPHA TEST REF                    //Compat!
    STATE_ISENABLED(GL_STENCIL_TEST);
    STATE_INTEGERENUMV(GL_STENCIL_FUNC, 1);
    STATE_INTEGERV(GL_STENCIL_VALUE_MASK, 1);
    STATE_INTEGERV(GL_STENCIL_REF, 1);
    STATE_INTEGERENUMV(GL_STENCIL_FAIL, 1);
    STATE_INTEGERENUMV(GL_STENCIL_PASS_DEPTH_FAIL, 1);
    STATE_INTEGERENUMV(GL_STENCIL_PASS_DEPTH_PASS, 1);
    STATE_INTEGERENUMV(GL_STENCIL_BACK_FUNC, 1);
    STATE_INTEGERV(GL_STENCIL_BACK_VALUE_MASK, 1);
    STATE_INTEGERV(GL_STENCIL_BACK_REF, 1);
    STATE_INTEGERENUMV(GL_STENCIL_BACK_FAIL, 1);
    STATE_INTEGERENUMV(GL_STENCIL_BACK_PASS_DEPTH_FAIL, 1);
    STATE_INTEGERENUMV(GL_STENCIL_BACK_PASS_DEPTH_PASS, 1);
    STATE_ISENABLED(GL_DEPTH_TEST);
    STATE_INTEGERENUMV(GL_DEPTH_FUNC, 1);
    STATE_ISENABLED(GL_BLEND);
    STATE_INTEGERV(GL_BLEND_SRC_RGB, 1);
    STATE_INTEGERV(GL_BLEND_SRC_ALPHA, 1);
    STATE_INTEGERV(GL_BLEND_DST_RGB, 1);
    STATE_INTEGERV(GL_BLEND_DST_ALPHA, 1);
    STATE_INTEGERV(GL_BLEND_EQUATION_RGB, 1);
    STATE_INTEGERV(GL_BLEND_EQUATION_ALPHA, 1);
    STATE_FLOATV(GL_BLEND_COLOR, 4);
    STATE_ISENABLED(GL_FRAMEBUFFER_SRGB);
    STATE_ISENABLED(GL_DITHER);
    // INDEX LOGIC OP   //Compat!
    STATE_ISENABLED(GL_COLOR_LOGIC_OP);
    STATE_INTEGERENUMV(GL_LOGIC_OP_MODE, 1);

    // Framebuffer Control
    // INDEX WRITEMASK
    STATE_BOOLEANV(GL_DEPTH_WRITEMASK, 1);
    STATE_INTEGERV(GL_STENCIL_WRITEMASK, 1);
    STATE_INTEGERV(GL_STENCIL_BACK_WRITEMASK, 1);
    STATE_FLOATV(GL_COLOR_CLEAR_VALUE, 4);
    // INDEX CLEAR VALUE
    STATE_FLOATV(GL_DEPTH_CLEAR_VALUE, 1);
    STATE_INTEGERV(GL_STENCIL_CLEAR_VALUE, 1);
    // ACCUM CLEAR VALUE

    // Framebuffer (state per target binding point)
    STATE_INTEGERV(GL_DRAW_FRAMEBUFFER_BINDING, 1);
    STATE_INTEGERV(GL_READ_FRAMEBUFFER_BINDING, 1);

    // Framebuffer (state per framebuffer object)
    STATE_INTEGERV(GL_DRAW_BUFFER0, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER1, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER2, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER3, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER4, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER5, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER6, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER7, 1);
    STATE_INTEGERV(GL_DRAW_BUFFER8, 1);
    STATE_INTEGERENUMV(GL_READ_BUFFER, 1);
    // LABEL

    // Framebuffer (state per attachment point)
    // FRAMEBUFFER ATTACHMENT OBJECT TYPE
    // FRAMEBUFFER ATTACHMENT OBJECT NAME
    // FRAMEBUFFER ATTACHMENT TEXTURE LEVEL
    // FRAMEBUFFER ATTACHMENT TEXTURE CUBE MAP FACE
    // FRAMEBUFFER ATTACHMENT TEXTURE LAYER
    // FRAMEBUFFER ATTACHMENT LAYERED
    // FRAMEBUFFER ATTACHMENT COLOR ENCODING
    // FRAMEBUFFER ATTACHMENT COMPONENT TYPE
    // FRAMEBUFFER ATTACHMENT x SIZE

    // 23.32. Renderbuffer (state per target and binding point)
    STATE_INTEGERV(GL_RENDERBUFFER_BINDING, 1);

    // Renderbuffer (state per renderbuffer object)
    // RENDERBUFFER WIDTH
    // RENDERBUFFER HEIGHT
    // RENDERBUFFER INTERNAL FORMAT
    // RENDERBUFFER RED SIZE
    // RENDERBUFFER GREEN SIZE
    // RENDERBUFFER BLUE SIZE
    // RENDERBUFFER ALPHA SIZE
    // RENDERBUFFER DEPTH SIZE
    // RENDERBUFFER STENCIL SIZE
    // RENDERBUFFER SAMPLES
    // LABEL

    // Pixels
    STATE_BOOLEANV(GL_UNPACK_SWAP_BYTES, 1);
    STATE_BOOLEANV(GL_UNPACK_LSB_FIRST, 1);
    STATE_INTEGERV(GL_UNPACK_IMAGE_HEIGHT, 1);
    STATE_INTEGERV(GL_UNPACK_SKIP_IMAGES, 1);
    STATE_INTEGERV(GL_UNPACK_ROW_LENGTH, 1);
    STATE_INTEGERV(GL_UNPACK_SKIP_ROWS, 1);
    STATE_INTEGERV(GL_UNPACK_SKIP_PIXELS, 1);
    STATE_INTEGERV(GL_UNPACK_ALIGNMENT, 1);
    STATE_INTEGERV(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, 1);
    STATE_INTEGERV(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, 1);
    STATE_INTEGERV(GL_UNPACK_COMPRESSED_BLOCK_DEPTH, 1);
    STATE_INTEGERV(GL_UNPACK_COMPRESSED_BLOCK_SIZE, 1);
    STATE_INTEGERV(GL_PIXEL_UNPACK_BUFFER_BINDING, 1);
    STATE_BOOLEANV(GL_PACK_SWAP_BYTES, 1);
    STATE_BOOLEANV(GL_PACK_LSB_FIRST, 1);
    STATE_INTEGERV(GL_PACK_IMAGE_HEIGHT, 1);
    STATE_INTEGERV(GL_PACK_SKIP_IMAGES, 1);
    STATE_INTEGERV(GL_PACK_ROW_LENGTH, 1);
    STATE_INTEGERV(GL_PACK_SKIP_ROWS, 1);
    STATE_INTEGERV(GL_PACK_SKIP_PIXELS, 1);
    STATE_INTEGERV(GL_PACK_ALIGNMENT, 1);
    STATE_INTEGERV(GL_PACK_COMPRESSED_BLOCK_WIDTH, 1);
    STATE_INTEGERV(GL_PACK_COMPRESSED_BLOCK_HEIGHT, 1);
    STATE_INTEGERV(GL_PACK_COMPRESSED_BLOCK_DEPTH, 1);
    STATE_INTEGERV(GL_PACK_COMPRESSED_BLOCK_SIZE, 1);
    STATE_INTEGERV(GL_PIXEL_PACK_BUFFER_BINDING, 1);
    // MAP COLOR                                        //Compat
    // MAP STENCIL                                      //Compat
    // INDEX SHIFT                                      //Compat
    // INDEX OFFSET                                     //Compat
    // x SCALE                                          //Compat
    // x BIAS                                           //Compat
    // COLOR TABLE                                      //Compat
    // POST CONVOLUTION COLOR TABLE                     //Compat
    // POST COLOR MATRIX COLOR TABLE                    //Compat
    // COLOR TABLE                                      //Compat
    // POST CONVOLUTION COLOR TABLE                     //Compat
    // POST COLOR MATRIX COLOR TABLE                    //Compat
    // COLOR TABLE FORMAT                               //Compat
    // COLOR TABLE WIDTH                                //Compat
    // COLOR TABLE x SIZE                               //Compat
    // COLOR TABLE SCzALE                                //Compat
    // COLOR TABLE BIAS                                 //Compat
    // CONVOLUTION 1D                                   //COmpat
    // CONVOLUTION 2D                                   //COmpat
    // SEPARABLE 2D                                     //COmpat
    // CONVOLUTION xD                                   //COmpat
    // SEPARABLE 2D                                     //COmpat
    // CONVOLUTION BORDER COLOR                         //COmpat
    // CONVOLUTION BORDER MODE                          //COmpat
    // CONVOLUTION FILTER SCALE                         //COmpat
    // CONVOLUTION FILTER BIAS                          //COmpat
    // CONVOLUTION FORMAT                               //COmpat
    // CONVOLUTION WIDTH                                //COmpat
    // CONVOLUTION HEIGHT                               //COmpat
    // POST CONVOLUTION                                 //COmpat
    // POST CONVOLUTION                                 //COmpat
    // POST COLOR MATRIX                                //COmpat
    // POST COLOR MATRIX                                //COmpat
    // HISTOGRAM                                        //COmpat
    // HISTOGRAM                                        //COmpat
    // HISTOGRAM WIDTH                                  //COmpat
    // HISTOGRAM FORMAT                                 //COmpat
    // HISTOGRAM x SIZE                                 //COmpat
    // HISTOGRAM SINK                                   //COmpat
    // MINMAX                                           //COmpat
    // MINMAX                                           //COmpat
    // MINMAX FORMAT                                    //COmpat
    // MINMAX SINK                                      //COmpat
    // ZOOM X                                           //COmpat
    // ZOOM Y                                           //COmpat
    // x                                                //COmpat
    // x                                                //COmpat
    // x SIZE                                           //COmpat

    // Evaluators                                       //COmpat
    // ORDER                                            //COmpat
    // ORDER                                            //COmpat
    // COEFF                                            //COmpat
    // COEFF                                            //COmpat
    // DOMAIN                                           //COmpat
    // DOMAIN                                           //COmpat
    // MAP1 x                                           //COmpat
    // MAP2 x                                           //COmpat
    // MAP1 GRID DOMAIN                                 //COmpat
    // MAP2 GRID DOMAIN                                 //COmpat
    // MAP1 GRID SEGMENTS                               //COmpat
    // MAP2 GRID SEGMENTS                               //COmpat
    // AUTO NORMAL                                      //COmpat

    // Shader object state
    // SHADER TYPE
    // DELETE STATUS
    // COMPILE STATUS
    //�
    // INFO LOG LENGTH
    //�
    // SHADER SOURCE LENGTH
    // LABEL

    // Program Pipeline Object State
    // ACTIVE PROGRAM
    // VERTEX SHADER
    // GEOMETRY SHADER
    // FRAGMENT SHADER
    // TESS CONTROL SHADER
    // TESS EVALUATION SHADER
    // VALIDATE STATUS
    //�
    // INFO LOG LENGTH
    // LABEL

    // Program Object State
    STATE_INTEGERV(GL_CURRENT_PROGRAM, 1);
    STATE_INTEGERV(GL_PROGRAM_PIPELINE_BINDING, 1);
    // PROGRAM SEPARABLE
    // DELETE STATUS
    // LINK STATUS
    // VALIDATE STATUS
    // ATTACHED SHADERS
    //�
    //�
    // INFO LOG LENGTH
    // PROGRAM BINARY LENGTH
    // PROGRAM BINARY RETRIEVABLE
    //-�
    // COMPUTE WORK GROUP SIZE
    // LABEL
    // ACTIVE UNIFORMS
    //�
    //�
    //�
    //�
    // ACTIVE UNIFORM MAX LENGTH
    //�
    // ACTIVE ATTRIBUTES
    //�
    //�
    //�
    //�
    // ACTIVE ATTRIBUTE MAX LENGTH
    // GEOMETRY VERTICES
    // GEOMETRY INPUT
    // GEOMETRY OUTPUT
    // GEOMETRY SHADER INVOCATIONS
    // TRANSFORM FEEDBACK
    // MODE
    // TRANSFORM FEEDBACK VARYINGS
    // TRANSFORM FEEDBACK VARYING
    // MAX LENGTH
    //�
    //�
    //�
    STATE_INTEGERV(GL_UNIFORM_BUFFER_BINDING, 1);
    // UNIFORM BUFFER BINDING
    // UNIFORM BUFFER START
    // UNIFORM BUFFER SIZE
    // ACTIVE UNIFORM BLOCKS
    // ACTIVE UNIFORM BLOCK MAX
    // NAME LENGTH
    // UNIFORM TYPE
    // UNIFORM SIZE
    // UNIFORM NAME LENGTH
    // UNIFORM BLOCK INDEX
    // UNIFORM OFFSET
    // UNIFORM ARRAY STRIDE
    // UNIFORM MATRIX STRIDE
    // UNIFORM IS ROW MAJOR
    // UNIFORM BLOCK BINDING
    // UNIFORM BLOCK DATA SIZE
    // UNIFORM BLOCK ACTIVE UNIFORMS
    // UNIFORM BLOCK ACTIVE UNIFORM
    // INDICES
    // UNIFORM BLOCK REFERENCED -
    // BY VERTEX SHADER
    // UNIFORM BLOCK REFERENCED -
    // BY TESS CONTROL SHADER
    // UNIFORM BLOCK REFERENCED -
    // BY TESS EVALUTION SHADER
    // UNIFORM BLOCK REFERENCED -
    // BY GEOMETRY SHADER
    // UNIFORM BLOCK REFERENCED -
    // BY FRAGMENT SHADER
    // UNIFORM BLOCK REFERENCED -
    // BY COMPUTE SHADER
    // TESS CONTROL OUTPUT VERTICES
    // TESS GEN MODE
    // TESS GEN SPACING
    // TESS GEN VERTEX ORDER
    // TESS GEN POINT MODE
    // ACTIVE SUBROUTINE UNIFORM -
    // LOCATIONS
    // ACTIVE SUBROUTINE UNIFORMS
    // ACTIVE SUBROUTINES
    // ACTIVE SUBROUTINE UNIFORM -
    // MAX LENGTH
    // ACTIVE SUBROUTINE MAX -
    // LENGTH
    // NUM COMPATIBLE SUBROUTINES
    // COMPATIBLE SUBROUTINES
    // UNIFORM SIZE
    // UNIFORM NAME LENGTH
    //�
    //�
    //�
    // ACTIVE ATOMIC COUNTER -
    // BUFFERS
    // ATOMIC COUNTER BUFFER BINDING
    // ATOMIC COUNTER BUFFER DATA
    // SIZE
    // ATOMIC COUNTER BUFFER ACTIVE
    // ATOMIC COUNTERS
    // ATOMIC COUNTER BUFFER ACTIVE
    // ATOMIC COUNTER INDICES
    // ATOMIC COUNTER BUFFER REFERENCED
    // BY VERTEX SHADER
    // ATOMIC COUNTER BUFFER REFERENCED
    // BY TESS CONTROL -
    // SHADER
    // ATOMIC COUNTER BUFFER REFERENCED
    // BY TESS EVALUTION -
    // SHADER
    // ATOMIC COUNTER BUFFER REFERENCED
    // BY GEOMETRY SHADER
    // ATOMIC COUNTER BUFFER REFERENCED
    // BY FRAGMENT SHADER
    // ATOMIC COUNTER BUFFER REFERENCED
    // BY COMPUTE SHADER
    // UNIFORM ATOMIC COUNTER -
    // BUFFER INDEX

    // Program Interface State
    // ACTIVE RESOURCES
    // MAX NAME LENGTH
    // MAX NUM ACTIVE VARIABLES
    // MAX NUM COMPATIBLE SUBROUTINES

    // Program Object Resource State
    // NAME LENGTH
    // TYPE
    // ARRAY SIZE
    // OFFSET
    // BLOCK INDEX
    // ARRAY STRIDE
    // MATRIX STRIDE
    // IS ROW MAJOR
    // ATOMIC COUNTER BUFFER INDEX
    // BUFFER BINDING
    // BUFFER DATA SIZE
    // NUM ACTIVE VARIABLES
    // ACTIVE VARIABLES
    // REFERENCED BY VERTEX SHADER
    // REFERENCED BY TESS CONTROL -
    // SHADER
    // REFERENCED BY TESS EVALUATION
    // SHADER
    // REFERENCED BY GEOMETRY -
    // SHADER
    // REFERENCED BY FRAGMENT -
    // SHADER
    // REFERENCED BY COMPUTE -
    // SHADER
    // TOP LEVEL ARRAY SIZE
    // TOP LEVEL ARRAY STRIDE
    // LOCATION
    // LOCATION INDEX
    // IS PER PATCH
    // NUM COMPATIBLE SUBROUTINES
    // COMPATIBLE SUBROUTINES

    // Vertex and Geometry Shader State
    // VERTEX PROGRAM TWO SIDE                    //Compat
    // CURRENT VERTEX ATTRIB
    STATE_ISENABLED(GL_PROGRAM_POINT_SIZE);

    // Query Object State
    // QUERY RESULT
    // QUERY RESULT AVAILABLE
    // LABEL

    // Image State (state per image unit)
    // IMAGE BINDING NAME
    // IMAGE BINDING LEVEL
    // IMAGE BINDING LAYERED
    // IMAGE BINDING LAYER
    // IMAGE BINDING ACCESS
    // IMAGE BINDING FORMAT

    // Transform Feedback State
    STATE_INTEGERV(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 1);
    // TRANSFORM FEEDBACK BUFFER BINDING
    // TRANSFORM FEEDBACK BUFFER START
    // TRANSFORM FEEDBACK BUFFER SIZE
    STATE_BOOLEANV(GL_TRANSFORM_FEEDBACK_PAUSED, 1);
    STATE_BOOLEANV(GL_TRANSFORM_FEEDBACK_ACTIVE, 1);
    // LABEL

    // Atomic Counter State
    STATE_INTEGERV(GL_ATOMIC_COUNTER_BUFFER_BINDING, 1);
    STATE_INTEGERV(GL_SHADER_STORAGE_BUFFER_BINDING, 1);
    // ATOMIC COUNTER BUFFER START
    // ATOMIC COUNTER BUFFER SIZE

    // Shader Storage Buffer State
    // SHADER STORAGE BUFFER BINDING
    // SHADER STORAGE BUFFER BINDING
    // SHADER STORAGE BUFFER START
    // SHADER STORAGE BUFFER SIZE

    // Sync (state per sync object)
    // GetSynciv
    // GetSynciv
    // GetSynciv
    // GetSynciv
    // GetObjectPtrLabel

    // Hints
    // PERSPECTIVE CORRECTION HINT            //COmpat!
    // POINT SMOOTH HINT                      //COmpat!
    STATE_INTEGERENUMV(GL_LINE_SMOOTH_HINT, 1);
    STATE_INTEGERENUMV(GL_POLYGON_SMOOTH_HINT, 1);
    // FOG HINT                                  //Compat
    // GENERATE MIPMAP HINT                      //Compat
    STATE_INTEGERENUMV(GL_TEXTURE_COMPRESSION_HINT, 1);
    STATE_INTEGERENUMV(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, 1);

    // Compute Dispatch State
    STATE_INTEGERV(GL_DISPATCH_INDIRECT_BUFFER_BINDING, 1);

    // Implementation Dependent Values
    STATE_INTEGERV(GL_MAX_CLIP_DISTANCES, 1);
    STATE_INTEGERV(GL_SUBPIXEL_BITS, 1);
    STATE_INTEGER64V(GL_MAX_ELEMENT_INDEX, 1);
    STATE_INTEGERENUMV(GL_IMPLEMENTATION_COLOR_READ_FORMAT, 1);
    STATE_INTEGERENUMV(GL_IMPLEMENTATION_COLOR_READ_TYPE, 1);
    STATE_INTEGERV(GL_MAX_3D_TEXTURE_SIZE, 1);
    STATE_INTEGERV(GL_MAX_TEXTURE_SIZE, 1);
    STATE_INTEGERV(GL_MAX_ARRAY_TEXTURE_LAYERS, 1);
    STATE_FLOATV(GL_MAX_TEXTURE_LOD_BIAS, 1);
    STATE_INTEGERV(GL_MAX_CUBE_MAP_TEXTURE_SIZE, 1);
    STATE_INTEGERV(GL_MAX_RENDERBUFFER_SIZE, 1);
    // MAX LIGHTS                                  //COmpat
    // MAX COLOR MATRIX STACK DEPTH                //COmpat
    // MAX MODELVIEW STACK DEPTH                   //COmpat
    // MAX PROJECTION STACK DEPTH                  //COmpat
    // MAX TEXTURE STACK DEPTH                     //COmpat
    // MAX PIXEL MAP TABLE                         //COmpat
    // MAX NAME STACK DEPTH                        //COmpat
    // MAX LIST NESTING                            //COmpat
    // MAX EVAL ORDER                              //COmpat
    // MAX ATTRIB STACK DEPTH                      //COmpat
    // MAX CLIENT ATTRIB STACK DEPTH               //COmpat
    // ALIASED POINT SIZE RANGE                    //COmpat
    // MAX CONVOLUTION WIDTH                       //COmpat
    // MAX CONVOLUTION HEIGHT                      //COmpat
    STATE_INTEGERV(GL_MAX_VIEWPORTS, 1);
    STATE_INTEGERV(GL_VIEWPORT_SUBPIXEL_BITS, 1);
    STATE_FLOATV(GL_VIEWPORT_BOUNDS_RANGE, 2);
    STATE_INTEGERENUMV(GL_LAYER_PROVOKING_VERTEX, 1);
    STATE_INTEGERENUMV(GL_VIEWPORT_INDEX_PROVOKING_VERTEX, 1);
    STATE_FLOATV(GL_POINT_SIZE_RANGE, 2);
    STATE_FLOATV(GL_POINT_SIZE_GRANULARITY, 1);
    STATE_FLOATV(GL_ALIASED_LINE_WIDTH_RANGE, 2);
    STATE_FLOATV(GL_SMOOTH_LINE_WIDTH_RANGE, 2);
    STATE_FLOATV(GL_SMOOTH_LINE_WIDTH_GRANULARITY, 1);
    STATE_INTEGERV(GL_MAX_ELEMENTS_INDICES, 1);
    STATE_INTEGERV(GL_MAX_ELEMENTS_VERTICES, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_ATTRIB_BINDINGS, 1);
    // STATE_INTEGERV(GL_COMPRESSED_TEXTURE_FORMATS);
    STATE_INTEGERV(GL_NUM_COMPRESSED_TEXTURE_FORMATS, 1);
    STATE_INTEGERV(GL_MAX_TEXTURE_BUFFER_SIZE, 1);
    STATE_INTEGERV(GL_MAX_RECTANGLE_TEXTURE_SIZE, 1);
    STATE_INTEGERV(GL_PROGRAM_BINARY_FORMATS, 1);
    STATE_INTEGERV(GL_NUM_PROGRAM_BINARY_FORMATS, 1);
    STATE_INTEGERV(GL_SHADER_BINARY_FORMATS, 1);
    STATE_INTEGERV(GL_NUM_SHADER_BINARY_FORMATS, 1);
    STATE_BOOLEANV(GL_SHADER_COMPILER, 1);
    STATE_INTEGERV(GL_MIN_MAP_BUFFER_ALIGNMENT, 1);
    STATE_INTEGERV(GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT, 1);
    STATE_INTEGERV(GL_MAJOR_VERSION, 1);
    STATE_INTEGERV(GL_MINOR_VERSION, 1);
    STATE_INTEGERV(GL_CONTEXT_FLAGS, 1);
    // EXTENSIONS        //Compat!
    // EXTENSIONS
    STATE_INTEGERV(GL_NUM_EXTENSIONS, 1);
    // RENDERER
    // SHADING LANGUAGE VERSION
    // SHADING LANGUAGE VERSION
    STATE_INTEGERV(GL_NUM_SHADING_LANGUAGE_VERSIONS, 1);
    // VENDOR
    // VERSION

    // Ok. continue here...!

    STATE_INTEGERV(GL_MAX_VERTEX_ATTRIBS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_UNIFORM_VECTORS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_OUTPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_TESS_GEN_LEVEL, 1);
    STATE_INTEGERV(GL_MAX_PATCH_VERTICES, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_OUTPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_PATCH_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_TOTAL_OUTPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_INPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_SHADER_STORAGE_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_OUTPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_INPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_SHADER_STORAGE_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_INPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_OUTPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_OUTPUT_VERTICES, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_SHADER_INVOCATIONS, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_STREAMS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_SHADER_STORAGE_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_UNIFORM_VECTORS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_INPUT_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MIN_PROGRAM_TEXTURE_GATHER_OFFSET, 1);
    STATE_INTEGERV(GL_MAX_PROGRAM_TEXTURE_GATHER_OFFSET, 1);
    // MAX TEXTURE UNITS   //Compat
    // MAX TEXTURE COORDS  //COmpat
    STATE_INTEGERV(GL_MAX_FRAGMENT_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, 1);
    // STATE_INTEGERV(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_COMPUTE_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, 1);
    STATE_INTEGERV(GL_MIN_PROGRAM_TEXEL_OFFSET, 1);
    STATE_INTEGERV(GL_MAX_PROGRAM_TEXEL_OFFSET, 1);
    STATE_INTEGERV(GL_MAX_UNIFORM_BUFFER_BINDINGS, 1);
    STATE_INTEGERV(GL_MAX_UNIFORM_BLOCK_SIZE, 1);
    STATE_INTEGERV(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_UNIFORM_BLOCKS, 1);
    STATE_INTEGERV(GL_MAX_VARYING_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_VARYING_VECTORS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_SUBROUTINES, 1);
    STATE_INTEGERV(GL_MAX_SUBROUTINE_UNIFORM_LOCATIONS, 1);
    STATE_INTEGERV(GL_MAX_UNIFORM_LOCATIONS, 1);
    STATE_INTEGERV(GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, 1);
    STATE_INTEGERV(GL_MAX_ATOMIC_COUNTER_BUFFER_SIZE, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_ATOMIC_COUNTER_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_ATOMIC_COUNTERS, 1);
    STATE_INTEGERV(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, 1);
    STATE_INTEGER64V(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS, 1);
    STATE_INTEGERV(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, 1);
    STATE_INTEGERV(GL_MAX_IMAGE_UNITS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_SHADER_OUTPUT_RESOURCES, 1);
    STATE_INTEGERV(GL_MAX_IMAGE_SAMPLES, 1);
    STATE_INTEGERV(GL_MAX_VERTEX_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_TESS_CONTROL_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_TESS_EVALUATION_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_GEOMETRY_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_FRAGMENT_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_IMAGE_UNIFORMS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_GEOMETRY_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_TESS_CONTROL_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_TESS_EVALUATION_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS, 1);
    STATE_INTEGERV(GL_DEBUG_LOGGED_MESSAGES, 1);
    STATE_INTEGERV(GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH, 1);
    STATE_ISENABLED(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    STATE_INTEGERV(GL_DEBUG_GROUP_STACK_DEPTH, 1);
    STATE_ISENABLED(GL_DEBUG_OUTPUT);
    STATE_INTEGERV(GL_MAX_DEBUG_MESSAGE_LENGTH, 1);
    STATE_INTEGERV(GL_MAX_DEBUG_LOGGED_MESSAGES, 1);
    STATE_INTEGERV(GL_MAX_DEBUG_GROUP_STACK_DEPTH, 1);
    STATE_INTEGERV(GL_MAX_LABEL_LENGTH, 1);
    STATE_INTEGERV(GL_MAX_SAMPLE_MASK_WORDS, 1);
    STATE_INTEGERV(GL_MAX_SAMPLES, 1);
    STATE_INTEGERV(GL_MAX_COLOR_TEXTURE_SAMPLES, 1);
    STATE_INTEGERV(GL_MAX_DEPTH_TEXTURE_SAMPLES, 1);
    STATE_INTEGERV(GL_MAX_INTEGER_SAMPLES, 1);
    STATE_FLOATV(GL_MIN_FRAGMENT_INTERPOLATION_OFFSET, 1);
    STATE_FLOATV(GL_MAX_FRAGMENT_INTERPOLATION_OFFSET, 1);
    STATE_INTEGERV(GL_FRAGMENT_INTERPOLATION_OFFSET_BITS, 1);
    STATE_INTEGERV(GL_MAX_DRAW_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, 1);
    STATE_INTEGERV(GL_MAX_COLOR_ATTACHMENTS, 1);
    STATE_INTEGERV(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, 1);
    STATE_INTEGERV(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS, 1);
    STATE_INTEGERV(GL_MAX_TRANSFORM_FEEDBACK_BUFFERS, 1);
    STATE_BOOLEANV(GL_DOUBLEBUFFER, 1);
    STATE_BOOLEANV(GL_STEREO, 1);
    STATE_INTEGERV(GL_SAMPLE_BUFFERS, 1);
    STATE_INTEGERV(GL_SAMPLES, 1);
    STATE_INTEGERV(GL_COPY_READ_BUFFER_BINDING, 1);
    STATE_INTEGERV(GL_COPY_WRITE_BUFFER_BINDING, 1);


        if (!store) {
            //in the first run of this loop just resize the container.
            resource->m_Items.resize(loc + 1);
        }

    }
    return ret;
}

opaque_id_t GLContext::getId() const { return m_Id; }

void APIENTRY
GLContext::debugOutputCallback(GLenum source, GLenum type, GLuint id,
                               GLenum severity, GLsizei length,
                               const GLchar* message, const GLvoid* userParam) {
    if (gc) {
        gc->setDebugOutput(source, type, id, severity, length, message,
                           userParam);
    }
}

void GLContext::firstUse() {
    GLint maxExtensions;

    std::vector<std::string> exts;

    enum {
        NO_DEBUG_OUTPUT,
        DEBUG_OUTPUT_ARB,
        DEBUG_OUTPUT_KHR
    }  debugOutputSupporStatus = NO_DEBUG_OUTPUT;

    m_Version.initialize(reinterpret_cast<const char*>(
            DIRECT_CALL_CHK(glGetString)(GL_VERSION)));

    //Needed API library is already loaded during startup or in CreateContext-related action
    // depending on requested ctx version.
    //However, drivers may give higher versions than requested,/making more
    //entrypoints usable. To avoid nullptr crashes on additional pointers recompute
    //needed library according to GL strings and load more entrypoints.
    //For example LIBRARY_ES2 is usually promoted to LIBRARY_ES3 on EGL, if ES3.0 is supported.
    //On WGL/GLX nothing happens below.
    EarlyGlobalState::getApiLoader().loadLibraries(m_Version.getNeededApiLibraries(getDisplay()));
    

    if (hasCapability(ContextCap::HasGetStringI)) {
        DIRECT_CALL_CHK(glGetIntegerv)(GL_NUM_EXTENSIONS, &maxExtensions);
        exts.resize(static_cast<size_t>(maxExtensions));
        for (size_t i = 0; i < static_cast<size_t>(maxExtensions); i++) {
            exts[i] = (const char*)DIRECT_CALL_CHK(glGetStringi)(GL_EXTENSIONS,
                                                                 static_cast<GLuint>(i));
        }
    } else {
        std::string allExts =
                (const char*)DIRECT_CALL_CHK(glGetString)(GL_EXTENSIONS);
        std::istringstream allExtsStream(allExts);
        std::copy(std::istream_iterator<std::string>(allExtsStream),
                  std::istream_iterator<std::string>(),
                  std::back_inserter<std::vector<std::string> >(exts));
    }

    for (size_t i = 0; i < exts.size(); i++) {
        if (strcmp("GL_ARB_debug_output", exts[i].c_str()) == 0) {
            if (debugOutputSupporStatus == NO_DEBUG_OUTPUT) {
               debugOutputSupporStatus = DEBUG_OUTPUT_ARB;
            }
        }
        if (strcmp("GL_KHR_debug", exts[i].c_str()) == 0) {
            //prefer GL_KHR_debug over GL_ARB_debug_output
            debugOutputSupporStatus = DEBUG_OUTPUT_KHR;
        }
        if (strcmp("GL_NVX_gpu_memory_info", exts[i].c_str()) == 0)
            m_HasNVXMemoryInfo = true;
    }

    switch (debugOutputSupporStatus) {
        case DEBUG_OUTPUT_ARB:
            DIRECT_CALL_CHK(glEnable)(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
            DIRECT_CALL_CHK(glDebugMessageCallbackARB)(debugOutputCallback, NULL);
            break;
        case DEBUG_OUTPUT_KHR:
            DIRECT_CALL_CHK(glEnable)(GL_DEBUG_OUTPUT);
            DIRECT_CALL_CHK(glEnable)(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            DIRECT_CALL_CHK(glDebugMessageCallback)(debugOutputCallback, NULL);
            break;
        case NO_DEBUG_OUTPUT:
        default:
            break;
    }

    shadow().getTexUnits().init();
}

bool GLContext::hasCapability(ContextCap cap) const {
    GLContextVersion version = getVersion();
    switch (cap) {
        case ContextCap::PixelBufferObjects:
            return version.check(GLContextVersion::Type::DT, 2, 1) ||
                   version.check(GLContextVersion::Type::ES, 3);

        case ContextCap::FramebufferObjects:
            return version.check(GLContextVersion::Type::DT, 3) ||
                   version.check(GLContextVersion::Type::ES, 2);

        case ContextCap::SeparateReadDrawFramebufferObjects:
            return hasCapability(ContextCap::FramebufferObjects) &&
                   (version.check(GLContextVersion::Type::DT, 3) ||
                    version.check(GLContextVersion::Type::ES, 3));

        case ContextCap::ReadBuffer:
            return version.check(GLContextVersion::Type::ES, 3) ||
                   version.check(GLContextVersion::Type::DT);

        case ContextCap::ReadBuffersFrontBuffer:
            return version.check(GLContextVersion::Type::DT);

        case ContextCap::DrawBuffersMRT:
            return version.check(GLContextVersion::Type::ES, 3) ||
                   version.check(GLContextVersion::Type::DT, 2);

        case ContextCap::TextureMultisample:
            return version.check(GLContextVersion::Type::ES, 3, 1) || 
                   version.check(GLContextVersion::Type::DT, 3, 2);

        case ContextCap::RenderBufferMultisample:
            return version.check(GLContextVersion::Type::ES, 3) ||
                   version.check(GLContextVersion::Type::DT, 3);

        case ContextCap::TextureQueryStencilBits:
            return version.check(GLContextVersion::Type::ES, 3, 1) || 
                   version.check(GLContextVersion::Type::DT, 3);

        case ContextCap::MultipleFramebufferAttachments:
            hasCapability(ContextCap::FramebufferObjects) &&
                    (version.check(GLContextVersion::Type::ES, 3) ||
                     version.check(GLContextVersion::Type::DT, 3));

        case ContextCap::CanQueryFramebufferAttachmentBitSize:
            hasCapability(ContextCap::FramebufferObjects) &&
                    (version.check(GLContextVersion::Type::ES, 3) ||
                     version.check(GLContextVersion::Type::DT, 3));

        case ContextCap::Has64BitGetters:
            return version.check(GLContextVersion::Type::DT, 3, 2) ||
                   version.check(GLContextVersion::Type::ES, 3);

        case ContextCap::HasGetStringI:
            return version.check(GLContextVersion::Type::DT, 3) ||
                   version.check(GLContextVersion::Type::ES, 3);

        case ContextCap::TextureGetters:
            return version.check(GLContextVersion::Type::DT);

        case ContextCap::GetBufferSubData:
            return version.check(GLContextVersion::Type::DT);

        case ContextCap::MapBuffer:
            return version.check(GLContextVersion::Type::DT, 3) ||
                version.check(GLContextVersion::Type::ES, 3);

        case ContextCap::GLSLShaders:
            return version.check(GLContextVersion::Type::DT, 2) ||
                version.check(GLContextVersion::Type::ES, 2);

        default:
            DGL_ASSERT(0);
            return false;
    }
}

const GLContextCreationData& GLContext::getContextCreationData() const {
    return m_CreationData;
}

GLAuxContext* GLContext::getAuxContext() {
    if (!m_AuxContext) {
        m_AuxContext = GLAuxContext::Create(this);
    }
    return m_AuxContext.get();
}

const DGLDisplayState* GLContext::getDisplay() const { return m_Display; }

}    // namespace dglState
