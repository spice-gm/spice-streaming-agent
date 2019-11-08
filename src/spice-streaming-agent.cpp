/* An implementation of a SPICE streaming agent
 *
 * \copyright
 * Copyright 2016-2017 Red Hat Inc. All rights reserved.
 */

#include "concrete-agent.hpp"
#include "mjpeg-fallback.hpp"
#include "cursor-updater.hpp"
#include "frame-log.hpp"
#include "stream-port.hpp"
#include "utils.hpp"
#include <spice-streaming-agent/error.hpp>

#include <spice/stream-device.h>
#include <spice/enums.h>

#include <spice-streaming-agent/frame-capture.hpp>
#include <spice-streaming-agent/plugin.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <poll.h>
#include <syslog.h>
#include <signal.h>
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <memory>
#include <thread>
#include <vector>
#include <string>

#include <common/utils.h>

using namespace spice::streaming_agent;

class FormatMessage : public OutboundMessage<StreamMsgFormat, FormatMessage, STREAM_TYPE_FORMAT>
{
public:
    FormatMessage(unsigned w, unsigned h, uint8_t c) {}

    static size_t size()
    {
        return sizeof(PayloadType);
    }

    void write_message_body(StreamPort &stream_port, unsigned w, unsigned h, uint8_t c)
    {
        StreamMsgFormat msg{};
        msg.width = w;
        msg.height = h;
        msg.codec = c;

        stream_port.write(&msg, sizeof(msg));
    }
};

class FrameMessage : public OutboundMessage<StreamMsgData, FrameMessage, STREAM_TYPE_DATA>
{
public:
    FrameMessage(const void *frame, size_t length) : OutboundMessage(length) {}

    static size_t size(size_t length)
    {
        return sizeof(PayloadType) + length;
    }

    void write_message_body(StreamPort &stream_port, const void *frame, size_t length)
    {
        stream_port.write(frame, length);
    }
};

class CapabilitiesOutMessage : public OutboundMessage<StreamMsgCapabilities, CapabilitiesOutMessage, STREAM_TYPE_CAPABILITIES>
{
public:
    CapabilitiesOutMessage(const std::vector<bool> &capabilities) : OutboundMessage() {}

    static size_t size()
    {
        uint8_t caps[AgentCapabilitiesBytes];

        return sizeof(PayloadType) + sizeof(caps);
    }

    void write_message_body(StreamPort &stream_port, const std::vector<bool> &capabilities)
    {
        uint8_t caps[AgentCapabilitiesBytes] = {};
        size_t i = 0;

        for (auto cap: capabilities) {
            if (cap) {
                set_bitmap(i, caps);
            }
            i++;
        }
        stream_port.write(caps, sizeof(caps));
    }
};

class DeviceDisplayInfoMessage : public OutboundMessage<StreamMsgDeviceDisplayInfo, DeviceDisplayInfoMessage, STREAM_TYPE_DEVICE_DISPLAY_INFO>
{
public:
    DeviceDisplayInfoMessage(const DeviceDisplayInfo &info) : OutboundMessage(info) {}

    static size_t size(const DeviceDisplayInfo &info)
    {
        return sizeof(PayloadType) +
               std::min(info.device_address.length(), static_cast<size_t>(max_device_address_len)) +
               1;
    }

    void write_message_body(StreamPort &stream_port, const DeviceDisplayInfo &info)
    {
        std::string device_address = info.device_address;
        if (device_address.length() > max_device_address_len) {
            syslog(LOG_WARNING,
                   "device address of stream id %u is longer than %u bytes, trimming.",
                   info.stream_id, max_device_address_len);
            device_address = device_address.substr(0, max_device_address_len);
        }
        StreamMsgDeviceDisplayInfo strm_msg_info{};
        strm_msg_info.stream_id = info.stream_id;
        strm_msg_info.device_display_id = info.device_display_id;
        strm_msg_info.device_address_len = device_address.length() + 1;
        stream_port.write(&strm_msg_info, sizeof(strm_msg_info));
        stream_port.write(device_address.c_str(), device_address.length() + 1);
    }

private:
    static constexpr uint32_t max_device_address_len = 255;
};

static bool streaming_requested = false;
static bool quit_requested = false;
static std::set<SpiceVideoCodecType> client_codecs;

static bool have_something_to_read(StreamPort &stream_port, bool blocking)
{
    struct pollfd pollfd = {stream_port.fd, POLLIN, 0};

    if (poll(&pollfd, 1, blocking ? -1 : 0) < 0) {
        if (errno == EINTR) {
            // report nothing to read, next iteration of the enclosing loop will retry
            return false;
        }

        throw IOError("poll failed on the device", errno);
    }

    if (pollfd.revents & POLLIN) {
        return true;
    }

    return false;
}

