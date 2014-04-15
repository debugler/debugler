#!/usr/bin/env python
# Copyright (C) 2014 Slawomir Cygan <slawomir.cygan@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the 'License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an 'AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


import os
import sys
import subprocess
from optparse import OptionParser
import logging

class BaseTarget(object):
    def __init__(self):
        self.deps = []
        pass

    def depend(self, target):
        self.deps.append(target)

    def getBuildTypeStr(self, debug):
        if debug:
            return 'Debug'
        else:
            return 'Release'

    def prepare(self, targetName, debug = False):
        pass

    def build(self):
        pass

class CMakeTarget(BaseTarget):
    def __init__(self):
        super(CMakeTarget, self).__init__()


    def prepare(self, targetName, debug, cmakeOpts, toolchain = ''):

        self.path = 'build' + os.sep + targetName
        
        if not os.path.exists(self.path):
           os.makedirs(self.path)

        cmdLine = cmakeOpts

        if len(toolchain):
            cmdLine += ['-DCMAKE_TOOLCHAIN_FILE=' + toolchain]

        cmdLine += ['-DCMAKE_BUILD_TYPE=' + self.getBuildTypeStr(debug)]
        cmdLine += ['-G', 'Eclipse CDT4 - Unix Makefiles']
        cmdLine += ['..' + os.sep + '..']
        path = os.getcwd() + os.sep + self.path

        logging.debug('Running CMAKE in ' + path + ' with args: ' + str(cmdLine) + '...')
        return subprocess.call(['cmake'] + cmdLine, cwd=path)


    def build(self, target = 'all'):
        logging.debug('Running make for ' + self.path + '...')
        return subprocess.call(['make', '-j', '-C', self.path, target])
        

class AndroidBuildTarget(CMakeTarget):
    def __init__(self, arch):
        super(AndroidBuildTarget, self).__init__()
        self.arch = arch

    def prepare(self, targetName, debug = False):
        return super(AndroidBuildTarget, self).prepare(targetName, debug, ['-DANDROID_ABI=' + self.arch],  '../../tools/build/cmake/toolchains/android.toolchain.cmake')

    def build(self):
        return super(AndroidBuildTarget, self).build()


class LinuxBuildTarget(CMakeTarget):
    def __init__(self, arch):
        super(LinuxBuildTarget, self).__init__()
        self.arch = arch

    def prepare(self, targetName, debug = False):
        return super(LinuxBuildTarget, self).prepare(targetName, debug, ['-DARCH=' + self.arch])

    def build(self):
        return super(LinuxBuildTarget, self).build('package')


class WindowsBuildTarget(BaseTarget):
    def __init__(self, platform, configSuffix):
        super(WindowsBuildTarget, self).__init__()
        self.platform = platform
        self.configSuffix = configSuffix

    def prepare(self, targetName, debug = False):
        self.config = self.getBuildTypeStr(debug) + self.configSuffix
        return 0

    def build(self):
        args = ['debugler.sln', '/p:VisualStudioVersion=11.0', '/m', '/nologo', '/t:Build',  '/p:Configuration=' + self.config + ';platform=' + self.platform]
        logging.debug('Running MSBUILD with args ' + str(args) + '...')
        return subprocess.call([os.getenv('WINDIR') + os.sep + 'Microsoft.NET' + os.sep + 'Framework' + os.sep + 'v4.0.30319' + os.sep + 'MSBuild.exe'] + args)



logging.basicConfig(level=logging.INFO)

buildTargets = {}
#Android build targets
buildTargets['android-arm']    = AndroidBuildTarget('armeabi')
buildTargets['android-armv7a'] = AndroidBuildTarget('armeabi-v7a') #not really used
buildTargets['android-x86']    = AndroidBuildTarget('x86')
buildTargets['android-mips']   = AndroidBuildTarget('mips')

buildTargets['android']        = BaseTarget()
buildTargets['android'].depend('android-arm')
buildTargets['android'].depend('android-x86')
buildTargets['android'].depend('android-mips')


