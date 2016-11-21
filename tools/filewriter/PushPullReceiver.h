//
// Created by Ulrik Kofoed Pedersen on 21/11/2016.
//

#ifndef FRAMERECEIVER_PUSHPULLRECEIVER_H
#define FRAMERECEIVER_PUSHPULLRECEIVER_H

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>
using namespace log4cxx;
using namespace log4cxx::helpers;

#include "boost/date_time/posix_time/posix_time.hpp"

#include "IFrameCallback.h"
#include "IpcReactor.h"
#include "IpcChannel.h"
#include "IpcMessage.h"

#include "zmq/zmq.hpp"

namespace filewriter
{

class PushPullReceiver {
public:
    PushPullReceiver(boost::shared_ptr<FrameReceiver::IpcReactor> reactor, const std::string& rxEndPoint, const std::string& txEndPoint);
    virtual ~PushPullReceiver();

    void registerCallback(const std::string& name, boost::shared_ptr<IFrameCallback> cb);
    void removeCallback(const std::string& name);
    void handleRxChannel();

private:
    /** Pointer to logger */
    LoggerPtr logger_;
    /** Map of IFrameCallback pointers, indexed by name */
    std::map<std::string, boost::shared_ptr<IFrameCallback> > callbacks_;
    /** IpcReactor pointer, for managing IpcMessage objects */
    boost::shared_ptr<FrameReceiver::IpcReactor> reactor_;
    /** ZMQ socket for receiving new frames */
    FrameReceiver::IpcContext& context_;
    zmq::socket_t rxSocket_;
    /** IpcChannel for sending notifications of frame release */
    FrameReceiver::IpcChannel             txChannel_;
};

} /* namespace filewriter */

#endif //FRAMERECEIVER_PUSHPULLRECEIVER_H
