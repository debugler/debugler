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


#include "gl-resource.h"
#include <DGLCommon/gl-types.h>

DGLResourceFBO::FBOAttachment::FBOAttachment(gl_t id):m_Ok(true),m_Id(id) {}

void DGLResourceFBO::FBOAttachment::error(std::string msg) {
    m_Ok = false;
    m_ErrorMsg = msg;
}

bool DGLResourceFBO::FBOAttachment::isOk(std::string& msg) const {
    msg = m_ErrorMsg;
    return m_Ok;
}

ContextObjectName::ContextObjectName():m_Context(0), m_Name(0), m_Target(0) {}
ContextObjectName::ContextObjectName(opaque_id_t context, gl_t name, gl_t target):m_Name(name),m_Context(context),m_Target(target) {}
ContextObjectName::~ContextObjectName() {}

bool ContextObjectName::operator==(const ContextObjectName&rhs) const {

    //it is crucial that m_Target is not get into account here (ID + ctxID is enough to indentify an object and m_Target is optional)

    return m_Context == rhs.m_Context && m_Name == rhs.m_Name;
}

bool ContextObjectName::operator<(const ContextObjectName&rhs) const {

    //it is crucial that m_Target is not get into account here (ID + ctxID is enough to indentify an object and m_Target is optional)

    if (m_Context < rhs.m_Context)
        return true;
    if (m_Context > rhs.m_Context)
        return false;
    if (m_Name < rhs.m_Name)
        return true;
    return false;
}

DGLPixelRectangle::DGLPixelRectangle(value_t width, value_t height, value_t rowBytes, gl_t glFormat, gl_t glType, gl_t iFormat, value_t samples):m_Width(width),
    m_Height(height), m_RowBytes(rowBytes), m_GLFormat(glFormat), m_GLType(glType), m_InternalFormat(iFormat), m_Samples(samples), m_Storage(NULL) {

        if (m_Height * m_RowBytes) {
            m_Storage = malloc(m_Height * m_RowBytes);
        }
}

DGLPixelRectangle::DGLPixelRectangle(const DGLPixelRectangle& rhs):m_Width(rhs.m_Width), m_Height(rhs.m_Height),
    m_RowBytes(rhs.m_RowBytes), m_GLFormat(rhs.m_GLFormat), m_GLType(rhs.m_GLType), m_InternalFormat(rhs.m_InternalFormat), m_Samples(rhs.m_Samples) {
        if (rhs.getPtr()) {
            m_Storage = malloc(m_Height * m_RowBytes);
            memcpy(m_Storage, rhs.getPtr(), m_Height * m_RowBytes);
        } else {
            m_Storage = NULL;
        }
}

DGLPixelRectangle::~DGLPixelRectangle() {
    if (m_Storage) {
        free(m_Storage);
        m_Storage = NULL;
    }
}

void* DGLPixelRectangle::getPtr() const {
    return m_Storage;
}

size_t DGLPixelRectangle::getSize() const {
    return m_Height * m_RowBytes;
}

