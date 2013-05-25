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

#ifndef PIXELTRANSFER_H
#define PIXELTRANSFER_H

#include <DGLCommon/gl-types.h>
#include <DGLNet/protocol/anyvalue.h>

#include <vector>

struct GLDataType {
    GLenum type;
    unsigned int byteSize;
    bool packed;
    void (*blitFunc) (const int* outputOffsets, int width, int height, const void * src, void* dst, int srcStride, int dstStride, int srcPixelSize, int dstPixelSize, int srcComponents, std::pair<float, float>* scale);
    std::vector<AnyValue> (*extractor)(const void*, int);
    unsigned int components;
}; 

struct GLDataFormat {
    GLenum format;
    unsigned int components;
};

struct GLInternalFormat {
    GLenum internalFormat;
    GLenum dataFormat;
    GLenum dataType;
};

class GLFormats {
public:
    static GLInternalFormat* getInternalFormat(GLenum internalFormat);
    static GLDataFormat* getDataFormat(GLenum dataFormat);
    static GLDataType* getDataType(GLenum dataType);  
};

class DGLPixelTransfer {
public: 
    DGLPixelTransfer(std::vector<GLint> _rgbaSizes, std::vector<GLint> _depthStencilSizes, GLenum internalFormat);

    bool isValid();
    GLenum getFormat();
    GLenum getType();
    unsigned int getPixelSize();

private:
    GLDataFormat* m_DataFormat;
    GLDataType* m_DataType;
};

#endif