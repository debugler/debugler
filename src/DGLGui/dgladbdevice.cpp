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

#include "dgladbdevice.h"

#include <memory>

#include <DGLCommon/def.h>

namespace {

class DGLEmptyOutputFilter : public DGLAdbOutputFilter {
    virtual bool filter(const std::vector<std::string>& input,
                        std::vector<std::string>&) override {

        for (size_t i = 0; i < input.size(); i++) {
            if (input[i].size()) {
                return false;
            }
        }
        return true;
    }
};

class DGLGetPropOutputFilter : public DGLAdbOutputFilter {
    virtual bool filter(const std::vector<std::string>& input,
                        std::vector<std::string>& output) override {
        if (input.size() > 2 || input.size() < 1 ||
            input[0].find("not found") != std::string::npos) {
            return false;
        }
        output.push_back(input[0]);
        return true;
    }
};

class DGLUnixSocketsFilter : public DGLAdbOutputFilter {
    virtual bool filter(const std::vector<std::string>& input,
                        std::vector<std::string>& output) override {
        if (!input.size()) return false;
        size_t pathOffset = input[0].find("Path");

        if (pathOffset == std::string::npos) {
            return false;
        }

        for (size_t i = 1; i < input.size(); i++) {
            if (input[i].size() > pathOffset) {
                std::string path = input[i].substr(pathOffset);
                size_t j = 0; 
                while (j < path.size() && path[j] != '/') j++;
                output.push_back(path.substr(j));
            }
        }
        output.push_back(input[0]);
        return true;
    }
};

class DGLTransferFilter : public DGLAdbOutputFilter {
    virtual bool filter(const std::vector<std::string>& input,
                        std::vector<std::string>& output) override {
        if (input.size() > 2 || input.size() < 1 ||
            input[0].find("bytes in") == std::string::npos) {
            return false;
        }
        output.push_back(input[0]);
        return true;
    }
};

class DGLInstallerFilter : public DGLAdbOutputFilter {
    virtual bool filter(const std::vector<std::string>& input,
                        std::vector<std::string>& output) override {
        output = input;
        if (input.size() < 2 ||
            input[input.size() - 2].find("Success") == std::string::npos) {
            return false;
        }
        return true;
    }
};

class DGLPackageListFilter : public DGLAdbOutputFilter {
    virtual bool filter(const std::vector<std::string>& input,
        std::vector<std::string>& output) override {
        
        const size_t offset = strlen("package:");

        bool frameworkNotRunning = false;

        for (size_t i = 0; i < input.size(); i++) {
            int pos = input[i].find("package:"); 
            if (pos == 0) {
                output.push_back(input[i].substr(offset));
            } else {
                if (input[i].find("Could not access the Package Manager") != std::string::npos) {
                    frameworkNotRunning = true;
                }
            }
        }

        return (output.size() > 0 || frameworkNotRunning);
    }
};
}

DGLAdbDeviceProcess::DGLAdbDeviceProcess(const std::string& pid,
                                         const std::string& name,
                                         const std::string& portName)
        : m_Pid(pid), m_Name(name), m_PortName(portName) {}

bool DGLAdbDeviceProcess::operator==(const DGLAdbDeviceProcess& rhs) const {
    return 
        rhs.getName() == m_Name && 
        rhs.getPid() == m_Pid;
    //we don't compare port strings, as they are not mandatory in ctor
}

bool DGLAdbDeviceProcess::operator<(const DGLAdbDeviceProcess& other) const {
    if (m_Name < other.getName()) {
        return true;
    } else if (m_Name == other.getName()) {
        return m_Pid < other.getPid();
    }
    return false;
}

const std::string& DGLAdbDeviceProcess::getPid() const { return m_Pid; }

const std::string& DGLAdbDeviceProcess::getName() const { return m_Name; }

const std::string& DGLAdbDeviceProcess::getPortName() const {
    return m_PortName;
}

const std::string DGLAdbDeviceProcess::getDescriptionStr() const {
    return m_Name + " (" + m_Pid + ")";
}

