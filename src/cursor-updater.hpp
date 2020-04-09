/* A class that monitors X11 cursor changes and sends the cursor image over the
 * streaming virtio port.
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include "stream-port.hpp"

#include <xcb/xfixes.h>
#include <memory>

namespace spice {
namespace streaming_agent {

#define DECLARE_T_UPTR(type, func)   \
    struct type##_deleter {          \
        void operator()(type##_t* p) \
        {                            \
            func(p);                 \
        }                            \
    };                               \
    using type##_uptr = std::unique_ptr<type##_t, type##_deleter>;

DECLARE_T_UPTR(xcb_connection, xcb_disconnect)
DECLARE_T_UPTR(xcb_xfixes_get_cursor_image_reply, free)

class CursorUpdater
{
public:
    CursorUpdater(StreamPort *stream_port);

    [[noreturn]] void operator()();

private:
    StreamPort *stream_port;
    xcb_connection_uptr con; // connection to X11
    uint32_t xfixes_event_base;  // event number for the XFixes events
};

}} // namespace spice::streaming_agent
