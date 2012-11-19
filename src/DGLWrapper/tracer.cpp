#include <DGLNet/server.h>

#include <boost/make_shared.hpp>

#include "gl-wrappers.h"
#include "debugger.h"
#include "tracer.h"
#include "pointers.h"
#include "api-loader.h"


boost::shared_ptr<TracerBase> g_Tracers[NUM_ENTRYPOINTS];

void TracerBase::SetPrev(const boost::shared_ptr<TracerBase>& prev) {
    m_PrevTracer = prev;
}

RetValue TracerBase::Pre(const CalledEntryPoint& call) {
    return PrevPre(call);
}

void TracerBase::Post(const CalledEntryPoint& call, const RetValue& ret) {
    return PrevPost(call, ret);
}


RetValue TracerBase::PrevPre(const CalledEntryPoint& call) {
    if (m_PrevTracer)
        return m_PrevTracer->Pre(call);
    return RetValue();
}

void TracerBase::PrevPost(const CalledEntryPoint& call, const RetValue& ret) {
    if (m_PrevTracer)
        m_PrevTracer->Post(call, ret);
}

RetValue DefaultTracer::Pre(const CalledEntryPoint& call) {
    RetValue ret = PrevPre(call);

    g_Controller->getServer().lock();

    //do a fast non-blocking poll to get "interrupt" message, etc.."
    g_Controller->getServer().poll();

    if (g_Controller->getBreakState().breakAt(call.getEntrypoint())) {
        //we just hit a break;
        dglstate::GLContext* ctx = g_GLState.getCurrent();
        dglnet::BreakedCallMessage callStateMessage(call, g_Controller->getCallHistory().size(), ctx?ctx->getId():0, g_GLState.describe());
        g_Controller->getServer().sendMessage(&callStateMessage);
    }
    
    while (g_Controller->getBreakState().isBreaked()) {
        //block & loop until someone unbreaks us
        g_Controller->getServer().run_one();
    }

    g_Controller->getCallHistory().add(call);

    g_Controller->getBreakState().endStep();
    g_Controller->getServer().unlock();
    return ret;
}

RetValue GetProcAddressTracer::Pre(const CalledEntryPoint& call) {
    RetValue ret = PrevPre(call);

    if (ret.isSet()) return ret;

    Entrypoint entryp;
    const char* entrpName; call.getArgs()[0].get(entrpName);
    entryp = GetEntryPointEnum(entrpName);
    if (entryp == NO_ENTRYPOINT) {
        //we do not support this entrypoint
        //TODO: add partial support for unknown entrypoints
        return ret; 
    }
    //we recognize this entrypoint, load if nessesary and return address to  wrapper
    LoadOpenGLExtPointer(entryp);
    ret = getWrapperPointer(entryp);
    return ret;
}

void ContextTracer::Post(const CalledEntryPoint& call, const RetValue& ret) {
    HGLRC ctx;
    HDC device;
    BOOL retBool;
   
    switch (call.getEntrypoint()) {
        case wglCreateContext_Call:
            ret.get(ctx);
            if (NULL != ctx) {
                g_GLState.ensureContext(reinterpret_cast<int32_t>(ctx));
            }
            break;
        case wglMakeCurrent_Call:
            ret.get(retBool);
            if (retBool) {
                call.getArgs()[0].get(device);
                call.getArgs()[1].get(ctx);
                g_GLState.bindContext(reinterpret_cast<uint32_t>(ctx), reinterpret_cast<uint32_t>(device));
            }
            break;
        case wglDeleteContext_Call:
            ret.get(retBool);
            if (retBool) {
                call.getArgs()[0].get(ctx);
                g_GLState.deleteContext(reinterpret_cast<int32_t>(ctx));
            }
            break;
    }
    PrevPost(call, ret);
}

void TextureTracer::Post(const CalledEntryPoint& call, const RetValue& ret) {
    Entrypoint entrp = call.getEntrypoint();
    if (g_GLState.getCurrent()) {

        if (entrp == glGenTextures_Call) {
            GLsizei n = 0;
            call.getArgs()[0].get(n);

            GLuint* names;
            call.getArgs()[1].get(names);

            for (GLsizei i = 0; i < n; i++) {
                g_GLState.getCurrent()->ensureTexture(names[i]);
            }
        } else if (entrp == glDeleteTextures_Call) {
            GLsizei n = 0;
            call.getArgs()[0].get(n);

            const GLuint* names;
            call.getArgs()[1].get(names);

            for (GLsizei i = 0; i < n; i++) {
                g_GLState.getCurrent()->deleteTexture(names[i]);
            }
        } else if (entrp == glBindTexture_Call) {
            GLenum target;
            call.getArgs()[0].get(target);
            GLuint name;
            call.getArgs()[1].get(name);
            g_GLState.getCurrent()->ensureTexture(name)->setTarget(target);
        }
    }
    PrevPost(call, ret);
}

