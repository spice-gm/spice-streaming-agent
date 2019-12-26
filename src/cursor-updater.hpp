/* A class that monitors X11 cursor changes and sends the cursor image over the
 * streaming virtio port.
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include "stream-port.hpp"

#include <X11/Xlib.h>


namespace spice {
namespace streaming_agent {

class CursorUpdater
{
public:
    CursorUpdater(StreamPort *stream_port);

    [[noreturn]] void operator()();

private:
    StreamPort *stream_port;
    Display *display;  // the X11 display
    int xfixes_event_base;  // event number for the XFixes events
};

}} // namespace spice::streaming_agent
