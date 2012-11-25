#ifndef GL_STATE_H
#define GL_STATE_H

#include <DGLCommon/gl-types.h>
#include <DGLNet/message.h>

#include <map>
#include <queue>

namespace dglstate {

class GLObj {
public:
    GLObj();
    GLObj(GLuint name);
    GLuint getName();
private:
    GLuint m_Name;
};

class GLTextureObj: public GLObj {
public:
    GLTextureObj(GLuint name);
    GLTextureObj() {}
    void setTarget(GLenum);
    GLenum getTarget();
private:
    GLenum m_Target;
};

class GLBufferObj: public GLObj {
public:
    GLBufferObj(GLuint name);
    void setTarget(GLenum);
    GLenum getTarget();
    GLBufferObj() {}
private:
    GLenum m_Target;
};

class GLProgramObj: public GLObj {
public:
    GLProgramObj(GLuint name);
    GLProgramObj() {}
    void use(bool inUse); 
    bool mayDelete(); 
    void markDeleted();

private:
    int m_InUse;
    bool m_Deleted;
};

class GLFBObj: public GLObj {
public:
    GLFBObj(GLuint name);
    GLFBObj() {}
    void setTarget(GLenum);
    GLenum getTarget();
private:
    GLenum m_Target;
};

//Native platform interface surface. HDC on windows
class NPISurface {
public:
    NPISurface(uint32_t);
    uint32_t getId();
    bool isDoubleBuffered();
    bool isStereo();
    bool isAlpha();
    int getWidth(); 
    int getHeight(); 
private:
    uint32_t m_Id;
    int m_Width, m_Height;
    bool m_Stereo, m_DoubleBuffered, m_Alpha;
};

class GLContext {
public:
    GLContext(uint32_t id);
    bool m_deleted;
    std::map<GLuint, GLTextureObj> m_Textures;
    std::map<GLuint, GLBufferObj> m_Buffers;
    std::map<GLuint, GLProgramObj> m_Programs;
    std::map<GLuint, GLFBObj> m_FBOs;

    dglnet::ContextReport describe();

    void use(bool);
    bool lazyDelete();
    bool isDeleted();

    NPISurface* getNpiSurface();
    void setNpiSurface(NPISurface*);

    GLTextureObj* ensureTexture(GLuint name);
    void deleteTexture(GLuint name);
    GLBufferObj* ensureBuffer(GLuint name);
    void deleteBuffer(GLuint name);
    GLFBObj* ensureFBO(GLuint name);
    void deleteFBO(GLuint name);
    GLProgramObj* ensureProgram(GLuint name);
    void deleteProgram(GLuint name);

    void queryTexture(GLuint name, dglnet::TextureMessage& ret);
    void queryBuffer(GLuint name, dglnet::BufferMessage& ret);
    void queryFramebuffer(GLuint bufferEnum, dglnet::FramebufferMessage& ret);
    void queryFBO(GLuint name, dglnet::FBOMessage& ret);

    int32_t getId();

    GLenum getError();
    GLenum peekError();

private:
    
    void startQuery();
    bool endQuery(std::string& message);
        

    int32_t m_Id;
    bool m_InUse, m_Deleted;
    NPISurface* m_NPISurface;

    std::queue<GLenum> m_PokedErrorQueue;
};

} //namespace
#endif
