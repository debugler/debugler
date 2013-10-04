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


#include "dglframebufferview.h"
#include "dglgui.h"



DGLFramebufferViewItem::DGLFramebufferViewItem(ContextObjectName name, DGLResourceManager* resManager, QWidget* parrent):DGLTabbedViewItem(name, parrent) {
    m_Ui.setupUi(this);
    m_PixelRectangleScene = new DGLPixelRectangleScene();
    m_Ui.m_PixelRectangleView->setScene(m_PixelRectangleScene);  

    m_Listener = resManager->createListener(name, DGLResource::ObjectTypeFramebuffer);
    m_Listener->setParent(this);

    CONNASSERT(connect(m_Listener,SIGNAL(update(const DGLResource&)),this,SLOT(update(const DGLResource&))));
    CONNASSERT(connect(m_Listener,SIGNAL(error(const std::string&)),this,SLOT(error(const std::string&))));
}

void DGLFramebufferViewItem::error(const std::string& message) {
    m_PixelRectangleScene->setText(message);
    m_Ui.m_PixelRectangleView->updateFormatSizeInfo(NULL);
}

void DGLFramebufferViewItem::update(const DGLResource& res) {
    const DGLResourceFramebuffer* resource = dynamic_cast<const DGLResourceFramebuffer*>(&res);
    m_PixelRectangle = resource->m_PixelRectangle;
    m_PixelRectangleScene->setPixelRectangle(*m_PixelRectangle);
    m_Ui.m_PixelRectangleView->updateFormatSizeInfo(m_PixelRectangle.get());
}

DGLFramebufferView::DGLFramebufferView(QWidget* parrent, DglController* controller):DGLTabbedView(parrent, controller) {
    setupNames("Frame Buffers", "DGLFramebufferView");

    //inbound
    CONNASSERT(connect(controller->getViewRouter(), SIGNAL(showFramebuffer(uint, uint)), this, SLOT(showFramebuffer(uint, uint))));
}

void DGLFramebufferView::showFramebuffer(uint ctx, uint bufferEnum) {
    ensureTabDisplayed(ctx, bufferEnum);
}

DGLTabbedViewItem* DGLFramebufferView::createTab(const ContextObjectName& id) {
    return new DGLFramebufferViewItem(id, m_ResourceManager, this);
}

QString DGLFramebufferView::getTabName(uint id, uint target) {
    switch (id) {
        case GL_FRONT_RIGHT:
            return "Front right buffer"; 
        case GL_BACK_RIGHT:
            return "Back right buffer";
        case GL_FRONT:
            return "Front buffer";
        case GL_BACK:
            return "Back buffer";
    }
    return QString("Frame Buffer ") + QString::number(id);
}