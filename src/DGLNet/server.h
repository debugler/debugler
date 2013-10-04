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


#ifndef _SERVER_H
#define _SERVER_H

#include "DGLNet/transport.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>


namespace dglnet {

class Server: public Transport {
public: 
    Server(int port, MessageHandler*);
    void lock();
    void unlock();
    void accept();

private:

    boost::asio::ip::tcp::endpoint m_endpoint;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::mutex m_mutex;
};

}

#endif