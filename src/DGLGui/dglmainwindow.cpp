#include <QMessageBox>

#include "dglmainwindow.h"
#include "dglconnectdialog.h"
#include "dgltraceview.h"
#include "dgltreeview.h"
#include "dgltextureview.h"
#include "dglgui.h"

DGLMainWindow::DGLMainWindow(QWidget *parent, Qt::WFlags flags)
    : QMainWindow(parent, flags) {
    m_ui.setupUi(this);
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    createDockWindows();
    createInteractions();
}

DGLMainWindow::~DGLMainWindow() {

}


void DGLMainWindow::createDockWindows() {
    {
        QDockWidget *dock = new DGLTraceView(this, &m_controller);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        viewMenu->addAction(dock->toggleViewAction());
    } {
        QDockWidget *dock = new DGLTreeView(this, &m_controller);
        addDockWidget(Qt::LeftDockWidgetArea, dock);
        viewMenu->addAction(dock->toggleViewAction());
    } {
        QDockWidget *dock = new DGLTextureView(this, &m_controller);
        addDockWidget(Qt::TopDockWidgetArea, dock);
        viewMenu->addAction(dock->toggleViewAction());
    }

     /*connect(customerList, SIGNAL(currentTextChanged(QString)),
             this, SLOT(insertCustomer(QString)));
     connect(paragraphsList, SIGNAL(currentTextChanged(QString)),
             this, SLOT(addParagraph(QString)));*/
 }

void DGLMainWindow::createMenus() {
     fileMenu = menuBar()->addMenu(tr("&File"));
     fileMenu->addAction(attachAct);
     fileMenu->addAction(disconnectAct);
     fileMenu->addSeparator();
     fileMenu->addAction(quitAct);

     //editMenu = menuBar()->addMenu(tr("&Edit"));
     //editMenu->addAction(undoAct);*/

     debugMenu = menuBar()->addMenu(tr("&Debug"));
     debugMenu->addAction(debugContinueAct);
     debugMenu->addAction(debugInterruptAct);
     debugMenu->addAction(debugStepAct);


     viewMenu = menuBar()->addMenu(tr("&View"));

     menuBar()->addSeparator();

     helpMenu = menuBar()->addMenu(tr("&Help"));
     helpMenu->addAction(aboutAct);
     
 }


void DGLMainWindow::createToolBars() {
     /*fileToolBar = addToolBar(tr("File"));
     fileToolBar->addAction(newLetterAct);
     fileToolBar->addAction(saveAct);
     fileToolBar->addAction(printAct);

     editToolBar = addToolBar(tr("Edit"));
     editToolBar->addAction(undoAct);*/
 }

 void DGLMainWindow::createStatusBar() {
     statusBar()->showMessage(tr("Ready"));
 }

 void DGLMainWindow::createActions() {
     quitAct = new QAction(tr("&Quit"), this);
     quitAct->setShortcuts(QKeySequence::Quit);
     quitAct->setStatusTip(tr("Quit the application"));
     CONNASSERT(connect(quitAct, SIGNAL(triggered()), this, SLOT(close())));

     aboutAct = new QAction(tr("&About"), this);
     aboutAct->setStatusTip(tr("Show the application's About box"));
     CONNASSERT(connect(aboutAct, SIGNAL(triggered()), this, SLOT(about())));

     attachAct = new QAction(tr("&Attach to"), this);
     attachAct->setStatusTip(tr("Attach to IP target"));
     CONNASSERT(connect(attachAct, SIGNAL(triggered()), this, SLOT(attach())));

     disconnectAct = new QAction(tr("&Disconnect"), this);
     disconnectAct->setStatusTip(tr("Disconnect an terminate application"));
     CONNASSERT(connect(disconnectAct, SIGNAL(triggered()), this, SLOT(disconnect())));

     debugContinueAct = new QAction(tr("&Continue"), this);
     debugContinueAct->setStatusTip(tr("Continue program execution"));
     CONNASSERT(connect(debugContinueAct, SIGNAL(triggered()), &m_controller, SLOT(debugContinue())));

     debugInterruptAct = new QAction(tr("&Interrupt (on GL)"), this);
     debugInterruptAct->setStatusTip(tr("Interrupt program execution on GL call"));
     CONNASSERT(connect(debugInterruptAct, SIGNAL(triggered()), &m_controller, SLOT(debugInterrupt())));

     debugStepAct = new QAction(tr("&Step call"), this);
     debugStepAct->setStatusTip(tr("Step one GL call"));
     CONNASSERT(connect(debugStepAct, SIGNAL(triggered()), &m_controller, SLOT(debugStep())));

 }

  void DGLMainWindow::createInteractions() {
      CONNASSERT(connect(&m_controller, SIGNAL(newStatus(const QString&)), m_ui.statusBar, SLOT(showMessage(const QString&))));
      CONNASSERT(connect(&m_controller, SIGNAL(error(const QString&, const QString&)), this, SLOT(errorMessage(const QString&, const QString&))));
  }

  void DGLMainWindow::about() {
    QMessageBox::about(this, tr("About Debuggler"),
             tr("The <b>Debuggler</b>, OpenGL debugger<br><br> Slawomir Cygan: Eng. thesis,"
                "Gdansk University of Technology<br>"
                "Faculty of Electronics, Telecommunications and Informatics<br>"
                "Department of Computer Architecture, 2012."));
 }

  void DGLMainWindow::attach() {
      DGLConnectDialog dialog;
      if (dialog.exec() == QDialog::Accepted) {
          m_controller.connectServer(dialog.getAddress(), dialog.getPort());
      }
  }

  void DGLMainWindow::disconnect() {
      m_controller.disconnectServer();
  }


void DGLMainWindow::errorMessage(const QString& title, const QString& msg) {
    QMessageBox::critical(this, title, msg);
}