DGLADBDevice::DGLADBDevice(const std::string& serial)
        : m_Serial(serial),
          m_Status(InstallStatus::UNKNOWN),
          m_ABI(ABI::UNKNOWN),
          m_RequestStatus(RequestStatus::IDLE),
          m_DetailRequestStatus(DetailRequestStatus::NONE),
          m_RootSuRequired(false),
          m_RootSuParamConcat(false) {}

void DGLADBDevice::reloadProcesses() {
    if (m_RequestStatus == RequestStatus::IDLE) {
        setRequestStatus(RequestStatus::RELOAD_PROCESSES);
        setRequestStatus(DetailRequestStatus::RELOAD_GET_PORTSTR);
        getProp("debug." DGL_PRODUCT_LOWER ".socket")->process();
    }
}

void DGLADBDevice::reloadPackages() {
    if (m_RequestStatus == RequestStatus::IDLE) {
        setRequestStatus(RequestStatus::RELOAD_PACKAGES);
        setRequestStatus(DetailRequestStatus::RELOAD_GET_PORTSTR);
            getProp("debug." DGL_PRODUCT_LOWER ".socket")->process();
    }
}

const std::string& DGLADBDevice::getSerial() const { return m_Serial; }

void DGLADBDevice::queryStatus() {
    if (!checkIdle()) {
        return;
    }
    setRequestStatus(RequestStatus::QUERY_INSTALL_STATUS);

    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("ls");
    params.push_back("/system/bin/app_process.dgl");

    invokeAsShellUser(params)->process();
}

DGLAdbCookie* DGLADBDevice::getProp(const std::string& prop) {
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("getprop");
    params.push_back(prop);
    DGLAdbCookie* cookie = invokeAsShellUser(
            params, std::make_shared<DGLGetPropOutputFilter>());
    return cookie;
}

void DGLADBDevice::doneQueryInstallStatus(
        const std::vector<std::string>& prop) {

    setRequestStatus(RequestStatus::QUERY_ABI);

    if (prop.size() > 0 &&
        prop[0].find("/system/bin/app_process.dgl") != std::string::npos &&
        prop[0].find("No such file") == std::string::npos) {
        m_Status = InstallStatus::INSTALLED;
    } else {
        m_Status = InstallStatus::CLEAN;
    }

    getProp("ro.product.cpu.abi")->process();
}

void DGLADBDevice::doneQueryABI(const std::vector<std::string>& prop) {

    setRequestStatus(RequestStatus::IDLE);

    if (prop.size()) {
        if (prop[0].find("armeabi") != std::string::npos) {
            m_ABI = ABI::ARMEABI;
        } else if (prop[0].find("x86_64") != std::string::npos) {
            m_ABI = ABI::X86_64;
        } else if (prop[0].find("x86") != std::string::npos) {
            m_ABI = ABI::X86;
        } else if (prop[0].find("mips") != std::string::npos) {
            m_ABI = ABI::MIPS;
        }
    } else {
        m_ABI = ABI::UNKNOWN;
    }
    emit queryStatusSuccess(this);
}

void DGLADBDevice::portForward(const std::string& from, unsigned short to) {

    std::vector<std::string> params;
    params.push_back("forward");
    {
        std::ostringstream str;
        str << "tcp:" << to;
        params.push_back(str.str());
    }
    params.push_back("localfilesystem:" + from);

    setRequestStatus(RequestStatus::PORT_FORWARD);
    invokeAsShellUser(params, std::make_shared<DGLEmptyOutputFilter>())
        ->process();
}

void DGLADBDevice::setProcessBreakpoint(const std::string& processName) {
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("setprop");
    params.push_back("debug." DGL_PRODUCT_LOWER ".break");
    params.push_back(processName);
    
    setRequestStatus(RequestStatus::SET_BREAKPOINT);
    invokeAsShellUser(params, std::make_shared<DGLEmptyOutputFilter>())
        ->process();
}

void DGLADBDevice::unsetProcessBreakpoint() {
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("setprop");
    params.push_back("debug." DGL_PRODUCT_LOWER ".break");
    params.push_back("");

    setRequestStatus(RequestStatus::UNSET_BREAKPOINT);
    invokeAsShellUser(params, std::make_shared<DGLEmptyOutputFilter>())
        ->process();
}