if not sys.platform.startswith('win'):
    buildTargets['32']             = LinuxBuildTarget('32')
    buildTargets['64']             = LinuxBuildTarget('64')

    buildTargets['32-dist']         = BaseTarget()
    buildTargets['32-dist'].depend('32')
    buildTargets['32-dist'].depend('android')

    buildTargets['64-dist']         = BaseTarget()
    buildTargets['64-dist'].depend('64')
    buildTargets['64-dist'].depend('android')

else:
    buildTargets['32-dist']         = WindowsBuildTarget('Win32', '-ALL')
    buildTargets['64-dist']         = WindowsBuildTarget('x64',   '-ALL')
    buildTargets['32']              = WindowsBuildTarget('Win32', '')
    buildTargets['64']              = WindowsBuildTarget('x64',   '')



usage = 'usage: %prog [options] target'
parser = OptionParser(usage=usage)
parser.set_defaults(build_debug=False)
parser.add_option('-l', '--listTargets', dest='list_targets', action='store_true', help='List avaliable targets')
parser.add_option('-d', '--debug', dest='build_debug', action='store_true', help='Debug build')
parser.add_option('-r', '--release', dest='build_debug', action='store_false', help='Release build')


(options, args) = parser.parse_args()


if options.list_targets:
    for k in buildTargets.keys():
        print k
    exit(0)



if len(args) != 1: 
    parser.error('Please supply target to build')



def Build(targetName):
    ret = 0
    if targetName not in buildTargets.keys():
        parser.error('Unrecognized target: ' + targetName)
    else:
        target = buildTargets[targetName]

    for dep in target.deps:
        ret = Build(dep)
        if ret != 0:
            return ret

    logging.info('Preparing target ' + targetName)
    ret = target.prepare(targetName, options.build_debug)

    if ret != 0:
        logging.critical('Builld prepare failed for target ' + targetName + '. Error code = ' + str(ret) + '.')
        return ret

    ret = target.build()
    if ret != 0:
        logging.critical('Builld failed for target ' + targetName + '. Error code = ' + str(ret) + '.')

    return ret



exit(Build(args[0]))


'''

ARCH=$1
BUILDTYPE=$2

if [ 'x$ARCH' == 'x' ]; then 
    ARCH=32
fi

if [ 'x$BUILDTYPE' == 'x' ]; then 
    BUILDTYPE=Release
fi

case '$ARCH' in
'32')
    TARGET=package
    ;;
'64')
    TARGET=package
    ;;
'android-arm')
    CMAKEFLAGS='-DCMAKE_TOOLCHAIN_FILE=../../../tools/cmake/toolchains/android.toolchain.cmake -DANDROID_ABI=armeabi'
    ;;
'android-armv7a')
    CMAKEFLAGS='-DCMAKE_TOOLCHAIN_FILE=../../../tools/cmake/toolchains/android.toolchain.cmake -DANDROID_ABI=armeabi-v7a'
    ;;
'android-mips')
    CMAKEFLAGS='-DCMAKE_TOOLCHAIN_FILE=../../../tools/cmake/toolchains/android.toolchain.cmake -DANDROID_ABI=mips'
    ;;
'android-x86')
    CMAKEFLAGS='-DCMAKE_TOOLCHAIN_FILE=../../../tools/cmake/toolchains/android.toolchain.cmake -DANDROID_ABI=x86'
    ;;
 
*)
    echo 'Platform $ARCH unsupported.'
    exit 1
    ;;
esac


CWD=`pwd`
mkdir -p build/$ARCH/$BUILDTYPE && cd build/$ARCH/$BUILDTYPE && cmake -DARCH=$ARCH $CMAKEFLAGS -DCMAKE_BUILD_TYPE=$BUILDTYPE -G 'Eclipse CDT4 - Unix Makefiles' ../../..

if [ '$?' != '0' ]; then 
    echo CMAKE failed.
    exit 1
fi

cd $CWD
make -C build/$ARCH/$BUILDTYPE  $TARGET

'''