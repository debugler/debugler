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

#include "dglcontroller.h"

#include <DGLNet/protocol/message.h>
#include <DGLNet/protocol/request.h>
#include <DGLNet/protocol/resource.h>

DGLRequestHandler::DGLRequestHandler(DGLRequestManager* manager)
        : m_Manager(manager) {}

DGLRequestHandler::~DGLRequestHandler() { m_Manager->unregisterHandler(this); }

DGLRequestManager::DGLRequestManager(DglController* controller)
        : m_Controller(controller) {}

void DGLRequestManager::request(dglnet::DGLRequest* req,
                                DGLRequestHandler* handler) {

    dglnet::message::Request requestMessage(req);
    m_CurrentHandlers[requestMessage.getId()] = handler;
    m_Controller->sendMessage(&requestMessage);
}

void DGLRequestManager::handle(const dglnet::message::RequestReply& msg) {
    std::map<int, DGLRequestHandler*>::iterator i =
            m_CurrentHandlers.find(msg.getId());
    if (i != m_CurrentHandlers.end()) {
        std::string error;
        if (msg.isOk(error)) {
             i->second->onRequestFinished(msg.m_Reply.get());
        } else {
             i->second->onRequestFailed(error);
        }
        m_CurrentHandlers.erase(i);
    }
}

void DGLRequestManager::unregisterHandler(DGLRequestHandler* handler) {
    std::map<int, DGLRequestHandler*>::iterator i =
        m_CurrentHandlers.begin();
    while (i != m_CurrentHandlers.end()) {
        if (i->second == handler) {
            m_CurrentHandlers.erase(i++);
        } else {
            i++;
        }
    }
}

DGLResourceListener::DGLResourceListener(dglnet::ContextObjectName obName,
                                         dglnet::message::ObjectType type,
                                         DGLResourceManager* manager)
        : DGLRequestHandler(manager->getRequestManager()),
          m_ObjectType(type),
          m_ObjectName(obName),
          m_Manager(manager), 
          m_Enabled(true),
          m_Outdated(false) {}

DGLResourceListener::~DGLResourceListener() {
    m_Manager->unregisterListener(this);
}

void DGLResourceListener::onRequestFinished(
        const dglnet::message::utils::ReplyBase* msg) {
        update(*dynamic_cast<const dglnet::DGLResource*>(msg));
}

void DGLResourceListener::onRequestFailed(
    const std::string& msg) {
        error(msg);
}

void DGLResourceListener::fire() {
    if (isEnabledMarkOutDatedIfNot()) {
        m_Manager->getRequestManager()->request(
            new dglnet::request::QueryResource(m_ObjectType, m_ObjectName),
            this);
    }
}

bool DGLResourceListener::isEnabledMarkOutDatedIfNot() {
    if (!m_Enabled) {
        m_Outdated = true;
    }
    return m_Enabled;
}

void DGLResourceListener::setEnabled(bool enabled) {

    m_Enabled = enabled;
    if (m_Enabled && m_Outdated) {

        //someone enabled this listener, but it already missed some queries.
        //the view may be now outdated: immediate emit empty error & request update
        error("");
        fire();

        m_Outdated = false;
    }
}

DGLResourceManager::DGLResourceManager(DGLRequestManager* manager)
        : m_RequestManager(manager) {}

void DGLResourceManager::emitQueries() {
    for (std::list<DGLResourceListener*>::iterator i = m_Listeners.begin();
         i != m_Listeners.end(); i++) {
         if ((*i)->isEnabledMarkOutDatedIfNot()) {
            m_RequestManager->request(
                new dglnet::request::QueryResource((*i)->m_ObjectType,
                (*i)->m_ObjectName),
                *i);
        }
    }
}

DGLResourceListener* DGLResourceManager::createListener(
        dglnet::ContextObjectName name, dglnet::message::ObjectType type) {
    DGLResourceListener* listener = new DGLResourceListener(name, type, this);

    m_Listeners.insert(m_Listeners.end(), listener);
    if (listener->isEnabledMarkOutDatedIfNot()) {
        listener->fire();
    }

    return listener;
}

DGLRequestManager* DGLResourceManager::getRequestManager() {
    return m_RequestManager;
}

void DGLResourceManager::unregisterListener(DGLResourceListener* listener) {
    for (std::list<DGLResourceListener*>::iterator i = m_Listeners.begin();
         i != m_Listeners.end(); i++) {
        if (*i == listener) {
            m_Listeners.erase(i);
            break;
        }
    }
}

