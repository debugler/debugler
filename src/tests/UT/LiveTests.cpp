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


#include "gtest/gtest.h"

#include <DGLGui/dglprocess.h>
#include <DGLCommon/gl-headers.h>
#include <DGLNet/client.h>

#include <boost/thread/thread.hpp>


namespace {

    class LiveTest : public ::testing::Test {
    protected:

        LiveTest() {
            if (once) {
#ifdef _WIN32
                SetCurrentDirectory("..");
#endif
                once = false;
            }

            DGLProcess* process = DGLProcess::Create(
#ifdef _WIN32
                 "C:\\Python27\\python.exe", "..", "..\\..\\src\\tests\\samples\\simple.py",
#else
                 "python", "..", CMAKE_CURRENT_SOURCE_DIR"/../samples/simple.py",
#endif
                 8888, false);
            while (!process->waitReady(100)) {}
            delete process;
        }

        virtual ~LiveTest() {
        }

        virtual void SetUp() {}

        virtual void TearDown() {}

        static bool once;
    };
    bool LiveTest::once = true;


    namespace stubs {
        class Controller: public dglnet::IController {
        public:
            virtual void onSetStatus(std::string) {}
            virtual void onSocket() {}
        };

        class MessageHandler: public dglnet::MessageHandler {
            
            virtual void doHandle(const dglnet::HelloMessage& msg) { mLastMessage = new dglnet::HelloMessage(msg); }
            virtual void doHandle(const dglnet::ConfigurationMessage& msg) { mLastMessage = new dglnet::ConfigurationMessage(msg); }
            virtual void doHandle(const dglnet::BreakedCallMessage& msg) { mLastMessage = new dglnet::BreakedCallMessage(msg); }
            virtual void doHandle(const dglnet::ContinueBreakMessage& msg) { mLastMessage = new dglnet::ContinueBreakMessage(msg); }
            virtual void doHandle(const dglnet::QueryCallTraceMessage& msg) { mLastMessage = new dglnet::QueryCallTraceMessage(msg); }
            virtual void doHandle(const dglnet::CallTraceMessage& msg) { mLastMessage = new dglnet::CallTraceMessage(msg); }
            virtual void doHandle(const dglnet::QueryResourceMessage& msg) { mLastMessage = new dglnet::QueryResourceMessage(msg); }
            virtual void doHandle(const dglnet::ResourceMessage& msg) { mLastMessage = new dglnet::ResourceMessage(msg); }
            virtual void doHandle(const dglnet::SetBreakPointsMessage& msg) { mLastMessage = new dglnet::SetBreakPointsMessage(msg); }
            void doHandleDisconnect(const std::string &msg) { mDisconnected = true; mDisconnectedReason = msg; }

        public:
            MessageHandler():mLastMessage(NULL),mDisconnected(false) {};
            dglnet::Message* getLastMessage() {
                dglnet::Message* ret = mLastMessage;
                mLastMessage = NULL;
                return ret;
            }

            dglnet::Message* mLastMessage;
            bool mDisconnected; std::string mDisconnectedReason;
        };
    }

    namespace utils {
        template<typename T>
        T* receiveMessage(dglnet::Transport* transport, stubs::MessageHandler& handler) {
            dglnet::Message* msg = NULL; 
            do {
                transport->run_one();
            } while (!handler.mDisconnected && !(msg = handler.getLastMessage()));
            EXPECT_EQ(false, handler.mDisconnected);
            if (!msg || !dynamic_cast<T*>(msg)) {
                return NULL;
            }
            return dynamic_cast<T*>(msg);
        }

        template<typename T>
        T* receiveUntilMessage(dglnet::Transport* transport, stubs::MessageHandler& handler) {
            dglnet::Message* msg = NULL; 
            do {
                transport->run_one();
                if (!handler.mDisconnected) {
                    msg = handler.getLastMessage();
                }
            } while (!handler.mDisconnected && (!msg || !(dynamic_cast<T*>(msg))));
            EXPECT_EQ(false, handler.mDisconnected);
            if (!msg || !dynamic_cast<T*>(msg)) {
                return NULL;
            }
            return dynamic_cast<T*>(msg);
        }

    }


