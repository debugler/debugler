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

#pragma warning(push)
#pragma warning(disable:4996) 
#include "ipc.h"
#include <boost/uuid/uuid.hpp>
#pragma warning(pop)

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp> 
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/windows_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/make_shared.hpp>
#include <sstream>

#pragma warning(disable:4503)

class DGLIPCImpl: public DGLIPC {
public:

    DGLIPCImpl():m_region(NULL), m_regionowner(true) {
        std::ostringstream uuidStream;
        uuidStream << boost::uuids::random_generator()();
        m_uuid = uuidStream.str();
        m_shmem = boost::make_shared<boost::interprocess::windows_shared_memory>(boost::interprocess::create_only, m_uuid.c_str(), boost::interprocess::read_write, sizeof(MemoryRegion));
        m_shmemregion = boost::make_shared<boost::interprocess::mapped_region>(*m_shmem, boost::interprocess::read_write);

        //inplace
        m_region = new(m_shmemregion->get_address()) MemoryRegion;
    }

    DGLIPCImpl(std::string uuid):m_uuid(uuid),m_region(NULL), m_regionowner(false) {
        m_shmem = boost::make_shared<boost::interprocess::windows_shared_memory>(boost::interprocess::open_only, m_uuid.c_str(), boost::interprocess::read_write);
        m_shmemregion = boost::make_shared<boost::interprocess::mapped_region>(*m_shmem, boost::interprocess::read_write);
        m_region = reinterpret_cast<MemoryRegion*>(m_shmemregion->get_address());
    }

    ~DGLIPCImpl() {
        if (m_regionowner && m_region) {
            m_region->~MemoryRegion();
        }
    }

    virtual const std::string& getUUID() override {
        return m_uuid;
    }
    virtual void waitForRemoteThreadSemaphore() override {
        m_region->m_remoteThreadSemaphore.wait();
    }
    virtual void postRemoteThreadSemaphore() override {
        m_region->m_remoteThreadSemaphore.post();
    }

    virtual void setDebuggerMode(DebuggerMode mode) override {
        m_region->m_debuggerMode = mode;
    }

    virtual DebuggerMode getDebuggerMode() override {
        return m_region->m_debuggerMode;
    }

    virtual void setDebuggerPort(int port) override {
        m_region->m_debuggerPort = port;
    }

    virtual int getDebuggerPort() override {
        return m_region->m_debuggerPort;
    }

private:

    struct MemoryRegion {
        MemoryRegion():m_debuggerPort(5555), m_debuggerMode(DebuggerMode::DEFAULT), m_remoteThreadSemaphore(0) {}
        int m_debuggerPort;
        DebuggerMode m_debuggerMode;
        boost::interprocess::interprocess_semaphore m_remoteThreadSemaphore;
    };
    MemoryRegion* m_region;

    std::string m_uuid;
    boost::shared_ptr<boost::interprocess::windows_shared_memory> m_shmem;
    boost::shared_ptr<boost::interprocess::mapped_region> m_shmemregion;
    bool m_regionowner;
};




boost::shared_ptr<DGLIPC> DGLIPC::Create() {
    return boost::shared_ptr<DGLIPC>(new DGLIPCImpl());
}

boost::shared_ptr<DGLIPC> DGLIPC::CreateFromUUID(std::string uuid) {
    return boost::shared_ptr<DGLIPC>(new DGLIPCImpl(uuid));
}