void DGLViewRouter::show(const dglnet::ContextObjectName& name,
                         dglnet::message::ObjectType type) {
    switch (type) {
        case dglnet::message::ObjectType::Buffer:
            emit showBuffer(name.m_Context, name.m_Name);
            break;
        case dglnet::message::ObjectType::Framebuffer:
            emit showFramebuffer(name.m_Context, name.m_Name);
            break;
        case dglnet::message::ObjectType::FBO:
            emit showFBO(name.m_Context, name.m_Name);
            break;
        case dglnet::message::ObjectType::Renderbuffer:
            emit showRenderbuffer(name.m_Context, name.m_Name);
            break;
        case dglnet::message::ObjectType::Texture:
            emit showTexture(name.m_Context, name.m_Name);
            break;
        case dglnet::message::ObjectType::Shader:
            emit showShader(name.m_Context, name.m_Name, name.m_Target);
            break;
        case dglnet::message::ObjectType::Program:
            emit showProgram(name.m_Context, name.m_Name);
            break;
        default:
            DGL_ASSERT(0);
            break;
    }
}

DglController::DglController()
        : m_Disconnected(true),
          m_Connected(false),
          m_ConfiguredAndBkpointsSet(false),
          m_BreakPointController(this),
          m_RequestManager(this),
          m_ResourceManager(getRequestManager()) {
    m_Timer.setInterval(10);
    CONNASSERT(&m_Timer, SIGNAL(timeout()), this, SLOT(poll()));
}

void DglController::connectServer(const std::string& host,
                                  const std::string& port) {
    if (m_DglClient) {
        disconnectServer();
    }

    // we are not disconnected, but not yet connected - so we do not set
    // m_Connected
    m_Disconnected = false;

    m_DglClient = dglnet::Client::Create(this, this);
    m_DglClient->connectServer(host, port);
    m_Timer.start();
}

void DglController::onSocket() {

// Hey! If you are trying to disable the timer-based polling here, please
// increment the following counter.
// You shall not succeed... on windows some socketnotifies activate()-s are
// missed, so we get stuck and wait forever for data to be ready.

// Timer disable try count: 2
// m_Timer.stop();
//
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#define QSOCK_T int
#else
#define QSOCK_T qintptr
#endif

    m_NotifierRead = std::make_shared<QSocketNotifier>(
            (QSOCK_T)m_DglClient->getSocketFD(), QSocketNotifier::Read);
    m_NotifierWrite = std::make_shared<QSocketNotifier>(
            (QSOCK_T)m_DglClient->getSocketFD(), QSocketNotifier::Write);

#undef QSOCK_T

    m_NotifierWrite->setEnabled(false);
    CONNASSERT(&*m_NotifierRead, SIGNAL(activated(int)), this, SLOT(poll()));
    CONNASSERT(&*m_NotifierWrite, SIGNAL(activated(int)), this, SLOT(poll()));
}

void DglController::onSocketStartSend() { m_NotifierWrite->setEnabled(true); }

void DglController::onSocketStopSend() { m_NotifierWrite->setEnabled(false); }

void DglController::disconnectServer() {
    if (m_DglClient) {
        m_DglClient->abort();
        m_DglClient.reset();
        m_ConfiguredAndBkpointsSet = false;
        setConnected(false);
        setDisconnected(true);
        debugeeInfo("");
    }
    m_Connected = false;
    m_NotifierRead.reset();
    m_NotifierWrite.reset();
    newStatus("Disconnected.");
}

bool DglController::isConnected() { return m_Connected; }

void DglController::poll() {
    if (m_DglClient) {
        m_DglClient->poll();

        if (m_Disconnected) {
            // one of asio handlers requested disconnection
            disconnectServer();
            connectionLost(tr("Connection error"), m_DglClientDeadInfo.c_str());
        }
    }
}

void DglController::debugContinue() {
    setBreaked(false);
    setRunning(true);
    DGL_ASSERT(isConnected());
    dglnet::message::ContinueBreak message(false);
    m_DglClient->sendMessage(&message);
}

void DglController::debugInterrupt() {
    DGL_ASSERT(isConnected());
    dglnet::message::ContinueBreak message(true);
    m_DglClient->sendMessage(&message);
    newStatus("Interrupting...");
}

