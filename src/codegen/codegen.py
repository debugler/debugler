#!/usr/bin/env python
import os
import re



nonExtTypedefs = open("output/nonExtTypedefs.inl", "w")
wrappersFile = open("output/wrappers.inl", "w")
pointersFile = open("output/pointers.inl", "w")
pointersLoadFile = open("output/pointers_load.inl", "w")
functionListFile = open("output/functionList.inl", "w")
defFile = open("output/OpenGL32.def", "w")


def parse(file, genNonExtTypedefs = False):
	for line in file:
		coarseFunctionMatch = re.match("^([a-zA-Z0-9]*) (.*) (WINAPI|APIENTRY) ([a-zA-Z0-9]*) \((.*)\)(.*)$", line)
		if coarseFunctionMatch: 
			print coarseFunctionMatch.groups()
			functionRetType = coarseFunctionMatch.group(2)
			functionName = coarseFunctionMatch.group(4)
			functionAttrList = coarseFunctionMatch.group(5)
			functionAttrs = functionAttrList.split(",")
			functionAttrNames = ""
			functionNamedAttrList = "";
			
			implicitAttributeNameCount = 0
			if functionAttrList == "VOID" or functionAttrList == "void":
				functionNamedAttrList = functionAttrList
			else:
				for attribute in functionAttrs:
					attributeMatch = re.match("^[ ]*(const|CONST)?[ ]*([a-zA-Z0-9]*)[ ]*(\*?)[ ]*(\*?)[ ]*([a-zA-Z0-9]*)$", attribute)
					print attributeMatch.groups()
					attributeName = attributeMatch.group(5)
					
					if attributeName == "":
						attributeName = "unnamed" + str(implicitAttributeNameCount)
						implicitAttributeNameCount += 1
					
					if functionAttrNames != "":
						functionAttrNames = functionAttrNames + ", "
					functionAttrNames = functionAttrNames + attributeName
					
					if functionNamedAttrList != "":
						functionNamedAttrList += ", "
					
					if attributeMatch.group(1):
						functionNamedAttrList += attributeMatch.group(1) + " "
					functionNamedAttrList += attributeMatch.group(2) + " "
					if attributeMatch.group(3):
						functionNamedAttrList += attributeMatch.group(3) + " "
					if attributeMatch.group(4):
						functionNamedAttrList += attributeMatch.group(4)
					functionNamedAttrList += attributeName

				
			print >> functionListFile, functionName + "_Call,"
			print >> pointersFile, "PTR_PREFIX PFN" + functionName.upper() + " POINTER(" + functionName  + ");"
			print >> pointersLoadFile, "POINTER(" + functionName  + ") = (PFN"+ functionName.upper() + ") PTR_LOAD(" + functionName + ") ;"
			
			print >> wrappersFile, "extern \"C\" DGLWRAPPER_API " + functionRetType + " APIENTRY " + functionName + "(" + functionNamedAttrList + ") {"
			if functionRetType != "void":
				print >> wrappersFile, "    " + functionRetType + " retVal;"
			print >> wrappersFile, "    assert(POINTER(" + functionName + "));"
			print >> wrappersFile, "    printf(\"Calling " + functionName + "\\n\");"
						
			if functionRetType != "void":
				print >> wrappersFile, "    retVal = DIRECT_CALL(" + functionName + ")(" + functionAttrNames + ");"
			else:
				print >> wrappersFile, "    DIRECT_CALL(" + functionName + ")(" + functionAttrNames + ");"			
			if functionRetType != "void":
				print >> wrappersFile, "    return retVal;"
			print >> wrappersFile, "}"
			
			if genNonExtTypedefs:
				print >> nonExtTypedefs, "typedef " + functionRetType + " (APIENTRYP PFN" + functionName.upper() + ")(" + functionAttrList + ");"
			
			print >> defFile, "  " + functionName



print >> defFile, "LIBRARY opengl32.dll"
print >> defFile, "EXPORTS"

			
wglFile = open("input/wgl.h", "r").readlines()
parse(wglFile)

glFile = open("input/GL.h", "r").readlines()
parse(glFile, True)


#wglFile = open("input/temp.h", "r").readlines()
#parse(wglFile)
