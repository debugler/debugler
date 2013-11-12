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

#include <DGLNet/protocol/pixeltransfer.h>

namespace {

// The fixture for testing class Foo.
class DGLNetUT : public ::testing::Test {};

TEST_F(DGLNetUT, formats_iformat) {
    DGLPixelTransfer rgba8(std::vector<GLint>(), std::vector<GLint>(),
                           GL_RGBA8);
    EXPECT_EQ(rgba8.getFormat(), GL_RGBA);
    EXPECT_EQ(rgba8.getType(), GL_UNSIGNED_BYTE);
}

TEST_F(DGLNetUT, formats_noiformat) {

    std::vector<GLint> rgbaSizes(4, 0);
    rgbaSizes[0] = rgbaSizes[1] = rgbaSizes[2] = 8;
    std::vector<GLint> dsSizes(2, 0);

    DGLPixelTransfer rgba8(rgbaSizes, dsSizes, 0);
    EXPECT_EQ(rgba8.getFormat(), GL_RGB);
    EXPECT_EQ(rgba8.getType(), GL_UNSIGNED_BYTE);
}

}    // namespace
