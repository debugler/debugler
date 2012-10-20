#include <QObject>
#include <QTimer>
#include <QSocketNotifier>

#include "DGLNet/client.h"
#include <boost/make_shared.hpp>

class DglController: public QObject, public dglnet::IController {
    Q_OBJECT

public:
    DglController();
    void connectClient(const std::string& host, const std::string& port);

    //IController methods:
    virtual void onSetStatus(std::string);
    virtual void onInternalError(std::string);

    //GUI interactions:
signals:
    void disconnected();
    void connected();

    //void breaked();
    //void running();

    void newStatus(const QString&);
    void error(const QString&, const QString&);
    
public slots:
    void poll();

private:
    boost::shared_ptr<dglnet::Client> m_DglClient;
    boost::shared_ptr<QSocketNotifier> m_NotifierRead, m_NotifierWrite;
    QTimer m_Timer;
};