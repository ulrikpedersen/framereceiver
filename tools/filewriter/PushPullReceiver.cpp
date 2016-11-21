//
// Created by Ulrik Kofoed Pedersen on 21/11/2016.
//

#include "PushPullReceiver.h"

filewriter::PushPullReceiver::PushPullReceiver(boost::shared_ptr<FrameReceiver::IpcReactor> reactor,
                                               const std::string &rxEndPoint, const std::string &txEndPoint) :
    reactor_(),
    context_(FrameReceiver::IpcContext::Instance()),
    rxSocket_(context_.get(), ZMQ_PULL),
    txChannel_(ZMQ_PUB)
{
    logger_ = Logger::getLogger("FW.PushPullReceiver");
    logger_->setLevel(Level::getAll());
    LOG4CXX_TRACE(logger_, "PushPullReceiver constructor.");

    // Connect the PUSH socket
    LOG4CXX_DEBUG(logger_, "Connecting to PUSH socket: " << rxEndPoint);
    try {
        rxSocket_.connect(rxEndPoint.c_str());
    }
    catch (zmq::error_t& e) {
        throw std::runtime_error(e.what());
    }

    // Add the PULL zmq socket to the reactor using the socket file descriptor
    int rxSocket_fd = 0;
    size_t rxSocket_fd_size = sizeof(int);
    rxSocket_.getsockopt(ZMQ_FD, static_cast<void*>(&rxSocket_fd), &rxSocket_fd_size);
    if (rxSocket_fd == 0) {
        throw std::runtime_error("No valid zmq socket file descriptor");
    } else {
        reactor_->register_socket(rxSocket_fd, boost::bind(&filewriter::PushPullReceiver::handleRxChannel, this));
    }
}

filewriter::PushPullReceiver::~PushPullReceiver() {

}

void filewriter::PushPullReceiver::registerCallback(const std::string &name,
                                                    boost::shared_ptr<filewriter::IFrameCallback> cb) {
    // Check if we own the callback already
    if (callbacks_.count(name) == 0){
        // Record the callback pointer
        callbacks_[name] = cb;
        // Confirm registration
        cb->confirmRegistration("push_pull_receiver");
    }
}

void filewriter::PushPullReceiver::removeCallback(const std::string &name) {
    boost::shared_ptr<IFrameCallback> cb;
    if (callbacks_.count(name) > 0){
        // Get the pointer
        cb = callbacks_[name];
        // Remove the callback from the map
        callbacks_.erase(name);
        // Confirm removal
        cb->confirmRemoval("push_pull_receiver");
    }
}

void filewriter::PushPullReceiver::handleRxChannel() {

}