void DglController::debugStep() {
    setBreaked(false);
    setRunning(true);
    DGL_ASSERT(isConnected());
    dglnet::message::ContinueBreak message(
            dglnet::message::StepMode::CALL);
    m_DglClient->sendMessage(&message);
    newStatus("Running...");
}

void DglController::debugStepDrawCall() {
    setBreaked(false);
    setRunning(true);
    DGL_ASSERT(isConnected());
    dglnet::message::ContinueBreak message(
            dglnet::message::StepMode::DRAW_CALL);
    m_DglClient->sendMessage(&message);
    newStatus("Running...");
}

void DglController::debugStepFrame() {
    setBreaked(false);
    setRunning(true);
    DGL_ASSERT(isConnected());
    dglnet::message::ContinueBreak message(
            dglnet::message::StepMode::FRAME);
    m_DglClient->sendMessage(&message);
    newStatus("Running...");
}

void DglController::debugTerminate() {
    DGL_ASSERT(isConnected());
    dglnet::message::Terminate message;
    m_DglClient->sendMessage(&message);
    newStatus("Terminating...");
}

void DglController::onSetStatus(std::string str) { newStatus(str.c_str()); }

void DglController::queryCallTrace(uint startOffset, uint endOffset) {
    dglnet::message::QueryCallTrace message(startOffset, endOffset);
    m_DglClient->sendMessage(&message);
}

void DglController::doHandleHello(const dglnet::message::Hello& msg) {
    // we are connected now
    m_Connected = true;
    debugeeInfo(msg.m_ProcessName);
    setConnected(true);
    setDisconnected(false);
}

void DglController::doHandleBreakedCall(
        const dglnet::message::BreakedCall& msg) {
    if (!m_ConfiguredAndBkpointsSet) {
        // this is the first time debugee was stopped, before any execution
        // we must upload some configuration to it
        m_ConfiguredAndBkpointsSet = true;

        sendConfig();
        getBreakPoints()->sendCurrent();
    }

    setBreaked(true);
    setRunning(false);

    breaked(msg.m_entryp, msg.m_TraceSize);
    breakedWithStateReports(msg.m_CurrentCtx, msg.m_CtxReports);

    m_ResourceManager.emitQueries();

    newStatus("Breaked execution.");
}

void DglController::doHandleCallTrace(const dglnet::message::CallTrace& msg) {
    gotCallTraceChunkChunk(msg.m_StartOffset, msg.m_Trace);
}

void DglController::doHandleRequestReply(
        const dglnet::message::RequestReply& msg) {
    getRequestManager()->handle(msg);
}

void DglController::doHandleConnect() {
    // nothing here. Do not advertise connection to GUI, until HelloMessage
    // packet arrives.
    // See DglController::doHandleHello.
}

void DglController::doHandleDisconnect(const std::string& msg) {
    m_DglClientDeadInfo = msg;
    m_Disconnected = true;
    m_Connected = !m_Disconnected;
}

void DglController::sendMessage(dglnet::Message* msg) {
    DGL_ASSERT(isConnected());
    m_DglClient->sendMessage(msg);
}

DGLBreakPointController* DglController::getBreakPoints() {
    return &m_BreakPointController;
}

DGLRequestManager* DglController::getRequestManager() {
    return &m_RequestManager;
}

DGLResourceManager* DglController::getResourceManager() {
    return &m_ResourceManager;
}

DGLViewRouter* DglController::getViewRouter() { return &m_ViewRouter; }

void DglController::sendConfig(const DGLConfiguration* config) {
    if (config) {
        m_Config = *config;
    }
    if (isConnected()) {
        dglnet::message::Configuration message(m_Config);
        m_DglClient->sendMessage(&message);
    }
}

DGLConfiguration& DglController::getConfig() { return m_Config; }

DGLBreakPointController::DGLBreakPointController(DglController* controller)
        : m_Controller(controller) {}

std::set<Entrypoint> DGLBreakPointController::getCurrent() { return m_Current; }

void DGLBreakPointController::setCurrent(
        const std::set<Entrypoint>& newCurrent) {
    if (m_Current != newCurrent) {
        m_Current = newCurrent;
        sendCurrent();
    }
}

void DGLBreakPointController::sendCurrent() {
    if (m_Controller->isConnected()) {
        dglnet::message::SetBreakPoints msg(m_Current);
        m_Controller->sendMessage(&msg);
    }
}