DGLADBDevice::InstallStatus DGLADBDevice::getInstallStatus() {
    return m_Status;
}

DGLADBDevice::ABI DGLADBDevice::getABI() { return m_ABI; }

void DGLADBDevice::reloadProcessesGotPortString(
        const std::vector<std::string>& prop) {

    setRequestStatus(DetailRequestStatus::RELOAD_GET_UNIXSOCKETS);

    const std::string& portString = prop[0];

    if (!portString.size()) {
        return;
    }

    QString pathRegexStr = "^";
    size_t lastOffset = 0;
    int currentRegexGroup = 0;

    m_PidInSocketRegex = -1;
    m_PNameInSocketRegex = -1;

    for (size_t i = 0; i < portString.length(); i++) {
        if (portString[i] == '%' && i < (portString.length() + 1)) {
            std::string current = portString.substr(lastOffset, i - lastOffset);
            pathRegexStr += QRegExp::escape(QString::fromStdString(current));

            switch (portString[i + 1]) {
                case 'p':
                    m_PidInSocketRegex = currentRegexGroup;
                    break;
                case 'n':
                    m_PNameInSocketRegex = currentRegexGroup;
                    break;
                default:
                    DGL_ASSERT(0);
            }

            i += 2;
            pathRegexStr += "(.*)";

            currentRegexGroup++;
            lastOffset = i;
        }
    }

    if (lastOffset < portString.length()) {
        std::string last = portString.substr(lastOffset);
        pathRegexStr += QRegExp::escape(QString::fromStdString(last));
    }

    m_SocketPathRegex = QRegExp(pathRegexStr + "$");

    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("cat");
    params.push_back("/proc/net/unix");
    invokeAsShellUser(params, std::make_shared<DGLUnixSocketsFilter>())
            ->process();
}

void DGLADBDevice::reloadProcessesGotUnixSockets(
        const std::vector<std::string>& sockets) {

    std::vector<DGLAdbDeviceProcess> processes;

    for (size_t i = 0; i < sockets.size(); i++) {
        if (m_SocketPathRegex.indexIn(QString::fromStdString(sockets[i])) !=
            -1) {
            std::string pid =
                    m_SocketPathRegex.cap(m_PidInSocketRegex + 1).toStdString();
            std::string processName;
            if (m_PNameInSocketRegex >= 0) {
                processName = m_SocketPathRegex.cap(m_PNameInSocketRegex + 1)
                                      .toStdString();
            }
            processes.push_back(
                    DGLAdbDeviceProcess(pid, processName, sockets[i]));
        }
    }
    emit gotProcesses(this, processes);
}

void DGLADBDevice::installWrapper(const std::string& path) {
    m_InstallerPath = path;
    if (!checkIdle()) {
        return;
    }
    setRequestStatus(RequestStatus::PREP_INSTALL);
    waitForDevice()->process();
}

void DGLADBDevice::uninstallWrapper(const std::string& path) {
    m_InstallerPath = path;
    if (!checkIdle()) {
        return;
    }
    setRequestStatus(RequestStatus::PREP_UNINSTALL);
    waitForDevice()->process();
}

void DGLADBDevice::updateWrapper(const std::string& path) {
    m_InstallerPath = path;
    if (!checkIdle()) {
        return;
    }
    setRequestStatus(RequestStatus::PREP_UPDATE);
    waitForDevice()->process();
}

DGLAdbCookie* DGLADBDevice::waitForDevice() {
    setRequestStatus(DetailRequestStatus::PREP_ADB_WAIT);
    return DGLAdbInterface::get()->getDevices(this);
}

DGLAdbCookie* DGLADBDevice::checkUser() {
    setRequestStatus(DetailRequestStatus::PREP_ADB_CHECKUSER);
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("id");
    return invokeAsShellUser(params);
}

DGLAdbCookie* DGLADBDevice::remountFromAdb() {
    setRequestStatus(DetailRequestStatus::PREP_REMOUNT_FROM_ADB);
    std::vector<std::string> params(1, "remount");
    return invokeAsShellUser(params);
}

