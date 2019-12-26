/* \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include <spice-streaming-agent/frame-capture.hpp>

#include <X11/Xlib.h>


namespace spice __attribute__ ((visibility ("default"))) {
namespace streaming_agent {

/**
 * Looks up device display info by listing the xrandr outputs of @display and
 * comparing them to card output names found under the DRM subsystem in
 * /sys/class/drm.
 *
 * Throws an Error in case of error or when the DRM outputs cannot be found.
 *
 * @param display the X display to find the info for
 * @return a vector of DeviceDisplayInfo structs containing the information of
 * all the outputs used by the display
 */
std::vector<DeviceDisplayInfo> get_device_display_info_drm(Display *display);

/**
 * Attempts to get the device display info without using DRM. It looks up the
 * first card in /sys/class/drm that doesn't have its outputs listed (assuming
 * that if the card in use has DRM outputs, it would be found using the
 * get_display_info function) and simply uses the xrandr output index as the
 * device_display_id. This can obviously incorrectly match the device address
 * and the device_display_ids of two unrelated devices.
 *
 * Unfortunately, without DRM, there is no way to match xrandr objects with the
 * real device.
 *
 * @param display the X display to find the info for
 * @return a vector of DeviceDisplayInfo structs containing the information of
 * all the outputs used by the display
 */
std::vector<DeviceDisplayInfo> get_device_display_info_no_drm(Display *display);

/**
 * Lists xrandr outputs and returns their names in a vector of strings.
 *
 * @param display the X display
 * @param window the X root window
 * @return A vector of xrandr output names
 */
std::vector<std::string> get_xrandr_outputs(Display *display, Window window);

}} // namespace spice::streaming_agent