static void server_capabilities_received(StreamPort &stream_port,
                                         std::vector<bool> &server_capabilities)
{
#if 0 /* No capability defined at the moment */
    /* Check here if the server supports the capability */
    if (server_capabilities[STREAM_CAP_...]) {
        ...
    }
#endif
}

static void read_command_from_device(StreamPort &stream_port)
{
    InboundMessage in_message = stream_port.receive();

    switch (in_message.header.type) {
    case STREAM_TYPE_CAPABILITIES: {
        InCapabilitiesMessage msg = in_message.get_payload<InCapabilitiesMessage>();
        std::vector<bool> agent_capabilities(STREAM_CAP_END, false);

        server_capabilities_received(stream_port, msg.capabilities);

        // populate here the `agent_capabilities` vector
        // agent_capabilities[STREAM_CAP_...] = true;
        stream_port.send<CapabilitiesOutMessage>(agent_capabilities);
        return;
    }
    case STREAM_TYPE_NOTIFY_ERROR: {
        NotifyErrorMessage msg = in_message.get_payload<NotifyErrorMessage>();

        syslog(LOG_ERR, "Received NotifyError message from the server: %d - %s",
               msg.error_code, msg.message);
        return;
    }
    case STREAM_TYPE_START_STOP: {
        StartStopMessage msg = in_message.get_payload<StartStopMessage>();
        streaming_requested = msg.start_streaming;
        client_codecs = msg.client_codecs;

        syslog(LOG_INFO, "GOT START_STOP message -- request to %s streaming",
               streaming_requested ? "START" : "STOP");
        return;
    }}

    throw std::runtime_error("UNKNOWN msg of type " + std::to_string(in_message.header.type));
}

static void read_command(StreamPort &stream_port, bool blocking)
{
    while (!quit_requested) {
        if (have_something_to_read(stream_port, blocking)) {
            read_command_from_device(stream_port);
            break;
        }

        if (!blocking) {
            break;
        }

        sleep(1);
    }
}

static void handle_interrupt(int intr)
{
    syslog(LOG_INFO, "Got signal %d, exiting", intr);
    quit_requested = true;
}

static void register_interrupts(void)
{
    struct sigaction sa = { };
    sa.sa_handler = handle_interrupt;
    if ((sigaction(SIGINT, &sa, NULL) != 0) &&
        (sigaction(SIGTERM, &sa, NULL) != 0)) {
        syslog(LOG_WARNING, "failed to register signal handler %m");
    }
}

static void usage(const char *progname)
{
    printf("usage: %s <options>\n", progname);
    printf("options are:\n");
    printf("\t-p portname  -- virtio-serial port to use\n");
    printf("\t-l file -- log frames to file\n");
    printf("\t--log-binary -- log binary frames (following -l)\n");
    printf("\t--log-categories -- log categories, separated by ':' (currently: frames)\n");
    printf("\t--plugins-dir=path -- change plugins directory\n");
    printf("\t-d -- enable debug logs\n");
    printf("\t-c variable=value -- change settings\n");
    printf("\t\tframerate = 1-100 (check 10,20,30,40,50,60)\n");
    printf("\n");
    printf("\t-h or --help     -- print this help message\n");

    exit(1);
}

