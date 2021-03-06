#!/usr/bin/env python
# Copyright (C) 2014 Slawomir Cygan <slawomir.cygan@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#



import os
import re
import sys
from sets import Set
import subprocess
from lxml import etree

inputDir  = sys.argv[1] + os.sep
outputDir = sys.argv[2] + os.sep

if not os.path.exists(outputDir):
    os.makedirs(outputDir)    

entrypTypedefs    = open(outputDir + "codegen_gl_pfn_types.inl",       "w")
enumFile          = open(outputDir + "codegen_gl_enum_list.inl",       "w")
enumGroupFile     = open(outputDir + "codegen_gl_enum_group_list.inl", "w")
functionsFile     = open(outputDir + "codegen_gl_functions.inl",       "w")
functionListFile  = open(outputDir + "codegen_gl_function_list.inl",   "w") 
wrappersFile      = open(outputDir + "codegen_dgl_wrappers.inl",       "w")
exportFile        = open(outputDir + "codegen_dgl_export.inl",         "w")
exportExtFile     = open(outputDir + "codegen_dgl_export_ext.inl",     "w")
exportAndroidFile = open(outputDir + "codegen_dgl_export_android.inl", "w")

gles2onlyPat = re.compile('2\.[0-9]')
gles3onlyPat = re.compile('3\.0')
gles31onlyPat = re.compile('3\.1')
gl11and10Match = re.compile('1\.[0-1]')
gl12andLaterMatch = re.compile('1\.[2-9]|[234]\.[0-9]')

# These are "mandatory" OpenGL ES 1 extensions, to
# be LIBRARY_ES1 (not LIBRARY_ES1_EXT).
es1CoreList = [
    'GL_OES_read_format',
    'GL_OES_compressed_paletted_texture',
    'GL_OES_point_size_array',
    'GL_OES_point_sprite'
]


entrypoints = dict()
enums = dict()
enumGroups = []

class Enum:
    def __init__(self, value):
        self.value = value
        self.groups = []

class Type:
    def __init__(self, name, enumGroup):
        self.name = name
        self.enumGroup = enumGroup
        if self.enumGroup == None:
            self.enumGroup = 'NoneGroup'

class FuncParameter:
    def __init__(self, type, name):

        if name == "near" or name == "far": 
            #these are reserved keywords in C
            name = "_" + name

        self.type = type
        self.name = name

class Entrypoint:
    def __init__(self, library, skipTrace, retType, paramList):
        self.libraries = library
        self.skipTrace = skipTrace
        self.retType = retType
        self.paramList = paramList
        self.forceEnumParamNames = []
    
    def addLibrary(self, library):
        if library not in self.libraries:
            self.libraries.append(library);
        
        if "LIBRARY_GL_EXT" in self.libraries and "LIBRARY_GL" in self.libraries:
            #this happens, when entrypoint is introduced in GL 1.1, removed in 3.2 and re-introduced later
            #we don't care for this, we cannot mark is as ext - PFN...PROC defintion is still required.
            self.libraries.remove("LIBRARY_GL_EXT")
        
    def getLibaryBitMask(self): 
        if len(self.libraries) == 0:
            print "library not defined"
            exit(1)
        ret = ""
        for lib in self.libraries: 
            if len(ret) > 0:
                ret += " | "
            ret += lib.strip()
        return ret
        
    def getLibraryIfdef(self):
        if len(self.libraries) == 0:
            print "library not defined"
            exit(1)
        ret = "#if "
        first = True
        for lib in self.libraries: 
            if not first:
                ret += " || "
            first = False
            ret += "defined(HAVE_" + lib.strip() + ")"
        return ret
        
def listToString(list):
    str = "";
    for elem in list:
        if str != "":
            str += ", "
        str += elem
    return str        
        
def isPointer(type):
    pointers = [ "*", "PROC", "LPCSTR", "HGLRC", "HDC", "LPPIXELFORMATDESCRIPTOR", "LPLAYERPLANEDESCRIPTOR", "LPGLYPHMETRICSFLOAT", "GLsync" ]
    if any(pointer in type for pointer in pointers):
        return True
    return False
    