void BufferTracer::Post(const CalledEntryPoint& call, const RetValue& ret) {
    Entrypoint entrp = call.getEntrypoint();
    if (g_GLState.getCurrent()) {

        if (entrp == glGenBuffers_Call) {
            GLsizei n = 0;
            call.getArgs()[0].get(n);

            GLuint* names;
            call.getArgs()[1].get(names);

            for (GLsizei i = 0; i < n; i++) {
                g_GLState.getCurrent()->ensureBuffer(names[i]);
            }
        } else if (entrp == glDeleteBuffers_Call) {
            GLsizei n = 0;
            call.getArgs()[0].get(n);

            const GLuint* names;
            call.getArgs()[1].get(names);

            for (GLsizei i = 0; i < n; i++) {
                g_GLState.getCurrent()->deleteBuffer(names[i]);
            }
        } else if (entrp == glBindBuffer_Call) {
            GLenum target;
            call.getArgs()[0].get(target);
            GLuint name;
            call.getArgs()[1].get(name);
            g_GLState.getCurrent()->ensureBuffer(name)->setTarget(target);
        }
    }
    PrevPost(call, ret);
}

RetValue ProgramTracer::Pre(const CalledEntryPoint& call) {
    RetValue ret = PrevPre(call);
    
    if (call.getEntrypoint() == glUseProgram_Call) {

        GLint currentProgramName;
        DIRECT_CALL(glGetIntegerv)(GL_CURRENT_PROGRAM, &currentProgramName);

        dglstate::GLProgramObj* currentProgram = g_GLState.getCurrent()->ensureProgram(currentProgramName);

        currentProgram->use(false);
        if (currentProgram->mayDelete()) {
            g_GLState.getCurrent()->deleteProgram(currentProgramName);
        }
    }
    return ret;
}

void ProgramTracer::Post(const CalledEntryPoint& call, const RetValue& ret) {
    Entrypoint entrp = call.getEntrypoint();

    if (g_GLState.getCurrent()) {
        GLuint name;
        if (entrp == glCreateProgram_Call) {

            ret.get(name);

            g_GLState.getCurrent()->ensureProgram(name);

        } else if (entrp == glDeleteProgram_Call) {

            call.getArgs()[0].get(name);

            dglstate::GLProgramObj* program = g_GLState.getCurrent()->ensureProgram(name);
            program->markDeleted();
            if (program->mayDelete()) {
                g_GLState.getCurrent()->deleteProgram(name);
            }

        } else if (entrp == glUseProgram_Call) {

            call.getArgs()[0].get(name);

            g_GLState.getCurrent()->ensureProgram(name)->use(true);

        }
    }
    PrevPost(call, ret);
}


void FBOTracer::Post(const CalledEntryPoint& call, const RetValue& ret) {
    Entrypoint entrp = call.getEntrypoint();
    if (g_GLState.getCurrent()) {

        if (entrp == glGenFramebuffers_Call) {
            GLsizei n = 0;
            call.getArgs()[0].get(n);

            GLuint* names;
            call.getArgs()[1].get(names);

            for (GLsizei i = 0; i < n; i++) {
                g_GLState.getCurrent()->ensureFBO(names[i]);
            }
        } else if (entrp == glDeleteFramebuffers_Call) {
            GLsizei n = 0;
            call.getArgs()[0].get(n);

            const GLuint* names;
            call.getArgs()[1].get(names);

            for (GLsizei i = 0; i < n; i++) {
                g_GLState.getCurrent()->deleteFBO(names[i]);
            }
        } else if (entrp == glBindFramebuffer_Call) {
            GLenum target;
            call.getArgs()[0].get(target);
            GLuint name;
            call.getArgs()[1].get(name);
            g_GLState.getCurrent()->ensureFBO(name)->setTarget(target);
        }
    }
    PrevPost(call, ret);
}