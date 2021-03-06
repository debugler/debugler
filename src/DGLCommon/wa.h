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

#ifndef WA_H
#define WA_H


#define DGL_HAVE_WA(WA) __wa_##WA

// WA list


// Buggy ARM Mali OpenGL ES 3.0 emulator: really much data is returned from
// glGetIntegerv
// Fix: use larger buffer
#define __wa_ARM_MALI_EMU_GETTERS_OVERFLOW 1


// Buggy ARM Mali OpenGL ES 3.0 emulator: EGL_CONFIG_ID queried using
// eglQuerySurface does not match any eglConfig
#define __wa_ARM_MALI_EMU_EGL_QUERY_SURFACE_CONFIG_ID 1


// Buggy ARM Mali OpenGL ES emulator on Windows:
// do not exit remote loader thread before app tear down
//  Normally we would just return from (empty) function invoked by CreateRemoteThread
//  causing loader thread to exit (leaving app in suspended state - no user threads).
//  This would also cause DLL_THREAD_DETACH on all recently loaded DLLs.
// Unfortunately this causes CreateWindowEx() to fail later in eglInitialize(),
//  probably because RegisterClass was called in this thread (sic!) from  DLLMain.
// Fix: lock this thread until application finishes
#define __wa_ARM_MALI_EMU_LOADERTHREAD_KEEP _WIN32


// Querying GL state is not implemented propely on ES. May be related to
// WA_ARM_MALI_EMU_GETTERS_OVERFLOW
// Fix: disable query on ES
#define __wa_ES_QUERY_STATE 1

// On Android < 4.2-r1 global intializers & constructors of LD_PRELOAD-ed
//  libraries are not called.
// It is now fixed:
// https://github.com/android/platform_bionic/commit/326e85eca6916eb904649f7bff65244a40088ba7
//.. but we have a WA for older versions (manually call DT_INIT and
// DT_INIT_ARRAY).
#define __wa_ANDROID_SO_CONSTRUCTORS defined(__ANDROID__)


// Some Android devices support 'big' OpenGL, but do not ship with libGL.so
// For example NVIDIA Shield Tablet & other Tegra K1 Android devices.
// The fix causes to load libGL.so entrypoints via eglGetProcAddress()
#define __wa_ANDROID_MISSING_LIBGL defined(__ANDROID__)


#endif    // WA_H