def libraryFromApiXML(api, isExtension, version = ""):
    if isExtension:
        if api == "gl" or api == "glcore":
            return "LIBRARY_GL_EXT"
        elif api == "gles1":
            return "LIBRARY_ES1_EXT"
        elif api == "gles2":
            return "LIBRARY_ES2_EXT"
        elif api == "egl":
            return "LIBRARY_EGL_EXT"
        elif api == "wgl":
            return "LIBRARY_WGL_EXT"
        elif api == "glx":
            return "LIBRARY_GLX_EXT"
        else:
            print "Unspported api: " + api
            exit(1)
    else:
        if api == "gl":
            if gl11and10Match.match(version):
                return "LIBRARY_GL"
            elif gl12andLaterMatch.match(version):
                return "LIBRARY_GL_EXT"
            else:
                print "Unspported version: " + version
                exit(1)
        elif api == "gles1":
            return "LIBRARY_ES1"
        elif api == "gles2":
            if gles2onlyPat.match(version):
                return "LIBRARY_ES2"
            elif gles3onlyPat.match(version):
                return "LIBRARY_ES3"
            elif gles31onlyPat.match(version):
                return "LIBRARY_ES2_EXT" #we treat ES31 as an ext for now (no library exports it)
            else:
                print "Unspported gles2 version: " + version
        elif api == "egl":
            return "LIBRARY_EGL"
        elif api == "wgl":
            return "LIBRARY_WGL"
        elif api == "glx":
            return "LIBRARY_GLX"
        else:
            print "Unspported api: " + api
            exit(1)
    

def getCTypeFromXML(element):
    if element.text != None:
        retType = element.text
    else:
        retType = ""
    if element.find("ptype") != None:
        retType += element.find("ptype").text
        retType += element.find("ptype").tail

    return Type(retType.strip(), element.get('group'))

    
def parseXML(path, skipTrace = False): 
    root = etree.parse(path).getroot()
    
    #get all entrypoints
    for enumsElement in root.iter("enums"):
        groupName = enumsElement.get("group")
        if groupName and not groupName in enumGroups:
            enumGroups.append(groupName)
        for enumElement in enumsElement.iter("enum"):
            name = enumElement.get("name")
            value = enumElement.get("value")
            if name == None or value == None:
                print "No entrypoint name/value"
                print etree.tostring(enumElement)
                exit(1)
            enums[name] = Enum(value)
            if groupName:
                enums[name].groups.append(groupName)
       
    #parse all groups, assign entrypoints to groups
    for groupsElement in root.iter("groups"):
        for groupElement in groupsElement.iter("group"):
            groupName = groupElement.get("name")
            if not groupName in enumGroups:
                enumGroups.append(groupName)
            for enumElement in groupElement.iter("enum"):
                enumName = enumElement.get("name")
                if enumName in enums != None:
                    enums[enumName].groups.append(groupName)
       
    #get all entrypoints (commands)
    for commandsElement in root.iter("commands"):
        for commandElement in commandsElement.iter("command"):
            entryPointName = commandElement.find("proto").find("name").text
           
            prototype = commandElement.find("proto")
            
            retType = getCTypeFromXML(prototype)

            if retType.enumGroup != 'NoneGroup' and retType.enumGroup not in enumGroups:
                enumGroups.append(retType.enumGroup)
          
            funcParams = []            
            
            for paramElement in commandElement.iter("param"):
                paramType = getCTypeFromXML(paramElement)
                if paramType.enumGroup != 'NoneGroup' and paramType.enumGroup not in enumGroups:
                    enumGroups.append(paramType.enumGroup)

                param = FuncParameter(paramType, paramElement.find("name").text)
                funcParams.append(param)
                      
            if entryPointName in entrypoints:
                print "Entrypoint already defined"
                exit(1)
            else:
                entrypoints[entryPointName] = Entrypoint([], skipTrace, retType, funcParams)

    

    #parse features to add core entryps to proper library            
    for featureElement in root.iter("feature"):
        api = featureElement.get("api")
        version = featureElement.get("number")
       
        library = libraryFromApiXML(api, False, version)
        
        for requireElement in featureElement.iter("require"):
            for commandElement in requireElement.iter("command"):
                entrypoints[commandElement.get("name")].addLibrary(library)
        
    
    #parse extensions to add extension entryps to proper library
    for extensionsElement in root.iter("extensions"):
        for extensionElement in extensionsElement.iter("extension"):
            apis = extensionElement.get("supported")
            
            for api in apis.split("|"):
                library = libraryFromApiXML(api, True)
                
                #there are three "mandatory" exts in es1, that are defined in GLES/gl.h
                forceLibEs1 = (extensionElement.get("name") in es1CoreList and api == "gles1")
                
                for requireElement in extensionElement.iter("require"):
                    if requireElement.get("api") != None and requireElement.get("api") != api:
                        #skip incompatible <require> element. will handle when doing proper api.
                        continue
                    for commandElement in requireElement.iter("command"):
                        if forceLibEs1:
                            entrypoints[commandElement.get("name")].addLibrary("LIBRARY_ES1")
                        else:
                            entrypoints[commandElement.get("name")].addLibrary(library)
       
        