    TEST_F(LiveTest, connect_disconnect) {
        stubs::Controller controllerStub;
        stubs::MessageHandler messageHandlerStub;
        boost::shared_ptr<dglnet::Client> client(new dglnet::Client(&controllerStub, &messageHandlerStub));
        client->connectServer("127.0.0.1", "8888");
        dglnet::HelloMessage * hello = utils::receiveMessage<dglnet::HelloMessage>(client.get(), messageHandlerStub);
        ASSERT_TRUE(hello != NULL);
#ifdef _WIN32
        EXPECT_EQ("python.exe", hello->m_ProcessName);
#else
        EXPECT_TRUE(std::string::npos != hello->m_ProcessName.find("python"));
#endif
        client->abort();
        EXPECT_TRUE(client.unique());
    }

   TEST_F(LiveTest, continue_break) {
       stubs::Controller controllerStub;
       stubs::MessageHandler messageHandlerStub;
       boost::shared_ptr<dglnet::Client> client(new dglnet::Client(&controllerStub, &messageHandlerStub));
       client->connectServer("127.0.0.1", "8888");

       utils::receiveMessage<dglnet::HelloMessage>(client.get(), messageHandlerStub);
       dglnet::BreakedCallMessage* breaked = utils::receiveMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
       ASSERT_TRUE(breaked != NULL);

#ifdef _WIN32
       EXPECT_EQ(wglGetCurrentContext_Call, breaked->m_entryp.getEntrypoint());
#else
       EXPECT_EQ(glXGetCurrentContext_Call, breaked->m_entryp.getEntrypoint());
#endif
       EXPECT_EQ(0, breaked->m_TraceSize);
       EXPECT_EQ(0, breaked->m_CtxReports.size());
       EXPECT_EQ(0, breaked->m_CurrentCtx);

       do {
           {
               dglnet::ContinueBreakMessage continueMsg(false);
               client->sendMessage(&continueMsg);
           }
           boost::this_thread::sleep(boost::posix_time::milliseconds(10));
           {
               dglnet::ContinueBreakMessage continueMsg(true);
               client->sendMessage(&continueMsg);
           }

           breaked = utils::receiveMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
           ASSERT_TRUE(breaked != NULL);
       } while (!breaked->m_CurrentCtx);       
       EXPECT_NE(0, breaked->m_TraceSize);
       EXPECT_EQ(1, breaked->m_CtxReports.size());
       EXPECT_NE(0, breaked->m_CurrentCtx);
       client->abort();
   }

   TEST_F(LiveTest, breakpoint) {
       stubs::Controller controllerStub;
       stubs::MessageHandler messageHandlerStub;
       boost::shared_ptr<dglnet::Client> client(new dglnet::Client(&controllerStub, &messageHandlerStub));
       client->connectServer("127.0.0.1", "8888");

       utils::receiveMessage<dglnet::HelloMessage>(client.get(), messageHandlerStub);
       dglnet::BreakedCallMessage* breaked = utils::receiveMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
       ASSERT_TRUE(breaked != NULL);

#ifdef _WIN32
       EXPECT_EQ(wglGetCurrentContext_Call, breaked->m_entryp.getEntrypoint());
#else
       EXPECT_EQ(glXGetCurrentContext_Call, breaked->m_entryp.getEntrypoint());
#endif
       EXPECT_EQ(0, breaked->m_TraceSize);
       EXPECT_EQ(0, breaked->m_CtxReports.size());
       EXPECT_EQ(0, breaked->m_CurrentCtx);

       {
           std::set<Entrypoint> breakpoints;
           breakpoints.insert(glDrawArrays_Call);
           dglnet::SetBreakPointsMessage breakPointMessage(breakpoints);
           client->sendMessage(&breakPointMessage);
       }
       {
           dglnet::ContinueBreakMessage continueMsg(false);
           client->sendMessage(&continueMsg);
       }

       breaked = utils::receiveMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
       ASSERT_TRUE(breaked != NULL);
        
       EXPECT_EQ(glDrawArrays_Call, breaked->m_entryp.getEntrypoint());

       client->abort();
   }

