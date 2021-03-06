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

#include "gl-entrypoints.h"
#include "def.h"

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <cstdarg>

namespace lists {

//similar to GLParamTypeMetadata, but non-aggregate
struct ParamTypeMetadada {
    GLParamTypeMetadata::BaseType m_BaseType;
    GLEnumGroup m_EnumGroup;  
};

class GLEntrypoitParam {
public:
    const char* m_name;
    ParamTypeMetadada m_Metadata;
};

#define RETVAL(baseType, enumGroup) { GLParamTypeMetadata::BaseType::baseType, GLEnumGroup::enumGroup }
#define PARAM(name, baseType, enumGroup) {#name, {GLParamTypeMetadata::BaseType::baseType, GLEnumGroup::enumGroup}}
#define FUNC_PARAMS(...) {__VA_ARGS__}

#define FUNC_LIST_SUPPORTED_ELEM    (name, type, library, retVal, params) { #name, false, false, retVal, params }, 
#define FUNC_LIST_NOT_SUPPORTED_ELEM(name, type, library, retVal, params) { #name, false, false, retVal, params },

struct GLEntrypoint {
    const char* name;
    bool isFrameDelimiter;
    bool isDrawCall;
    ParamTypeMetadada m_RetValMetadata;
    GLEntrypoitParam params[18];
} g_Entrypoints[] = {
#include "codegen_gl_function_list.inl"
    { "<unknown>", false, false, { GLParamTypeMetadata::BaseType::Value, GLEnumGroup::NoneGroup}, {}}
};
#undef FUNC_LIST_SUPPORTED_ELEM
#undef FUNC_LIST_NOT_SUPPORTED_ELEM

#undef FUNC_PARAMS
#undef PARAM
#undef RETVAL

struct EntrypMapCache {
    EntrypMapCache() {

        for (Entrypoint e = 0; e < Entrypoints_NUM; e++) {
            entryPointNameToEnum[GetEntryPointName(e)] = e;
        }
    }

    static EntrypMapCache* get() {
        if (!s_cache.get()) {
            s_cache = std::make_shared<EntrypMapCache>();
        }
        return s_cache.get();
    }

    static std::shared_ptr<EntrypMapCache> s_cache;
    std::map<std::string, Entrypoint> entryPointNameToEnum;
};

std::shared_ptr<EntrypMapCache> EntrypMapCache::s_cache;

} //namespace lists

namespace call_sets {
Entrypoint frameDelims[] = {
        SwapBuffers_Call, wglSwapLayerBuffers_Call,
        eglSwapBuffers_Call,  glXSwapBuffers_Call};
Entrypoint drawCalls[] = {glDrawElements_Call,
                          glDrawElementsBaseVertex_Call,
                          glDrawElementsIndirect_Call,
                          glDrawElementsInstanced_Call,
                          glDrawElementsInstancedARB_Call,
                          glDrawElementsInstancedBaseInstance_Call,
                          glDrawElementsInstancedBaseVertex_Call,
                          glDrawElementsInstancedBaseVertexBaseInstance_Call,
                          glDrawElementsInstancedEXT_Call,
                          glMultiDrawElements_Call,
                          glMultiDrawElementsBaseVertex_Call,
                          glMultiDrawElementsEXT_Call,
                          glMultiDrawElementsIndirect_Call,
                          glMultiDrawElementsIndirectAMD_Call,
                          glMultiModeDrawElementsIBM_Call,
                          glDrawArrays_Call,
                          glDrawArraysEXT_Call,
                          glDrawArraysIndirect_Call,
                          glDrawArraysInstanced_Call,
                          glDrawArraysInstancedARB_Call,
                          glDrawArraysInstancedBaseInstance_Call,
                          glDrawArraysInstancedEXT_Call,
                          glMultiDrawArrays_Call,
                          glMultiDrawArraysEXT_Call,
                          glMultiDrawArraysIndirect_Call,
                          glMultiDrawArraysIndirectAMD_Call,
                          glMultiModeDrawArraysIBM_Call,
                          glClear_Call,
                          glClearBufferfi_Call,
                          glClearBufferfv_Call,
                          glClearBufferiv_Call,
                          glClearBufferuiv_Call,
                          glDrawRangeElementArrayAPPLE_Call,
                          glDrawRangeElementArrayATI_Call,
                          glDrawRangeElements_Call,
                          glDrawRangeElementsBaseVertex_Call,
                          glDrawRangeElementsEXT_Call,
                          glMultiDrawRangeElementArrayAPPLE_Call,
                          glDrawTransformFeedback_Call,
                          glDrawTransformFeedbackInstanced_Call,
                          glDrawTransformFeedbackNV_Call,
                          glDrawTransformFeedbackStream_Call,
                          glDrawTransformFeedbackStreamInstanced_Call,
                          glEnd_Call, };
}


const char* GetEntryPointName(Entrypoint entryp) {
    return lists::g_Entrypoints[entryp].name;
}

Entrypoint GetEntryPointEnum(const char* name) {
    std::map<std::string, Entrypoint>::iterator ret =
        lists::EntrypMapCache::get()->entryPointNameToEnum.find(name);
    if (ret == lists::EntrypMapCache::get()->entryPointNameToEnum.end())
        return NO_ENTRYPOINT;
    return ret->second;
}

const GLParamTypeMetadata GetEntryPointGLParamTypeMetadata(Entrypoint entryp, size_t param) {
    lists::ParamTypeMetadada& metadata = 
        lists::g_Entrypoints[entryp].params[param].m_Metadata;
    return GLParamTypeMetadata(metadata.m_BaseType, metadata.m_EnumGroup);
}

const GLParamTypeMetadata GetEntryPointRetvalMetadata(Entrypoint entryp) {
    lists::ParamTypeMetadada& metadata = 
        lists::g_Entrypoints[entryp].m_RetValMetadata;
    return GLParamTypeMetadata(metadata.m_BaseType, metadata.m_EnumGroup);
}

bool IsDrawCall(Entrypoint entryp) {

    //check if structure is initialized
    if (!lists::g_Entrypoints[call_sets::drawCalls[0]].isDrawCall) {
        //initialize
        for (size_t i = 0; i < DGL_ARRAY_LENGTH(call_sets::drawCalls); i++) {
            lists::g_Entrypoints[call_sets::drawCalls[i]].isDrawCall = true;
        }
    }

    return lists::g_Entrypoints[entryp].isDrawCall;
}

bool IsFrameDelimiter(Entrypoint entryp) {

    //check if structure is initialized
    if (!lists::g_Entrypoints[call_sets::frameDelims[0]].isFrameDelimiter) {
        //initialize
        for (size_t i = 0; i < DGL_ARRAY_LENGTH(call_sets::frameDelims); i++) {
            lists::g_Entrypoints[call_sets::frameDelims[i]].isFrameDelimiter = true;
        }
    }

    return lists::g_Entrypoints[entryp].isFrameDelimiter;
}
