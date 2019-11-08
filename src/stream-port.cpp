/* A module for low-level communication over the streaming virtio port.
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#include "stream-port.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdexcept>

#include <common/utils.h>

namespace spice {
namespace streaming_agent {

IOError::IOError(const std::string &msg, int sys_errno) :
    Error(msg + ": " + std::to_string(sys_errno) + " - " + strerror(sys_errno))
{}

InboundMessage::InboundMessage(const StreamDevHeader &header, std::unique_ptr<uint8_t[]> &&data) :
    header(header),
    data(std::move(data))
{}

template<>
StartStopMessage InboundMessage::get_payload<StartStopMessage>()
{
    StartStopMessage msg;

    // data[0] is num_codecs. No codecs in the message means to stop streaming.
    msg.start_streaming = data[0] > 0;

    const size_t max_codecs = header.size - 1;
    if (data[0] > max_codecs) {
        throw std::runtime_error("Malformed StartStop message: num_codecs (" +
                                 std::to_string(data[0]) + ") is greater than the message size (" +
                                 std::to_string(max_codecs) + ")");
    }

    for (size_t i = 1; i <= data[0]; ++i) {
        msg.client_codecs.insert((SpiceVideoCodecType) data[i]);
    }

    return msg;
}

template<>
InCapabilitiesMessage InboundMessage::get_payload<InCapabilitiesMessage>()
{
    size_t msg_len = header.size;
    if (msg_len > STREAM_MSG_CAPABILITIES_MAX_BYTES) {
        throw std::runtime_error("Received Capabilities message is too long (" +
                                 std::to_string(header.size) + " > " +
                                 std::to_string(STREAM_MSG_CAPABILITIES_MAX_BYTES) + ")");
    }

    StreamMsgCapabilities *server_caps = (StreamMsgCapabilities *) data.get();
    struct InCapabilitiesMessage msg;
    size_t caps_len = std::min(msg_len*8, (size_t) STREAM_CAP_END);

    for (size_t cap = 0; cap < caps_len; ++cap) {
        msg.capabilities.push_back((bool) test_bitmap(cap, server_caps->capabilities));
    }
    for (size_t cap = caps_len; cap < (size_t) STREAM_CAP_END; ++cap) {
        msg.capabilities.push_back(false);
    }
    return msg;
}

template<>
NotifyErrorMessage InboundMessage::get_payload<NotifyErrorMessage>()
{
    if (header.size < sizeof(StreamMsgNotifyError)) {
        throw std::runtime_error("Received NotifyError message size " + std::to_string(header.size) +
                                 " is too small (smaller than " +
                                 std::to_string(sizeof(StreamMsgNotifyError)) + ")");
    }

    size_t msg_len = header.size - sizeof(StreamMsgNotifyError);
    if (msg_len > 1024) {
        throw std::runtime_error("Received NotifyError message is too long (" +
                                 std::to_string(msg_len) + " > 1024)");
    }

    StreamMsgNotifyError *raw_message = reinterpret_cast<StreamMsgNotifyError*>(data.get());

    NotifyErrorMessage msg;
    msg.error_code = raw_message->error_code;
    strncpy(msg.message, reinterpret_cast<char*>(raw_message->msg), msg_len);
    // make sure the string is terminated
    msg.message[msg_len] = '\0';

    return msg;
}

StreamPort::StreamPort(const std::string &port_name) : fd(open(port_name.c_str(), O_RDWR | O_NONBLOCK))
{
    if (fd < 0) {
        throw IOError("Failed to open the streaming device \"" + port_name + "\"", errno);
    }
}

StreamPort::~StreamPort()
{
    close(fd);
}

InboundMessage StreamPort::receive()
{
    std::lock_guard<std::mutex> stream_guard(mutex);

    StreamDevHeader header;
    read_all(fd, &header, sizeof(header));

    if (header.protocol_version != STREAM_DEVICE_PROTOCOL) {
        throw std::runtime_error("Bad protocol version: " + std::to_string(header.protocol_version) +
                                 ", expected: " + std::to_string(STREAM_DEVICE_PROTOCOL));
    }

    if (header.size > 4 * 1024) {  // a 4kB generic limit of the message size
        throw std::runtime_error("Inbound message too big, exceeding the 4kB limit.");
    }

    std::unique_ptr<uint8_t[]> data(new uint8_t[header.size]);
    read_all(fd, data.get(), header.size);

    return InboundMessage(header, std::move(data));
}

void StreamPort::write(const void *buf, size_t len)
{
    write_all(fd, buf, len);
}

void read_all(int fd, void *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = read(fd, buf, len);

        if (n == 0) {
            throw ReadError("Reading message from device failed: read() returned 0, device is closed.");
        }

        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pollfd = {fd, POLLIN, 0};
                if (poll(&pollfd, 1, -1) < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    throw ReadError("poll failed while reading message from device", errno);
                }

                if (pollfd.revents & POLLIN) {
                    continue;
                }

                if (pollfd.revents & POLLHUP) {
                    throw ReadError("Reading message from device failed: The device is closed.");
                }

                throw ReadError("Reading message from device failed: poll returned " +
                                std::to_string(pollfd.revents));
            }
            throw ReadError("Reading message from device failed", errno);
        }

        len -= n;
        buf = (uint8_t *) buf + n;
    }
}

void write_all(int fd, const void *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);

        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pollfd = {fd, POLLOUT, 0};
                if (poll(&pollfd, 1, -1) < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    throw WriteError("poll failed while writing message to device", errno);
                }

                if (pollfd.revents & POLLOUT) {
                    continue;
                }

                if (pollfd.revents & POLLHUP) {
                    throw WriteError("Writing message to device failed: The device is closed.");
                }

                throw WriteError("Writing message to device failed: poll returned " +
                                 std::to_string(pollfd.revents));
            }
            throw WriteError("Writing message to device failed", errno);
        }

        len -= n;
        buf = (uint8_t *) buf + n;
    }
}

}} // namespace spice::streaming_agent