static void
do_capture(StreamPort &stream_port, FrameLog &frame_log, ConcreteAgent &agent)
{
    unsigned int frame_count = 0;
    while (!quit_requested) {
        while (!quit_requested && !streaming_requested) {
            read_command(stream_port, true);
        }

        if (quit_requested) {
            return;
        }

        syslog(LOG_INFO, "streaming starts now");
        uint64_t time_last = 0;

        std::unique_ptr<FrameCapture> capture(agent.GetBestFrameCapture(client_codecs));
        if (!capture) {
            throw std::runtime_error("cannot find a suitable capture system");
        }

        std::vector<DeviceDisplayInfo> display_info;
        try {
            display_info = capture->get_device_display_info();
        } catch (const Error &e) {
            syslog(LOG_ERR, "Error while getting device display info: %s", e.what());
        }

        syslog(LOG_DEBUG, "Got device info of %zu devices from the plugin", display_info.size());
        for (const auto &info : display_info) {
            syslog(LOG_DEBUG, "   stream id %u: device address: %s, device display id: %u",
                   info.stream_id,
                   info.device_address.c_str(),
                   info.device_display_id);
        }

        if (display_info.size() > 0) {
            if (display_info.size() > 1) {
                syslog(LOG_WARNING, "Warning: the Frame Capture plugin returned device display "
                       "info for more than one display device, but we currently only support "
                       "a single device. Sending information for first device to the server.");
            }
            stream_port.send<DeviceDisplayInfoMessage>(display_info[0]);
        } else {
            syslog(LOG_ERR, "Empty device display info from the plugin");
        }

        while (!quit_requested && streaming_requested) {
            if (++frame_count % 100 == 0) {
                syslog(LOG_DEBUG, "SENT %d frames", frame_count);
            }
            uint64_t time_before = FrameLog::get_time();

            frame_log.log_stat("Capturing frame...");
            FrameInfo frame = capture->CaptureFrame();
            frame_log.log_stat("Captured frame");

            uint64_t time_after = FrameLog::get_time();
            syslog(LOG_DEBUG,
                   "got a frame -- size is %zu (%" PRIu64 " ms) "
                   "(%" PRIu64 " ms from last frame)(%" PRIu64 " us)\n",
                   frame.buffer_size, (time_after - time_before)/1000,
                   (time_after - time_last)/1000,
                   (time_before - time_last));
            time_last = time_after;

            if (frame.stream_start) {
                unsigned width, height;
                unsigned char codec;

                width = frame.size.width;
                height = frame.size.height;
                codec = capture->VideoCodecType();

                syslog(LOG_DEBUG, "wXh %uX%u  codec=%u", width, height, codec);
                frame_log.log_stat("Started new stream wXh %uX%u codec=%u", width, height, codec);

                stream_port.send<FormatMessage>(width, height, codec);
            }
            frame_log.log_stat("Frame of %zu bytes", frame.buffer_size);
            frame_log.log_frame(frame.buffer, frame.buffer_size);

            try {
                stream_port.send<FrameMessage>(frame.buffer, frame.buffer_size);
            } catch (const WriteError& e) {
                utils::syslog(e);
                break;
            }
            frame_log.log_stat("Sent frame");

            read_command(stream_port, false);
        }
    }
}

int main(int argc, char* argv[])
{
    const char *stream_port_name = "/dev/virtio-ports/org.spice-space.stream.0";
    int opt;
    const char *log_filename = NULL;
    bool log_binary = false;
    bool log_frames = false;
    const char *pluginsdir = PLUGINSDIR;
    enum {
        OPT_first = UCHAR_MAX,
        OPT_PLUGINS_DIR,
        OPT_LOG_BINARY,
        OPT_LOG_CATEGORIES,
    };
    static const struct option long_options[] = {
        { "plugins-dir", required_argument, NULL, OPT_PLUGINS_DIR},
        { "log-binary", no_argument, NULL, OPT_LOG_BINARY},
        { "log-categories", required_argument, NULL, OPT_LOG_CATEGORIES},
        { "help", no_argument, NULL, 'h'},
        { 0, 0, 0, 0}
    };
    std::vector<std::string> old_args(argv, argv+argc);

    openlog("spice-streaming-agent",
            isatty(fileno(stderr)) ? (LOG_PERROR|LOG_PID) : LOG_PID, LOG_USER);

    setlogmask(LOG_UPTO(LOG_NOTICE));

    std::vector<ConcreteConfigureOption> options;

    while ((opt = getopt_long(argc, argv, "hp:c:l:d", long_options, NULL)) != -1) {
        switch (opt) {
        case 0:
            /* Handle long options if needed */
            break;
        case OPT_PLUGINS_DIR:
            pluginsdir = optarg;
            break;
        case 'p':
            stream_port_name = optarg;
            break;
        case 'c': {
            char *p = strchr(optarg, '=');
            if (p == NULL) {
                syslog(LOG_ERR, "Invalid '-c' argument value: %s", optarg);
                usage(argv[0]);
            }
            *p++ = '\0';
            options.push_back(ConcreteConfigureOption(optarg, p));
            break;
        }
        case OPT_LOG_BINARY:
            log_binary = true;
            break;
        case OPT_LOG_CATEGORIES:
            for (const char *tok = strtok(optarg, ":"); tok; tok = strtok(nullptr, ":")) {
                std::string cat = tok;
                if (cat == "frames") {
                    log_frames = true;
                }
                // ignore not existing, compatibility for future
            }
            break;
        case 'l':
            log_filename = optarg;
            break;
        case 'd':
            setlogmask(LOG_UPTO(LOG_DEBUG));
            break;
        case 'h':
            usage(argv[0]);
            break;
        }
    }

    register_interrupts();

    try {
        FrameLog frame_log(log_filename, log_binary, log_frames);

        ConcreteAgent agent(options, &frame_log);

        // register built-in plugins
        MjpegPlugin::Register(&agent);

        agent.LoadPlugins(pluginsdir);

        for (const std::string& arg: old_args) {
            frame_log.log_stat("Args: %s", arg.c_str());
        }
        old_args.clear();

        StreamPort stream_port(stream_port_name);

        std::thread cursor_updater{CursorUpdater(&stream_port)};
        cursor_updater.detach();

        do_capture(stream_port, frame_log, agent);
    }
    catch (std::exception &err) {
        syslog(LOG_ERR, "%s", err.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
