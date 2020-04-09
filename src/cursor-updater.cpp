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

CursorUpdater::CursorUpdater(StreamPort *stream_port) :
    stream_port(stream_port),
    con(xcb_connect(nullptr, nullptr))
{
    if (xcb_connection_has_error(con.get())) {
        throw Error("Failed to initiate connection to X");
    }

    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(con.get())).data;
    if (!screen) {
        throw Error("Cannot get XCB screen");
    }

    // init xfixes
    const xcb_query_extension_reply_t *reply = xcb_get_extension_data(con.get(), &xcb_xfixes_id);
    if (!reply || !reply->present) {
        throw Error("Get xfixes extension failed");
    }
    xfixes_event_base = reply->first_event;

    xcb_xfixes_query_version_cookie_t xfixes_query_cookie;
    xcb_xfixes_query_version_reply_t *xfixes_query_reply;
    xcb_generic_error_t *error = 0;

    xfixes_query_cookie = xcb_xfixes_query_version(con.get(), XCB_XFIXES_MAJOR_VERSION,
                                                   XCB_XFIXES_MINOR_VERSION);
    xfixes_query_reply = xcb_xfixes_query_version_reply(con.get(), xfixes_query_cookie, &error);
    if (!xfixes_query_reply || error) {
        free(xfixes_query_reply);
        free(error);
        throw Error("Query xfixes extension failed");
    }
    free(xfixes_query_reply);

    // register to cursor events
    xcb_xfixes_select_cursor_input(con.get(), screen->root,
                                   XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);

    xcb_flush(con.get());
}

void CursorUpdater::operator()()
{
    unsigned long last_serial = 0;

    while (1) {
        try {
            while (auto event = xcb_wait_for_event(con.get())) {
                if (event->response_type != xfixes_event_base + XCB_XFIXES_CURSOR_NOTIFY) {
                    continue;
                }

                xcb_xfixes_get_cursor_image_cookie_t cookie;

                cookie = xcb_xfixes_get_cursor_image(con.get());
                xcb_xfixes_get_cursor_image_reply_uptr
                    cursor_reply(xcb_xfixes_get_cursor_image_reply(con.get(), cookie, nullptr));

                if (cursor_reply->cursor_serial == last_serial) {
                    continue;
                }

                if (cursor_reply->width  > STREAM_MSG_CURSOR_SET_MAX_WIDTH ||
                    cursor_reply->height > STREAM_MSG_CURSOR_SET_MAX_HEIGHT) {
                    ::syslog(LOG_WARNING, "cursor updater thread: ignoring cursor: too big %ux%u",
                             cursor_reply->width, cursor_reply->height);
                    continue;
                }

                last_serial = cursor_reply->cursor_serial;

                // the X11 cursor data may be in a wrong format, copy them to an uint32_t array
                size_t pixcount =
                    xcb_xfixes_get_cursor_image_cursor_image_length(cursor_reply.get());
                std::vector<uint32_t> pixels;
                pixels.reserve(pixcount);
                const uint32_t *reply_pixels =
                    xcb_xfixes_get_cursor_image_cursor_image(cursor_reply.get());

                for (size_t i = 0; i < pixcount; ++i) {
                    pixels.push_back(reply_pixels[i]);
                }

                stream_port->send<CursorMessage>(cursor_reply->width, cursor_reply->height,
                                             cursor_reply->xhot, cursor_reply->yhot, pixels);
            }
        } catch (const std::exception &e) {
            ::syslog(LOG_ERR, "Error in cursor updater thread: %s", e.what());
            sleep(1); // rate-limit the error
        }
    }
}

}} // namespace spice::streaming_agent
