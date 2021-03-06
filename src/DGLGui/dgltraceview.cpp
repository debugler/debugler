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

#include "dgltraceview.h"

#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QPainter>

#include <DGLNet/protocol/entrypoint.h>

class DGLTraceViewDelegate : public QStyledItemDelegate {
   public:
    DGLTraceViewDelegate(QObject* _parent = 0) : QStyledItemDelegate(_parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const {

        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect,
                              option.palette.color(QPalette::Highlight));
        }

        int glErrorI = index.data(Qt::UserRole + 1).toInt();
        GLenum glError = static_cast<GLenum>(glErrorI);
        QString error;
        if (glErrorI != -1) {
            error = (glError == GL_NO_ERROR)
                            ? "GL_NO_ERROR"
                            : QString::fromStdString(GetGLEnumName(glError, GLEnumGroup::ErrorCode));
        }

        QPen backup = painter->pen();

        QRect r = option.rect.adjusted(15, 0, -160, -option.rect.height() / 2);
        painter->drawText(r.left(), r.top(), r.width(), r.height(),
                          Qt::AlignTop | Qt::AlignLeft,
                          index.data(Qt::UserRole).toString());

        if (glError == GL_NO_ERROR) {
            painter->setPen(QPen(QColor("#20ff20")));
        } else {
            painter->setPen(QPen(QColor("#ff2020")));
        }

        r = QRect(option.rect.width() - 160, option.rect.y(), 150,
                  option.rect.height() / 2);
        painter->drawText(r.left(), r.top(), r.width(), r.height(),
                          Qt::AlignTop | Qt::AlignRight, error);

        painter->setPen(QPen(QColor("#2020ff")));
        r = option.rect.adjusted(15, option.rect.height() / 2, 0, 0);
        painter->drawText(r.left(), r.top(), r.width(), r.height(),
                          Qt::AlignTop | Qt::AlignLeft,
                          index.data(Qt::UserRole + 2).toString());

        painter->setPen(backup);
    }

    QSize sizeHint(const QStyleOptionViewItem& /*option*/,
                   const QModelIndex& /*index*/) const {
        return QSize(200, 40);
    }
};

DGLTraceViewList::DGLTraceViewList(QWidget* parrent) : QListWidget(parrent) {
    CONNASSERT(this, SIGNAL(resized()), parrent, SLOT(mayNeedNewElements()));
    CONNASSERT(this->verticalScrollBar(), SIGNAL(valueChanged(int)), parrent,
               SLOT(mayNeedNewElements()));
    setItemDelegate(new DGLTraceViewDelegate(this));
}

uint DGLTraceViewList::getVisibleRowCount() {
    QListWidgetItem* minimumItem = itemAt(5, 5);
    QListWidgetItem* maximumItem = itemAt(5, height() - 5);
    if (!minimumItem) {
        minimumItem = item(0);
    }
    if (!maximumItem) {
        maximumItem = item(count() - 1);
    }
    return indexFromItem(maximumItem).row() - indexFromItem(minimumItem).row() +
           1;
}

int DGLTraceViewList::getFirstVisibleElementIdx() {
    QListWidgetItem* minimumItem = itemAt(5, 5);
    if (!minimumItem) {
        minimumItem = item(0);
    }
    return indexFromItem(minimumItem).row();
}

void DGLTraceViewList::resizeEvent(QResizeEvent* /*e*/) { resized(); }

DGLTraceView::DGLTraceView(QWidget* parrent, DglController* controller)
        : QDockWidget(tr("Call trace"), parrent), m_traceList(this) {
    setObjectName("DGLTraceView");

    setEnabled(false);

    setWidget(&m_traceList);
    // inbound
    CONNASSERT(controller, SIGNAL(setConnected(bool)), this,
               SLOT(setEnabled(bool)));
    CONNASSERT(controller, SIGNAL(setRunning(bool)), this,
               SLOT(setRunning(bool)));
    CONNASSERT(controller, SIGNAL(breaked(const CalledEntryPoint&, uint)), this,
               SLOT(breaked(const CalledEntryPoint&, uint)));
    CONNASSERT(controller, SIGNAL(gotCallTraceChunkChunk(
                                   uint, const std::vector<CalledEntryPoint>&)),
               this, SLOT(gotCallTraceChunkChunk(
                             uint, const std::vector<CalledEntryPoint>&)));
    // outbound
    CONNASSERT(this, SIGNAL(queryCallTrace(uint, uint)), controller,
               SLOT(queryCallTrace(uint, uint)));
}

void DGLTraceView::setEnabled(bool enabled) {
    m_traceList.clear();
    m_Enabled = enabled;
    m_QueryUpperBound = 0;
}

void DGLTraceView::setRunning(bool running) {
    if (running) {
        m_traceList.clear();
        m_QueryUpperBound = 0;
    }
}

void DGLTraceView::mayNeedNewElements() {
    if (m_Enabled) {
        if (m_traceList.getFirstVisibleElementIdx() <
            m_traceList.count() - m_QueryUpperBound - 1) {
            // we are starving of entrypoints to display, try to query new
            // entrypoints up to this bound
            int nextUpperBound =
                    m_QueryUpperBound + 2 * m_traceList.getVisibleRowCount();
            queryCallTrace(m_QueryUpperBound, nextUpperBound);
            m_QueryUpperBound = nextUpperBound;
        }
    }
}

void DGLTraceView::breaked(const CalledEntryPoint& entryp, uint traceSize) {
    m_traceList.clear();
    m_QueryUpperBound = 0;
    for (uint i = 0; i < traceSize; i++) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setData(Qt::UserRole, "<unknown>");
        m_traceList.addItem(item);
    }
    QListWidgetItem* item = new QListWidgetItem();
    item->setData(
            Qt::UserRole,
            QString("BREAKED :  ") + QString::fromStdString(entryp.toString()));
    item->setData(Qt::UserRole + 1, -1);    // do not display GL error
    m_traceList.addItem(item);
    m_traceList.setCurrentRow(m_traceList.count() - 1);
    m_traceList.scrollToBottom();
    m_QueryUpperBound = 0;
    mayNeedNewElements();
}

void DGLTraceView::gotCallTraceChunkChunk(
        uint offset, const std::vector<CalledEntryPoint>& trace) {
    for (uint i = offset; i < offset + trace.size(); i++) {
        int row = m_traceList.count() - i - 2;
        delete m_traceList.takeItem(row);

        std::string func = trace[trace.size() - 1 - i + offset].toString();
        gl_t error = trace[trace.size() - 1 - i + offset].getError();
        std::string debugOutput =
                trace[trace.size() - 1 - i + offset].getDebugOutput();

        QListWidgetItem* item = new QListWidgetItem();
        item->setData(Qt::UserRole, func.c_str());
        item->setData(Qt::UserRole + 1, (uint)error);
        if (debugOutput.length()) {
            item->setData(Qt::UserRole + 2, debugOutput.c_str());
        }

        m_traceList.insertItem(row, item);
    }
}
