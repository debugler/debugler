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

#include <DGLCommon/gl-headers.h>
#include <DGLCommon/gl-glue-headers.h>
#include <DGLCommon/gl-types.h>

#include "globalstate.h"
#include "api-loader.h"

#include "codegen_gl_pfn_types.inl"

// POINTER_TYPE(X) returns type of function pointer for entrypoint X. The actual
// definitions are generated from codegen output
// For entrypoints unsupported on given platform type is undefined.
#define POINTER_TYPE(X) X##_Type

// DIRECT_CALL(X) can be used to directly call entrypoint X, like
// DIRECT_CALL(glEnable)(GL_BLEND).
#define DIRECT_CALL(X) (*reinterpret_cast<POINTER_TYPE(X)>(POINTER(X)))
#define POINTER(X) g_DirectPointers[X##_Call].ptr

// DIRECT_CALL_CHECKED(X) can be used call entrypoint X with NULL-checking, like
// DIRECT_CALL_CHK(glEnable)(GL_BLEND).
// will throw exception on NULL
#define DIRECT_CALL_CHK(X) \
    (*reinterpret_cast<POINTER_TYPE(X)>(POINTER_CHECKED(X)))
#define POINTER_CHECKED(X) EarlyGlobalState::getApiLoader().ensurePointer(X##_Call)

struct LoadedPointer {
    dgl_func_ptr ptr;
    int libraryMask;
};

extern LoadedPointer g_DirectPointers[Entrypoints_NUM];