DGLAdbCookie* DGLADBDevice::stopFrameworks() {
    setRequestStatus(DetailRequestStatus::PREP_FRAMEWORK_STOP);
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("stop");
    return invokeAsRoot(params,
        std::make_shared<DGLEmptyOutputFilter>());
}

DGLAdbCookie* DGLADBDevice::startFramewors() {
    setRequestStatus(DetailRequestStatus::PREP_FRAMEWORK_START);
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("start");
    return invokeAsRoot(params,
        std::make_shared<DGLEmptyOutputFilter>());
}

DGLAdbCookie* DGLADBDevice::remountFromShell() {
    setRequestStatus(DetailRequestStatus::PREP_REMOUNT_FROM_SHELL);
    std::vector<std::string> params;
    params.push_back("shell");
    params.push_back("mount");
    params.push_back("-o");
    params.push_back("remount,rw");
    params.push_back("nodev");
    params.push_back("/system");
    return invokeAsRoot(params, std::make_shared<DGLEmptyOutputFilter>());
}

void DGLADBDevice::done(const std::vector<std::string>& data) {
    for (size_t i = 0; i < data.size(); i++) {
        emit log(this, "< " + data[i]);
    }
    switch (m_RequestStatus) {
        case RequestStatus::IDLE:
            emit failed(this, "Spurious done() call.");
            break;
        case RequestStatus::QUERY_INSTALL_STATUS:
            doneQueryInstallStatus(data);
            break;
        case RequestStatus::QUERY_ABI:
            doneQueryABI(data);
            break;
        case RequestStatus::RELOAD_PROCESSES:
            switch (m_DetailRequestStatus) {
                case DetailRequestStatus::RELOAD_GET_PORTSTR:
                    reloadProcessesGotPortString(data);
                    break;
                case DetailRequestStatus::RELOAD_GET_UNIXSOCKETS:
                    setRequestStatus(RequestStatus::IDLE);
                    reloadProcessesGotUnixSockets(data);
                    break;
                default:
                    DGL_ASSERT(0);
            }
            break;
        case RequestStatus::RELOAD_PACKAGES:
            switch (m_DetailRequestStatus) {
            case DetailRequestStatus::RELOAD_GET_PORTSTR:
                {
                    setRequestStatus(DetailRequestStatus::RELOAD_GET_PACKAGELIST);
                    std::vector<std::string> params;
                    params.push_back("shell");
                    params.push_back("pm");
                    params.push_back("list");
                    params.push_back("package");
                    invokeAsShellUser(params, std::make_shared<DGLPackageListFilter>())->process();
                }
                break;
            case DetailRequestStatus::RELOAD_GET_PACKAGELIST:
                setRequestStatus(RequestStatus::IDLE);
                emit gotPackages(this, data);
                break;
            default:
                DGL_ASSERT(0);
            }
            break;
        case RequestStatus::PORT_FORWARD:
            setRequestStatus(RequestStatus::IDLE);
            emit portForwardSuccess(this);
            break;
        case RequestStatus::SET_BREAKPOINT:
            setRequestStatus(RequestStatus::IDLE);
            emit setProcessBreakPointSuccess(this);
            break;
        case RequestStatus::UNSET_BREAKPOINT:
            setRequestStatus(RequestStatus::IDLE);
            emit unsetProcessBreakPointSuccess(this);
            break;
        case RequestStatus::PREP_INSTALL:
        case RequestStatus::PREP_UPDATE:
        case RequestStatus::PREP_UNINSTALL: {
            switch (m_DetailRequestStatus) {
                case DetailRequestStatus::PREP_ADB_WAIT:
                    {
                        bool foundDevice = false;
                        for (size_t i = 0; i < data.size(); i++) {
                            if (data[0].find(m_Serial) != std::string::npos) {
                                foundDevice = true;
                            }
                        }
                        if (foundDevice) {
                            checkUser()->process();
                        } else {
                            waitForDevice()->processAfterDelay(5000);
                        }
                    }
                    break;
                case DetailRequestStatus::PREP_ADB_CHECKUSER:
                    if (data[0].find("uid=0(root)") != std::string::npos) {
                        remountFromAdb()->process();
                    } else {
                        setRequestStatus(
                                DetailRequestStatus::PREP_GET_DEBUGGABLE_FLAG);
                        getProp("ro.debuggable")->process();
                    }
                    break;
                case DetailRequestStatus::PREP_GET_DEBUGGABLE_FLAG:
                    if (data[0].find("1") != std::string::npos) {
                        //restart adb as root
                        setRequestStatus(
                            DetailRequestStatus::PREP_ADB_ROOT);
                        std::vector<std::string> params;
                        params.push_back("root");
                        invokeAsShellUser(params, std::make_shared<DGLEmptyOutputFilter>())->process();
                    } else {
                        //try su
                        setRequestStatus(
                            DetailRequestStatus::PREP_ADB_CHECK_SU_USER);
                        m_RootSuRequired = true;
                        std::vector<std::string> params;
                        params.push_back("shell");
                        params.push_back("id");
                        invokeAsRoot(params)->process();
                    }
                    break;
                case DetailRequestStatus::PREP_ADB_ROOT:
                    {
                        waitForDevice()->process();
                    }
                    break;
                case DetailRequestStatus::PREP_ADB_CHECK_SU_USER:
                    if (data[0].find("uid=0(root)") != std::string::npos) {
                        setRequestStatus(
                            DetailRequestStatus::PREP_ADB_CHECK_SU_PARAM_MODE);
                        m_RootSuRequired = true;
                        std::vector<std::string> params;
                        params.push_back("shell");
                        params.push_back("ls");
                        params.push_back("/system/bin/app_process");
                        invokeAsRoot(params)->process();
                    } else {
                        failed("Cannot get root permissions. Please ensure this device"
                            " is rooted, or running any of <b>development</b>, <b>eng</b>,"
                            "  <b>debug</b> or <b>userdebug</b> AOSP builds.");
                    }
                    break;

                case DetailRequestStatus::PREP_ADB_CHECK_SU_PARAM_MODE:
                    if (data[0].find("id") != std::string::npos) {
                        m_RootSuParamConcat = !m_RootSuParamConcat;
                    }
                    remountFromAdb()->process();
                    break;
                case DetailRequestStatus::PREP_REMOUNT_FROM_ADB:
                    if (data[0].find("remount succeeded") != std::string::npos) {
                        stopFrameworks()->process();
                    } else {
                        remountFromShell()->process();
                    }
                    break;
                case DetailRequestStatus::PREP_REMOUNT_FROM_SHELL:
                    stopFrameworks()->process();
                    break;
                case DetailRequestStatus::PREP_FRAMEWORK_STOP:
                    setRequestStatus(DetailRequestStatus::
                                             PREP_FRAMEWORK_UPLOAD_INSTALLER);
                    {
                        std::vector<std::string> params;
                        params.push_back("push");
                        params.push_back(m_InstallerPath +
                                         "/dglandroidinstaller");
                        params.push_back("/data/local/tmp/");
                        invokeAsShellUser(params,
                                          std::make_shared<DGLTransferFilter>())
                                ->process();
                    }
                    break;
                case DetailRequestStatus::PREP_FRAMEWORK_UPLOAD_INSTALLER:
                    setRequestStatus(DetailRequestStatus::
                        PREP_FRAMEWORK_SYNC_FLUSH);
                    {
                        std::vector<std::string> params;
                        params.push_back("shell");
                        params.push_back("sync");
                        invokeAsRoot(params,
                            std::make_shared<DGLEmptyOutputFilter>())
                            ->process();
                    }
                    break;
                    break;
                case DetailRequestStatus::PREP_FRAMEWORK_SYNC_FLUSH:
                    setRequestStatus(DetailRequestStatus::
                                             PREP_FRAMEWORK_CHMOD_INSTALLER);
                    {
                        std::vector<std::string> params;
                        params.push_back("shell");
                        params.push_back("chmod");
                        params.push_back("777");
                        params.push_back("/data/local/tmp/dglandroidinstaller");
                        invokeAsRoot(params,
                                     std::make_shared<DGLEmptyOutputFilter>())
                                ->process();
                    }
                    break;
                case DetailRequestStatus::PREP_FRAMEWORK_CHMOD_INSTALLER:
                    setRequestStatus(
                            DetailRequestStatus::PREP_FRAMEWORK_RUN_INSTALLER);
                    {
                        std::vector<std::string> params;
                        params.push_back("shell");
                        params.push_back("/data/local/tmp/dglandroidinstaller");

                        switch (m_RequestStatus) {
                            case RequestStatus::PREP_INSTALL:
                                params.push_back("install");
                                break;
                            case RequestStatus::PREP_UPDATE:
                                params.push_back("update");
                                break;
                            case RequestStatus::PREP_UNINSTALL:
                                params.push_back("uninstall");
                                break;
                            default:
                                DGL_ASSERT(0);
                        }
                        invokeAsRoot(params,
                                     std::make_shared<DGLInstallerFilter>())
                                ->process();
                    }
                    break;
                case DetailRequestStatus::PREP_FRAMEWORK_RUN_INSTALLER:
                    startFramewors()->process();
                    break;
                case DetailRequestStatus::PREP_FRAMEWORK_START:
                    setRequestStatus(DetailRequestStatus::NONE);
                    setRequestStatus(RequestStatus::IDLE);
                    emit installerDone(this);
                    break;
                default:
                    DGL_ASSERT(0);
            }
        }
    }
}

