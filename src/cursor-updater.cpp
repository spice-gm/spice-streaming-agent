/* A class that monitors X11 cursor changes and sends the cursor image over the
 * streaming virtio port.
 *
 * \copyright
 * Copyright 2016-2018 Red Hat Inc. All rights reserved.
 */

#include "cursor-updater.hpp"

#include <spice-streaming-agent/error.hpp>

#include <spice/stream-device.h>
#include <spice/enums.h>

#include <memory>
#include <vector>
#include <syslog.h>
#include <unistd.h>
#include <X11/extensions/Xfixes.h>


namespace spice {
namespace streaming_agent {

class CursorError : public Error
{
    using Error::Error;
};

class CursorMessage : public OutboundMessage<StreamMsgCursorSet, CursorMessage, STREAM_TYPE_CURSOR_SET>
{
public:
    CursorMessage(uint16_t width, uint16_t height, uint16_t xhot, uint16_t yhot,
        const std::vector<uint32_t> &pixels)
    :
        OutboundMessage(pixels)
    {
        if (width >= STREAM_MSG_CURSOR_SET_MAX_WIDTH) {
            throw CursorError("Cursor width " + std::to_string(width) +
                " too big (limit is " + std::to_string(STREAM_MSG_CURSOR_SET_MAX_WIDTH) + ")");
        }

        if (height >= STREAM_MSG_CURSOR_SET_MAX_HEIGHT) {
            throw CursorError("Cursor height " + std::to_string(height) +
                " too big (limit is " + std::to_string(STREAM_MSG_CURSOR_SET_MAX_HEIGHT) + ")");
        }
    }

    static size_t size(const std::vector<uint32_t> &pixels)
    {
        return sizeof(PayloadType) + sizeof(uint32_t) * pixels.size();
    }

    void write_message_body(StreamPort &stream_port,
        uint16_t width, uint16_t height, uint16_t xhot, uint16_t yhot,
        const std::vector<uint32_t> &pixels)
    {
        StreamMsgCursorSet msg{};
        msg.type = SPICE_CURSOR_TYPE_ALPHA;
        msg.width = width;
        msg.height = height;
        msg.hot_spot_x = xhot;
        msg.hot_spot_y = yhot;

        stream_port.write(&msg, sizeof(msg));
        stream_port.write(pixels.data(), sizeof(uint32_t) * pixels.size());
    }
};

CursorUpdater::CursorUpdater(StreamPort *stream_port) : stream_port(stream_port)
{
    display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        throw Error("Failed to open X display");
    }

    int error_base;
    if (!XFixesQueryExtension(display, &xfixes_event_base, &error_base)) {
        throw Error("XFixesQueryExtension failed");
    }

    XFixesSelectCursorInput(display, DefaultRootWindow(display), XFixesDisplayCursorNotifyMask);
}

void CursorUpdater::operator()()
{
    unsigned long last_serial = 0;

    while (1) {
        try {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != xfixes_event_base + 1) {
                continue;
            }

            XFixesCursorImage *cursor = XFixesGetCursorImage(display);
            if (!cursor) {
                continue;
            }

            if (cursor->cursor_serial == last_serial) {
                continue;
            }

            if (cursor->width  > STREAM_MSG_CURSOR_SET_MAX_WIDTH ||
                cursor->height > STREAM_MSG_CURSOR_SET_MAX_HEIGHT) {
                ::syslog(LOG_WARNING, "cursor updater thread: ignoring cursor: too big %ux%u",
                         cursor->width, cursor->height);
                continue;
            }

            last_serial = cursor->cursor_serial;

            // the X11 cursor data may be in a wrong format, copy them to an uint32_t array
            size_t pixcount = cursor->width * cursor->height;
            std::vector<uint32_t> pixels;
            pixels.reserve(pixcount);

            for (size_t i = 0; i < pixcount; ++i) {
                pixels.push_back(cursor->pixels[i]);
            }

            stream_port->send<CursorMessage>(cursor->width, cursor->height,
                                             cursor->xhot, cursor->yhot, pixels);
        } catch (const std::exception &e) {
            ::syslog(LOG_ERR, "Error in cursor updater thread: %s", e.what());
            sleep(1); // rate-limit the error
        }
    }
}

}} // namespace spice::streaming_agent