outRegistryDir = os.path.abspath(outputDir)

headersToGenerate = dict()

if not os.path.exists(outRegistryDir + os.sep + "GL"):
    os.makedirs(outRegistryDir + os.sep + "GL")
headersToGenerate["GL/gl.h"] = "gl.xml"
headersToGenerate["GL/glext.h"] = "gl.xml"
headersToGenerate["GL/wgl.h"] = "wgl.xml"
headersToGenerate["GL/wglext.h"] = "wgl.xml"
headersToGenerate["GL/glx.h"] = "glx.xml"
headersToGenerate["GL/glxext.h"] = "glx.xml"

if not os.path.exists(outRegistryDir + os.sep + "GLES"):
    os.makedirs(outRegistryDir + os.sep + "GLES")
headersToGenerate["GLES/gl.h"] = "gl.xml"
headersToGenerate["GLES/glext.h"] = "gl.xml"

if not os.path.exists(outRegistryDir + os.sep + "GLES2"):
    os.makedirs(outRegistryDir + os.sep + "GLES2")
headersToGenerate["GLES2/gl2.h"] = "gl.xml"
headersToGenerate["GLES2/gl2ext.h"] = "gl.xml"

if not os.path.exists(outRegistryDir + os.sep + "GLES3"):
    os.makedirs(outRegistryDir + os.sep + "GLES3")
headersToGenerate["GLES3/gl3.h"] = "gl.xml"

if not os.path.exists(outRegistryDir + os.sep + "EGL"):
    os.makedirs(outRegistryDir + os.sep + "EGL")
headersToGenerate["EGL/egl.h"] = "egl.xml"
headersToGenerate["EGL/eglext.h"] = "egl.xml"


