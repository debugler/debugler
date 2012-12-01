#ifndef DGLTREEVIEW_H
#define DGLTREEVIEW_H

#include <QDockWidget>
#include <QTreeWidget>


#include "DGLCommon//gl-types.h"

#include "dglcontroller.h"

class DGLTreeView; 

class QClickableTreeWidgetItem: public QTreeWidgetItem {
public: 
    virtual void handleDoubleClick(DglController*);
};


class DGLTextureWidget: public QClickableTreeWidgetItem {
public:
    DGLTextureWidget();
    DGLTextureWidget(uint name);
    virtual void handleDoubleClick(DglController*);
private:
    uint m_name;
};


class DGLTreeView : public QDockWidget {
    Q_OBJECT

public:
    DGLTreeView(QWidget* parrent, DglController* controller);
    ~DGLTreeView();

    void regiSterItem(QTreeWidgetItem item);

public slots:
    void enable();
    void disable();
    void breakedWithStateReports(uint, const std::vector<dglnet::ContextReport>&);

    void onDoubleClicked(QTreeWidgetItem*, int);

private: 
    QTreeWidget m_TreeWidget;
    bool m_Enabled;   
    DglController* m_controller;
};

#endif // DGLTREEVIEW_H