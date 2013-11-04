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
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <DGLNet/protocol/message.h>
#include <boost/asio/basic_streambuf_fwd.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
    namespace asio {
        typedef basic_streambuf<> streambuf;
    }
}

namespace dglnet {

class  TransportHeader;
class TransportDetail;

class Transport: public std::enable_shared_from_this<Transport> {
public: 
    Transport(MessageHandler* messageHandler);
    virtual ~Transport();
    void sendMessage(const Message* msg);
    void poll();
    void run_one();
    void abort();
protected:
    void read();
    void notifyConnect();
    void notifyDisconnect(const boost::system::error_code &ec);
    virtual void notifyStartSend();
    virtual void notifyEndSend();

    std::shared_ptr<Transport> get_shared_from_base() {
        return shared_from_this();
    }

    std::shared_ptr<TransportDetail> m_detail;

private:
    void writeQueue();

    void onReadHeader(TransportHeader* header, const boost::system::error_code &ec);
    void onReadArchive(boost::asio::streambuf* stream, const boost::system::error_code &ec);
    void onWrite(std::vector<std::pair<TransportHeader*, boost::asio::streambuf*> >, const boost::system::error_code &ec);
    
    
    void onMessage(const Message& msg);   

    MessageHandler* m_messageHandler;

    std::vector<std::pair<TransportHeader*, boost::asio::streambuf*> > m_WriteQueue;
    bool m_WriteReady;
    bool m_Abort;

};

}

#endif