void DGLADBDevice::failed(const std::string& reason) {
    emit log(this, "Failure: " + reason);
    switch (m_RequestStatus) {
        case RequestStatus::QUERY_INSTALL_STATUS:
        case RequestStatus::QUERY_ABI:
            setRequestStatus(RequestStatus::IDLE);
            if (reason.find("error: device unauthorized.") !=
                std::string::npos) {
                m_Status = InstallStatus::UNAUTHORIZED;
                emit queryStatusSuccess(this);
            } else {
                emit failed(this, reason);
            }
            break;
        case RequestStatus::PREP_INSTALL:
        case RequestStatus::PREP_UNINSTALL:
        case RequestStatus::PREP_UPDATE:
            if (m_DetailRequestStatus ==
                DetailRequestStatus::PREP_REMOUNT_FROM_ADB) {
                if (reason.find("Permission denied") != std::string::npos ||
                    reason.find("Operation not permitted")) {
                    // this error is acceptable, happens on production devices.
                    remountFromShell()->process();
                }
            } else {
                setRequestStatus(RequestStatus::IDLE);
                emit failed(this, reason);
            }
            break;
        default:
            setRequestStatus(RequestStatus::IDLE);
            emit failed(this, reason);
    }
}

DGLAdbCookie* DGLADBDevice::invokeAsShellUser(
        const std::vector<std::string>& params,
        std::shared_ptr<DGLAdbOutputFilter> filter) {

    std::string msg = "> adb ";
    for (size_t i = 0; i < params.size(); i++) {
        msg += params[i];
        if (i < params.size()) {
            msg += " ";
        }
    }
    emit log(this, msg);

    DGLAdbCookie* cookie = DGLAdbInterface::get()->invokeOnDevice(
            m_Serial, params, this, filter);
    return cookie;
}

