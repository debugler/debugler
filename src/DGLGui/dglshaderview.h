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


#ifndef DGLSHADERVIEW_H
#define DGLSHADERVIEW_H

#include "dgltabbedview.h"
#include <QPlainTextEdit>

class DGLShaderView : public DGLTabbedView {
    Q_OBJECT

public:
    DGLShaderView(QWidget* parrent, DglController* controller);

    public slots:
        void showShader(uint, uint, uint);

private:
        virtual DGLTabbedViewItem* createTab(const ContextObjectName& id);
        virtual QString getTabName(uint id, uint target);
};

class DGLGLSLEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    DGLGLSLEditor(QWidget *parent = 0);

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

protected:
    void resizeEvent(QResizeEvent *event);

    private slots:
        void updateLineNumberAreaWidth(int newBlockCount);
        void highlightCurrentLine();
        void updateLineNumberArea(const QRect &, int);

private:
    QWidget *lineNumberArea;
};

#endif //DGLSHADERVIEW_H