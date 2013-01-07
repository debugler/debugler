#include "os.h"


#include <vector>
#include <Windows.h>
#include <Psapi.h>

#include "resource.h"

class OsIconImpl: public OsIcon {
public:
    OsIconImpl(void* moduleHandle) {
        m_Icon = (HICON)LoadImage((HMODULE)moduleHandle, MAKEINTRESOURCE( IDI_ICON1 ), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_LOADTRANSPARENT );
    }

    ~OsIconImpl() {
        DestroyIcon(m_Icon);
    }

    virtual void * get()  {
        return m_Icon;
    }
    static void* m_Handle;
private:
    HICON m_Icon;
};

class OsStatusPresenterImpl: public OsStatusPresenter {
public:
    OsStatusPresenterImpl(void* moduleHandle):m_Icon(moduleHandle) {
        memset(&m_niData, 0, sizeof(m_niData));
        m_niData.cbSize = sizeof(m_niData);
        m_niData.hWnd = GetDesktopWindow();
        m_niData.uID = 0xdeb091e4;
        m_niData.uTimeout = 5000; //deprecated on Vista
        m_niData.hIcon = (HICON)m_Icon.get();
        m_niData.uFlags = NIF_ICON;

        std::string process = Os::getProcessName();
        memcpy(m_niData.szInfoTitle,  process.c_str(), process.length() + 1);

        Shell_NotifyIcon(NIM_ADD, &m_niData);


    }
    virtual void setStatus(const std::string message) {
        memcpy(m_niData.szInfo,  message.c_str(), message.length() + 1);
        m_niData.uFlags |= NIF_INFO;
        Shell_NotifyIcon(NIM_MODIFY, &m_niData);
    }
    virtual ~OsStatusPresenterImpl() {
        Shell_NotifyIcon(NIM_DELETE, &m_niData);
    }
private: 
    NOTIFYICONDATA m_niData;
    OsIconImpl m_Icon;
};


std::string Os::getProcessName() {
    std::string ret = "<unknown>";

    HANDLE currentProcess =  GetCurrentProcess();
    if (currentProcess) {
        std::vector<char> buff(200);
        buff[GetModuleBaseName(currentProcess, NULL, &buff[0], 200)] = 0;
        if (buff[0]) {
            ret = &buff[0];
        }
    }
    return ret;
}

std::string Os::getEnv(const char* variable) {
    char ret[100];
    if (GetEnvironmentVariableA(variable, ret, sizeof(ret)) != 0) {
        return ret;
    }
    return "";
}


void Os::terminate() {
    TerminateProcess(GetCurrentProcess(), 0);
}

OsStatusPresenter* Os::createStatusPresenter() {
    if (!m_CurrentHandle) {
        m_CurrentHandle = GetModuleHandle(NULL);
    }
    return new OsStatusPresenterImpl(m_CurrentHandle);
}

OsIcon*  Os::createIcon() {
    if (!m_CurrentHandle) {
        m_CurrentHandle = GetModuleHandle(NULL);
    }
    return new OsIconImpl(m_CurrentHandle);    
}

void Os::setCurrentModuleHandle(void * handle) {
    m_CurrentHandle = handle;
}

void* Os::m_CurrentHandle = NULL;