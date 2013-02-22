
#include "gtest/gtest.h"

#include <DGLCommon/gl-types.h>
#include <DGLCommon/gl-formats.h>

namespace {

    // The fixture for testing class Foo.
    class DGLCommonUT : public ::testing::Test {
    protected:
        // You can remove any or all of the following functions if its body
        // is empty.

        DGLCommonUT() {
            // You can do set-up work for each test here.
        }

        virtual ~DGLCommonUT() {
            // You can do clean-up work that doesn't throw exceptions here.
        }

        // If the constructor and destructor are not enough for setting up
        // and cleaning up each test, you can define the following methods:

        virtual void SetUp() {
            // Code here will be called immediately after the constructor (right
            // before each test).
        }

        virtual void TearDown() {
            // Code here will be called immediately after each test (right
            // before the destructor).
        }

        // Objects declared here can be used by all tests in the test case for Foo.
    };

    // Smoke test all inputs of codegen has been parsed to functionList
    TEST_F(DGLCommonUT, codegen_entryps) {
        //gl.h
        EXPECT_EQ(GetEntryPointName(glEnable_Call), "glEnable");

        //glext.h
        EXPECT_EQ(GetEntryPointName(glDrawArraysInstanced_Call), "glDrawArraysInstanced");
        EXPECT_EQ(GetEntryPointName(glDrawArraysInstancedARB_Call), "glDrawArraysInstancedARB");

        //wgl
        EXPECT_EQ(GetEntryPointName(wglCreateContext_Call), "wglCreateContext");

        //wgl-notrace
        EXPECT_EQ(GetEntryPointName(wglSetPixelFormat_Call), "wglSetPixelFormat");
        
        //wglext.h
        EXPECT_EQ(GetEntryPointName(wglCreateContextAttribsARB_Call), "wglCreateContextAttribsARB");

        //egl.h
        EXPECT_EQ(GetEntryPointName(eglBindAPI_Call), "eglBindAPI");

        //eglext.h
        EXPECT_EQ(GetEntryPointName(eglCreateImageKHR_Call), "eglCreateImageKHR");

        const char* null = NULL;
        //null
        EXPECT_EQ(std::string(GetEntryPointName(NO_ENTRYPOINT)), "<unknown>");
    }

    TEST_F(DGLCommonUT, codegen_entryp_names) {
        EXPECT_EQ(GetEntryPointEnum("bad"), NO_ENTRYPOINT);
        EXPECT_EQ(GetEntryPointEnum("glDrawArrays"), glDrawArrays_Call);
        EXPECT_EQ(GetEntryPointName(GetEntryPointEnum("glDrawArrays")), "glDrawArrays");
        EXPECT_EQ(GetEntryPointEnum(GetEntryPointName(glDrawArrays_Call)), glDrawArrays_Call);
    }

    TEST_F(DGLCommonUT, formats_iformat) {
       DGLPixelTransfer rgba8(std::vector<GLint>(), std::vector<GLint>(), GL_RGBA8);
       EXPECT_EQ(rgba8.getFormat(), GL_RGBA);
       EXPECT_EQ(rgba8.getType(), GL_UNSIGNED_BYTE);

    }

    TEST_F(DGLCommonUT, formats_noiformat) {

        std::vector<GLint>rgbaSizes(4, 0);
        rgbaSizes[0] = rgbaSizes[1] = rgbaSizes[2] = 8;
        std::vector<GLint>dsSizes(2, 0);

        DGLPixelTransfer rgba8(rgbaSizes, dsSizes, 0);
        EXPECT_EQ(rgba8.getFormat(), GL_RGB);
        EXPECT_EQ(rgba8.getType(), GL_FLOAT);
    }


}  // namespace