   TEST_F(LiveTest, entryp_retvals) {
       stubs::Controller controllerStub;
       stubs::MessageHandler messageHandlerStub;
       boost::shared_ptr<dglnet::Client> client(new dglnet::Client(&controllerStub, &messageHandlerStub));
       client->connectServer("127.0.0.1", "8888");

       dglnet::BreakedCallMessage* breaked = utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
       ASSERT_TRUE(breaked != NULL);

#ifdef _WIN32
#define _glCreateContext_Call wglCreateContext_Call
#else
#define _glCreateContext_Call glXCreateNewContext_Call
#endif
       {
           //set breakpoints && disable other breaking stuff
           std::set<Entrypoint> breakpoints;
           breakpoints.insert(_glCreateContext_Call);
           breakpoints.insert(glGetError_Call);
           breakpoints.insert(glCreateShader_Call);
           dglnet::SetBreakPointsMessage breakPointMessage(breakpoints);
           client->sendMessage(&breakPointMessage);

           dglnet::ConfigurationMessage config(DGLConfiguration(false, false, false));
           client->sendMessage(&config);
       }
       {
           //continue
           dglnet::ContinueBreakMessage continueMsg(false);
           client->sendMessage(&continueMsg);
       }

       bool wglCreateContext_done = false;
       bool glGetError_done = false;
       bool glCreateShader_done = false;

       while (!wglCreateContext_done || !glGetError_done || !glCreateShader_done) {
           breaked = utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
           ASSERT_TRUE(breaked != NULL);

           {
               //we should be breaked on one of earlier breakpoints
               ASSERT_TRUE(
                   breaked->m_entryp.getEntrypoint() == _glCreateContext_Call ||
                   breaked->m_entryp.getEntrypoint() == glGetError_Call ||
                   breaked->m_entryp.getEntrypoint() == glCreateShader_Call
                   );
               
               {
                   //advance one call
                   dglnet::ContinueBreakMessage step(dglnet::ContinueBreakMessage::STEP_CALL);
                   client->sendMessage(&step);
                   ASSERT_TRUE(utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub) != NULL);
               }
               
               {
                   //query callTrace with one entryp
                   dglnet::QueryCallTraceMessage queryCallTrace(0, 1);
                   client->sendMessage(&queryCallTrace);
               }

               dglnet::CallTraceMessage* callTrace = utils::receiveUntilMessage<dglnet::CallTraceMessage>(client.get(), messageHandlerStub);
               ASSERT_TRUE(callTrace != NULL);

#ifdef _WIN32
               HGLRC ctx;
#else
               GLXContext ctx;
#endif
               GLenum error;
               GLuint shader;

               switch (callTrace->m_Trace[0].getEntrypoint()) {
                   case _glCreateContext_Call:
                       wglCreateContext_done = true;
                       callTrace->m_Trace[0].getRetVal().get(ctx);
                       EXPECT_TRUE(ctx != NULL);
                       break;
                   case glGetError_Call:
                       glGetError_done = true;
                       callTrace->m_Trace[0].getRetVal().get(error);
                       EXPECT_EQ(GL_NO_ERROR, error);
                       break;
                   case glCreateShader_Call:
                       glCreateShader_done = true;
                       callTrace->m_Trace[0].getRetVal().get(shader);
                       EXPECT_TRUE(shader != 0);
                       break;
                   default:
                       ASSERT_TRUE(!"breaked at wrong entryp");
               }

               //continue execution
               dglnet::ContinueBreakMessage continueMsg(false);
               client->sendMessage(&continueMsg);
           }
       }