DGLAdbCookie* DGLADBDevice::invokeAsRoot(
        const std::vector<std::string>& params,
        std::shared_ptr<DGLAdbOutputFilter> filter) {

    if (!m_RootSuRequired || params[0] != "shell") {
        return invokeAsShellUser(params, filter);
    } else {
        std::vector<std::string> rootParams;
        if (!m_RootSuParamConcat) {
            rootParams.resize(params.size() + 2);
            rootParams[0] = params[0];    // shell
            rootParams[1] = "su";
            rootParams[2] = "-c";
            std::copy(params.begin() + 1, params.end(), rootParams.begin() + 3);
        } else {
            rootParams.resize(4);
            rootParams[0] = params[0];    // shell
            rootParams[1] = "su";
            rootParams[2] = "-c";
            std::ostringstream concat;
            //concat << "\"";
            for (size_t i = 1; i < params.size(); i++) {
                if (i > 1) {
                    concat << " ";
                }
                concat << params[i];
            }
            //concat << "\"";
            rootParams[3] = concat.str();
        }
       
        return invokeAsShellUser(rootParams, filter);
    }
}

bool DGLADBDevice::checkIdle() {
    if (m_RequestStatus == RequestStatus::IDLE) {
        return true;
    }
    emit failed(
            "Device busy - device is currently processing some adb commands");
    return false;
}

