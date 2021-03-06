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

#include "platform.h"
#ifndef OPENGL_ES2
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

#include <stdexcept>

class GLWFPlatWindowCtx : public PlatWindowCtx {
   public:
    GLWFPlatWindowCtx() : m_window(NULL) {
#ifdef OPENGL_ES2
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
#endif
        m_window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
        if (!m_window) {
            throw std::runtime_error("Cannot create glwf window");
        }
    }

    virtual void makeCurrent() override {
        glfwMakeContextCurrent(m_window);
#ifndef OPENGL_ES2
        if (!glewInitDone) {
            if (glewInit() != GLEW_OK)
                throw std::runtime_error("Cannot init glew.");
            glewInitDone = true;

            fprintf(stdout, "Using GLEW %s\n", glewGetString(GLEW_VERSION));
        }
#endif
    }

    virtual void swapBuffers() override { glfwSwapBuffers(m_window); }

    virtual bool pendingClose() override {
        return glfwWindowShouldClose(m_window) != 0;
    }

    virtual void resize(int newWidth, int newHeight) {
        glfwSetWindowSize(m_window, newWidth, newHeight);
    }

   private:
    GLFWwindow* m_window;
    static bool glewInitDone;
};

bool GLWFPlatWindowCtx::glewInitDone = false;

Platform::Platform() {
    if (!glfwInit()) throw std::runtime_error("Cannot init glfw.");
}

Platform::~Platform() { glfwTerminate(); }

void Platform::pollEvents() { glfwPollEvents(); }

std::shared_ptr<PlatWindowCtx> Platform::createWindow() {
    return std::make_shared<GLWFPlatWindowCtx>();
}