       client->abort();
   }


   TEST_F(LiveTest, framebuffer_query) {
       stubs::Controller controllerStub;
       stubs::MessageHandler messageHandlerStub;
       boost::shared_ptr<dglnet::Client> client(new dglnet::Client(&controllerStub, &messageHandlerStub));
       client->connectServer("127.0.0.1", "8888");

       dglnet::BreakedCallMessage* breaked = utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
       ASSERT_TRUE(breaked != NULL);

       {
           //set breakpoints && disable other breaking stuff
           std::set<Entrypoint> breakpoints;
           breakpoints.insert(glDrawArrays_Call);
           dglnet::SetBreakPointsMessage breakPointMessage(breakpoints);
           client->sendMessage(&breakPointMessage);

           dglnet::ConfigurationMessage config(DGLConfiguration(false, false, false));
           client->sendMessage(&config);
       }

       {
           //continue
           dglnet::ContinueBreakMessage continueMsg(false);
           client->sendMessage(&continueMsg);
       }
       
       breaked = utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub);
       ASSERT_TRUE(breaked != NULL);
       EXPECT_EQ(glDrawArrays_Call, breaked->m_entryp.getEntrypoint());

       for (int step = 0; step < 3; step++) {
           //step == 0: before first draw (GL_BACK should be cleared)
           //step == 1: after first draw (GL_BACK  should contain quad)
           //step == 2: after first swap (GL_FRONT should contain quad)

           {
               //query back framebuffer
               dglnet::QueryResourceMessage::ResourceQuery framebufferQuery;
               framebufferQuery.m_Type = DGLResource::ObjectTypeFramebuffer;
               framebufferQuery.m_ObjectName = ContextObjectName(breaked->m_CurrentCtx, step==2?GL_FRONT:GL_BACK);
               framebufferQuery.m_ListenerId = 3; //some rand value..
               std::vector<dglnet::QueryResourceMessage::ResourceQuery> resQueries;
               resQueries.push_back(framebufferQuery);
               dglnet::QueryResourceMessage query;
               query.m_ResourceQueries = resQueries;
               client->sendMessage(&query);
           }

           dglnet::ResourceMessage * resource = utils::receiveUntilMessage<dglnet::ResourceMessage>(client.get(), messageHandlerStub);
           std::string err;
           ASSERT_TRUE(resource->isOk(err));
           EXPECT_EQ(0, err.length());
           EXPECT_EQ(3, resource->m_ListenerId);
           DGLResourceFramebuffer * framebufferResource = dynamic_cast<DGLResourceFramebuffer*>(resource->m_Resource.get());
           ASSERT_TRUE(framebufferResource != NULL);

           ASSERT_EQ(GL_RGBA, framebufferResource->m_PixelRectangle->m_GLFormat);
           ASSERT_EQ(GL_UNSIGNED_BYTE, framebufferResource->m_PixelRectangle->m_GLType);
           EXPECT_EQ(0, framebufferResource->m_PixelRectangle->m_Samples);
           EXPECT_EQ(640, framebufferResource->m_PixelRectangle->m_Width);
           EXPECT_EQ(480, framebufferResource->m_PixelRectangle->m_Height);
           EXPECT_EQ(0, framebufferResource->m_PixelRectangle->m_InternalFormat);
           GLubyte* ptr = (GLubyte*)framebufferResource->m_PixelRectangle->getPtr();

           for (int y = 0; y < framebufferResource->m_PixelRectangle->m_Height; y++) {
               GLubyte* rowPtr = y * framebufferResource->m_PixelRectangle->m_RowBytes + ptr;
               for (int x = 0; x < framebufferResource->m_PixelRectangle->m_Width; x++) {
                   if ( step && x > 0.3 * framebufferResource->m_PixelRectangle->m_Width  &&
                        x < 0.7 * framebufferResource->m_PixelRectangle->m_Width  &&
                        y > 0.3 * framebufferResource->m_PixelRectangle->m_Height  &&
                        y < 0.7 * framebufferResource->m_PixelRectangle->m_Height) {
                       ASSERT_EQ(102 , rowPtr[4 * x + 0]);
                       ASSERT_EQ(127 , rowPtr[4 * x + 1]);
                       ASSERT_EQ(204 , rowPtr[4 * x + 2]);
                       ASSERT_EQ(255 , rowPtr[4 * x + 3]);
                   }
                   if (!step ||  x < 0.2 * framebufferResource->m_PixelRectangle->m_Width  ||
                       x > 0.8 * framebufferResource->m_PixelRectangle->m_Width  ||
                       y < 0.2 * framebufferResource->m_PixelRectangle->m_Height  ||
                       y > 0.8 * framebufferResource->m_PixelRectangle->m_Height) {
                           ASSERT_EQ(0, rowPtr[4 * x + 0]);
                           ASSERT_EQ(0, rowPtr[4 * x + 1]);
                           ASSERT_EQ(0, rowPtr[4 * x + 2]);
                           ASSERT_EQ(0, rowPtr[4 * x + 3]);
                   }
               }
           }
           if (step == 1) {
               //advance one frame (up to swap)
               dglnet::ContinueBreakMessage step(dglnet::ContinueBreakMessage::STEP_FRAME);
               client->sendMessage(&step);
               ASSERT_TRUE(utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub) != NULL);
           }
           //advance one call
           dglnet::ContinueBreakMessage step(dglnet::ContinueBreakMessage::STEP_CALL);
           client->sendMessage(&step);
           ASSERT_TRUE(utils::receiveUntilMessage<dglnet::BreakedCallMessage>(client.get(), messageHandlerStub) != NULL);
       }

       client->abort();
   }
    

}  // namespace