const char* DGLADBDevice::toString(RequestStatus status) {
    switch (status) {
        case RequestStatus::IDLE:
            return "Idle";
        case RequestStatus::RELOAD_PACKAGES:
            return "Getting package list";
        case RequestStatus::RELOAD_PROCESSES:
            return "Getting process list";
        case RequestStatus::SET_BREAKPOINT:
            return "Setting breakpoint";
        case RequestStatus::UNSET_BREAKPOINT:
            return "Unsetting breakpoint";
        case RequestStatus::QUERY_ABI:
            return "Check ABI";
        case RequestStatus::QUERY_INSTALL_STATUS:
            return "Check debugger installation";
        case RequestStatus::PREP_INSTALL:
            return "Installing";
        case RequestStatus::PREP_UPDATE:
            return "Updating";
        case RequestStatus::PREP_UNINSTALL:
            return "Uninstalling";
        case RequestStatus::PORT_FORWARD:
            return "Forwarding port";
    }
    return "Unknown";
}

const char* DGLADBDevice::toString(DetailRequestStatus detailStatus) {
    switch (detailStatus) {
        case DetailRequestStatus::NONE:
            return "None";
        case DetailRequestStatus::RELOAD_GET_PORTSTR:
            return "Get unix port string";
        case DetailRequestStatus::RELOAD_GET_UNIXSOCKETS:
            return "Get unix sockets";
        case DetailRequestStatus::RELOAD_GET_PACKAGELIST:
            return "Get package list";
        case DetailRequestStatus::PREP_ADB_WAIT:
            return "Waiting for device";
        case DetailRequestStatus::PREP_ADB_CHECKUSER:
            return "Check adb user";
        case DetailRequestStatus::PREP_ADB_ROOT:
            return "Restart adb service as root";
        case DetailRequestStatus::PREP_GET_DEBUGGABLE_FLAG:
            return "Get ro.debuggable flag";
        case DetailRequestStatus::PREP_ADB_CHECK_SU_USER:
            return "Check su user";
        case DetailRequestStatus::PREP_ADB_CHECK_SU_PARAM_MODE:
            return "Check su parameter passing mode";
        case DetailRequestStatus::PREP_REMOUNT_FROM_ADB:
            return "Remounting storage via adb";
        case DetailRequestStatus::PREP_REMOUNT_FROM_SHELL:
            return "Remounting storage from shell";
        case DetailRequestStatus::PREP_FRAMEWORK_STOP:
            return "Stopping framework";
        case DetailRequestStatus::PREP_FRAMEWORK_UPLOAD_INSTALLER:
            return "Uploading installer";
        case DetailRequestStatus::PREP_FRAMEWORK_SYNC_FLUSH:
            return "Sync-ing storage";
        case DetailRequestStatus::PREP_FRAMEWORK_CHMOD_INSTALLER:
            return "Setting installer permissions";
        case DetailRequestStatus::PREP_FRAMEWORK_RUN_INSTALLER:
            return "Running installer";
        case DetailRequestStatus::PREP_FRAMEWORK_START:
            return "Starting framework";
    }
    return "Unknown";
}

void DGLADBDevice::setRequestStatus(RequestStatus newStatus) {
    if (newStatus != m_RequestStatus) {
        emit log(this, std::string("Status: [") + toString(m_RequestStatus) +
                               "] => [" + toString(newStatus) + "]");
        m_RequestStatus = newStatus;
    }
}
void DGLADBDevice::setRequestStatus(DetailRequestStatus newDetailStatus) {
    if (newDetailStatus != m_DetailRequestStatus) {
        emit log(this, std::string("Detail Status: (") +
                               toString(m_DetailRequestStatus) + ") => (" +
                               toString(newDetailStatus) + ")");
        m_DetailRequestStatus = newDetailStatus;
    }
}
