/* A module for low-level communication over the streaming virtio port.
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include <spice/stream-device.h>
#include <spice/enums.h>

#include <spice-streaming-agent/error.hpp>

#include <cstddef>
#include <string>
#include <memory>
#include <mutex>
#include <set>


namespace spice {
namespace streaming_agent {

class IOError : public Error
{
public:
    using Error::Error;

    IOError(const std::string &msg, int sys_errno);
};

class ReadError : public IOError
{
public:
    using IOError::IOError;
};

class WriteError : public IOError
{
public:
    using IOError::IOError;
};

struct StartStopMessage
{
    bool start_streaming = false;
    std::set<SpiceVideoCodecType> client_codecs;
};

struct InCapabilitiesMessage {};

struct NotifyErrorMessage
{
    uint32_t error_code;
    char message[1025];
};

class InboundMessage
{
public:
    InboundMessage(const StreamDevHeader &header, std::unique_ptr<uint8_t[]> &&data);

    template<class Payload> Payload get_payload();

    const StreamDevHeader header;
private:
    std::unique_ptr<uint8_t[]> data;
};

template<>
StartStopMessage InboundMessage::get_payload<StartStopMessage>();
template<>
InCapabilitiesMessage InboundMessage::get_payload<InCapabilitiesMessage>();
template<>
NotifyErrorMessage InboundMessage::get_payload<NotifyErrorMessage>();

class StreamPort {
public:
    StreamPort(const std::string &port_name);
    ~StreamPort();

    InboundMessage receive();

    template <typename Message, typename ...PayloadArgs>
    void send(PayloadArgs&&... payload_args)
    {
        Message message(payload_args...);
        std::lock_guard<std::mutex> stream_guard(mutex);
        message.write_header(*this);
        message.write_message_body(*this, payload_args...);
    }

    void write(const void *buf, size_t len);

    const int fd;

private:
    std::mutex mutex;
};

template <typename Payload, typename Message, unsigned Type>
class OutboundMessage
{
public:
    template <typename ...PayloadArgs>
    OutboundMessage(PayloadArgs&&... payload_args)
    {
        hdr.protocol_version = STREAM_DEVICE_PROTOCOL;
        hdr.type = Type;
        hdr.size = (uint32_t) Message::size(payload_args...);
    }

    void write_header(StreamPort &stream_port)
    {
        stream_port.write(&hdr, sizeof(hdr));
    }

protected:
    StreamDevHeader hdr{};
    using PayloadType = Payload;
};

void read_all(int fd, void *buf, size_t len);
void write_all(int fd, const void *buf, size_t len);

}} // namespace spice::streaming_agent