for name, registry in headersToGenerate.items():
    currendDir = os.getcwd();
    os.chdir(inputDir)
    p = subprocess.Popen([sys.executable, "genheaders.py", "-registry", registry, "-out", outRegistryDir, name], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, error = p.communicate()
    os.chdir(currendDir)
    print out
    print error

    if p.returncode != 0:
        print "Khronos header generation failed: " + str(p.returncode)
        exit(1)



    
parseXML(inputDir + "gl.xml")
parseXML(inputDir + "egl.xml")
parseXML(inputDir + "wgl.xml")
parseXML(inputDir + "glx.xml")


#----- WORKAROUNDS START -------

#System: ALL
#API: GL, GLES
#
# Change <internalFormat> of all *TexImage* calls to GLenum, as GLint cannot be nicely printed.
    #WA for <internalFormat> in glTexImage, glTextureImage
    # - treats internalFormat as GLenum instead of GLint, so it can be nicely displayed.
for name, entrypoint in sorted(entrypoints.items()):
    if name.startswith("glTexImage") or name.startswith("glTextureImage"):
        if "MultisampleCoverageNV" in name:
            internalFormatIdx = 3;
        else:
            internalFormatIdx = 2;        
        if name.startswith("glTextureImage"):
            internalFormatIdx += 1
        if entrypoint.paramList[internalFormatIdx].name.lower() == "internalformat":
            if "GLenum" in entrypoint.paramList[internalFormatIdx].type.name:
                pass#this is ok
            elif "GLint" in entrypoint.paramList[internalFormatIdx].type.name:
                #replace
                entrypoints[name].forceEnumParamNames.append(entrypoint.paramList[internalFormatIdx].name)
            else:
                raise Exception(name + '\'s 3rd parameter assumed GLuint or GLenum, got ' + entrypoint.paramDeclList[internalFormatIdx] + ', bailing out')
        else:
            raise Exception(name + '\'s 3rd parameter assumed internalFormat, got ' + entrypoint.paramDeclList[internalFormatIdx] + ', bailing out')


#System: Windows
#
#This function exist in khronox xml registry, but is not associated with any feature:
if len(entrypoints["wglGetDefaultProcAddress"].libraries) > 0:
    print "Fix me - remove WA"
    exit(1)
entrypoints["wglGetDefaultProcAddress"].addLibrary("LIBRARY_WGL")


#System: Linux/X11
#
#This function is an EXT, however the only way to load it is dlsym()
#So it is placed also in core GLX
entrypoints['glXGetProcAddressARB'].addLibrary("LIBRARY_GLX")

#System: Windows
#API: gdi32.dll
#Registry enlists these functions as WGL, however they present only in gdi32.dll
entrypoints["GetPixelFormat"].libraries =            ["LIBRARY_WINGDI"];
entrypoints["ChoosePixelFormat"].libraries =         ["LIBRARY_WINGDI"];
entrypoints["DescribePixelFormat"].libraries =       ["LIBRARY_WINGDI"];
entrypoints["GetEnhMetaFilePixelFormat"].libraries = ["LIBRARY_WINGDI"];
entrypoints["GetPixelFormat"].libraries =            ["LIBRARY_WINGDI"];
entrypoints["SetPixelFormat"].libraries =            ["LIBRARY_WINGDI"];
entrypoints["SwapBuffers"].libraries =               ["LIBRARY_WINGDI"];


#System: Android
#API: GLES1, undocumented *Bounds entrypoins
#These undocumented symbols exported by libGLESv1.so, called sometimes by JNI
parseXML(inputDir + ".." + os.sep + "gl-android.xml", True)

#System: Android
#API: GLES1
#Some extensions are exported in libGLESv1.so
androidGLES1Exports = open( inputDir + ".." + os.sep + "android-gles1ext.exports", "r" )
for entryp in androidGLES1Exports:
    if "LIBRARY_ES1" not in entrypoints[entryp.strip()].libraries:
        entrypoints[entryp.strip()].addLibrary("LIBRARY_ES1_ANDROID")

#System: Android
#API: GLES2
#Some extensions are exported in libGLESv2.so
androidGLES2Exports = open( inputDir + ".." + os.sep + "android-gles2ext.exports", "r" )
for entryp in androidGLES2Exports:
    if "LIBRARY_ES2" not in entrypoints[entryp.strip()].libraries:
        entrypoints[entryp.strip()].addLibrary("LIBRARY_ES2_ANDROID")


#System: Linux/X11
#API: GLX EXT
#These rare functions require some external headers. 
blacklist = ["glXAssociateDMPbufferSGIX", "glXCreateGLXVideoSourceSGIX", "glXDestroyGLXVideoSourceSGIX" ]


#----- WORKAROUNDS END -------


#writeout files:

for name, enum in sorted(enums.items()):
    if not "_LINE_BIT" in name:  #TODO: what about _LINE_BIT stuff?
        if len(enum.groups) <= 0:
            enum.groups.append('NoneGroup')
        print >> enumFile, "ENUM_LIST_ELEMENT(" + name + ","  + enum.value + ", " + listToString(["GLEnumGroup::" + g for g in enum.groups]) + ")"

for name in enumGroups:
    print >> enumGroupFile, "ENUM_GROUP_LIST_ELEMENT(" + name + ")"
        
for name, entrypoint in sorted(entrypoints.items()):
    if name in blacklist:
        continue;

    paramDeclList = [ param.type.name + " " + param.name for param in entrypoint.paramList]
    paramCallList = [ param.name for param in entrypoint.paramList]
    
    retValBaseType = "Value"
    if ("GLenum" in entrypoint.retType.name and not "*" in entrypoint.retType.name):
        retValBaseType = "Enum"
    elif "GLbitfield" in entrypoint.retType.name:
        paramBaseType = "Bitfield"
    retValStr = "RETVAL(" + retValBaseType + ", " + entrypoint.retType.enumGroup +  ")"
    
    outParamList = []
    for param in entrypoint.paramList:
        paramBaseType = "Value"
        if ("GLenum" in param.type.name and not "*" in param.type.name) or param.name in entrypoint.forceEnumParamNames:
            paramBaseType = "Enum"
        elif "GLbitfield" in param.type.name:
            paramBaseType = "Bitfield"
        outParamList.append( "PARAM(" + param.name  + "," + paramBaseType + "," + param.type.enumGroup +")")
    paramsStr = "FUNC_PARAMS(" + listToString(outParamList) + ")"

#list of entrypointsg1
    entrypointPtrType = name + "_Type"
    print >> functionListFile, entrypoint.getLibraryIfdef()
    print >> functionListFile, "    FUNC_LIST_SUPPORTED_ELEM(" + name + ", " + entrypointPtrType + ", " + entrypoint.getLibaryBitMask() + ", " + retValStr + ", " + paramsStr + ")"
    print >> functionListFile,"#else"
    print >> functionListFile, "    FUNC_LIST_NOT_SUPPORTED_ELEM(" + name + ", " + entrypointPtrType + ", LIBRARY_NONE, " + retValStr + ", " +  paramsStr +  ")"
    print >> functionListFile,"#endif"
    
    print >> functionsFile, name + "_Call,"
    
#entrypoint export
    coreLib = False
    for coreLib1 in entrypoint.libraries:
        for coreLib2 in ["LIBRARY_WGL", "LIBRARY_GLX", "LIBRARY_EGL", "LIBRARY_GL", "LIBRARY_ES1", "LIBRARY_ES2", "LIBRARY_ES3" ]:
            if coreLib1.strip() == coreLib2.strip():
                coreLib = True
                
    if coreLib:
        print >> exportFile, entrypoint.getLibraryIfdef()
        print >> exportFile, "DGLWRAPPER_API " + entrypoint.retType.name + " APIENTRY " + name + "(" + listToString(paramDeclList) + ") {"
        print >> exportFile, "        return dgl_wrappers::" + name + "(" + listToString(paramCallList) + ");"        
        print >> exportFile, "}"
        print >> exportFile, "#endif"
    else:
        print >> exportExtFile, entrypoint.getLibraryIfdef()
        print >> exportExtFile, "DGLWRAPPER_API " + entrypoint.retType.name + " APIENTRY " + name + "(" + listToString(paramDeclList) + ") {"
        print >> exportExtFile, "        return dgl_wrappers::" + name + "(" + listToString(paramCallList) + ");"        
        print >> exportExtFile, "}"
        print >> exportExtFile, "#endif"
        
    androidLib = False
    for androidLib1 in entrypoint.libraries:
        for androidLib2 in ["LIBRARY_ES1_ANDROID", "LIBRARY_ES2_ANDROID" ]:
            if androidLib1.strip() == androidLib2.strip():
                androidLib = True
    if androidLib:
        print >> exportAndroidFile, entrypoint.getLibraryIfdef()
        print >> exportAndroidFile, "DGLWRAPPER_API " + entrypoint.retType.name + " APIENTRY " + name + "(" + listToString(paramDeclList) + ") {"
        print >> exportAndroidFile, "        return dgl_wrappers::" + name + "(" + listToString(paramCallList) + ");"        
        print >> exportAndroidFile, "}"
        print >> exportAndroidFile, "#endif"
        
#entrypoint wrappers

    print >> wrappersFile, entrypoint.getLibraryIfdef()
    print >> wrappersFile, entrypoint.retType.name + " APIENTRY " + name + "(" + listToString(paramDeclList) + ") {"
    
    if not entrypoint.skipTrace:
        cookie = "    DGLWrapperCookie cookie( " + name + "_Call"
        i = 0
        while i < len(entrypoint.paramList):
            cookie = cookie + ", "
            cookie = cookie + entrypoint.paramList[i].name
            i+=1
                
        print >> wrappersFile, cookie + " );"
    
        print >> wrappersFile, "    if (!cookie.retVal.isSet()) {"
        print >> wrappersFile, "        DGL_ASSERT(POINTER(" + name + "));"
        if entrypoint.retType.name.lower() != "void":
            print >> wrappersFile, "        cookie.retVal = DIRECT_CALL(" + name + ")(" + listToString(paramCallList) + ");"
        else:
            print >> wrappersFile, "        DIRECT_CALL(" + name + ")(" + listToString(paramCallList) + ");"            
        print >> wrappersFile, "    }"
        
        if entrypoint.retType.name.lower() != "void":
            print >> wrappersFile, "    " + entrypoint.retType.name + " tmp;  cookie.retVal.get(tmp); return tmp;"
    else:
        print >> wrappersFile, "    //this function is not traced"
        if entrypoint.retType.name.lower() != "void":
            print >> wrappersFile, "    return DIRECT_CALL(" + name + ")(" + listToString(paramCallList) + ");"
        else:
            print >> wrappersFile, "    DIRECT_CALL(" + name + ")(" + listToString(paramCallList) + ");"

    print >> wrappersFile, "}"
    print >> wrappersFile, "#endif"
    
    
#additional PFN definitions
    print >> entrypTypedefs, entrypoint.getLibraryIfdef()
    print >> entrypTypedefs, "typedef " + entrypoint.retType.name + " (APIENTRYP " + entrypointPtrType + ")(" + listToString(paramDeclList) + ");"
    print >> entrypTypedefs, "#else"
    print >> entrypTypedefs, "typedef void * " +  entrypointPtrType + ";"
    print >> entrypTypedefs, "#endif"

