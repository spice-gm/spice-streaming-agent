/* \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#include <spice-streaming-agent/x11-display-info.hpp>

#include <spice-streaming-agent/display-info.hpp>
#include "utils.hpp"
#include <spice-streaming-agent/error.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <X11/extensions/Xrandr.h>
#include <xf86drm.h>
#include <xf86drmMode.h>


namespace spice {
namespace streaming_agent {

namespace {

constexpr uint32_t pci_vendor_id_redhat = 0x1b36;
constexpr uint32_t pci_device_id_qxl = 0x0100;

struct OutputInfo {
    std::string output_name;
    std::string card_path;
    uint32_t card_vendor_id;
    uint32_t card_device_id;
    uint32_t device_display_id;
};

static const std::map<uint32_t, std::string> modesetting_output_names = {
    {DRM_MODE_CONNECTOR_Unknown, "None"},
    {DRM_MODE_CONNECTOR_VGA, "VGA"},
    {DRM_MODE_CONNECTOR_DVII, "DVI-I"},
    {DRM_MODE_CONNECTOR_DVID, "DVI-D"},
    {DRM_MODE_CONNECTOR_DVIA, "DVI-A"},
    {DRM_MODE_CONNECTOR_Composite, "Composite"},
    {DRM_MODE_CONNECTOR_SVIDEO, "SVIDEO"},
    {DRM_MODE_CONNECTOR_LVDS, "LVDS"},
    {DRM_MODE_CONNECTOR_Component, "Component"},
    {DRM_MODE_CONNECTOR_9PinDIN, "DIN"},
    {DRM_MODE_CONNECTOR_DisplayPort, "DP"},
    {DRM_MODE_CONNECTOR_HDMIA, "HDMI"},
    {DRM_MODE_CONNECTOR_HDMIB, "HDMI-B"},
    {DRM_MODE_CONNECTOR_TV, "TV"},
    {DRM_MODE_CONNECTOR_eDP, "eDP"},
    {DRM_MODE_CONNECTOR_VIRTUAL, "Virtual"},
    {DRM_MODE_CONNECTOR_DSI, "DSI"},
    {DRM_MODE_CONNECTOR_DPI, "DPI"},
};

// Connector type names from qxl driver
static const std::map<uint32_t, std::string> qxl_output_names = {
    {DRM_MODE_CONNECTOR_Unknown, "None"},
    {DRM_MODE_CONNECTOR_VGA, "VGA"},
    {DRM_MODE_CONNECTOR_DVII, "DVI"},
    {DRM_MODE_CONNECTOR_DVID, "DVI"},
    {DRM_MODE_CONNECTOR_DVIA, "DVI"},
    {DRM_MODE_CONNECTOR_Composite, "Composite"},
    {DRM_MODE_CONNECTOR_SVIDEO, "S-video"},
    {DRM_MODE_CONNECTOR_LVDS, "LVDS"},
    {DRM_MODE_CONNECTOR_Component, "CTV"},
    {DRM_MODE_CONNECTOR_9PinDIN, "DIN"},
    {DRM_MODE_CONNECTOR_DisplayPort, "DisplayPort"},
    {DRM_MODE_CONNECTOR_HDMIA, "HDMI"},
    {DRM_MODE_CONNECTOR_HDMIB, "HDMI"},
    {DRM_MODE_CONNECTOR_TV, "TV"},
    {DRM_MODE_CONNECTOR_eDP, "eDP"},
    {DRM_MODE_CONNECTOR_VIRTUAL, "Virtual"},
};

std::string get_drm_name(const std::map<uint32_t, std::string> &name_map,
                         uint32_t connector_type,
                         uint32_t connector_type_id)
{
    auto name_it = name_map.find(connector_type);
    if (name_it == name_map.end()) {
        throw Error("Could not find DRM connector name for type " + std::to_string(connector_type));
    }

    return name_it->second + "-" + std::to_string(connector_type_id);
}

class DrmOutputGetter
{
public:
    DrmOutputGetter(const char* card_path)
    {
        drm_fd = open(card_path, O_RDWR);
        if (drm_fd < 0) {
            throw Error(std::string("Unable to open file %s: ") + strerror(errno));
        }

        drm_resources = drmModeGetResources(drm_fd);

        if (drm_resources == nullptr) {
            close(drm_fd);
            throw Error(std::string("Unable to get DRM resources for card ") + card_path);
        }
    }

    ~DrmOutputGetter()
    {
        drmModeFreeResources(drm_resources);
        close(drm_fd);
    }

    std::vector<std::string> get_output_names(const std::map<uint32_t, std::string> &names_map,
                                              bool decrement = false)
    {
        std::vector<std::string> result;

        for (int i = 0; i < drm_resources->count_connectors; ++i) {
            drmModeConnectorPtr conn = drmModeGetConnector(drm_fd, drm_resources->connectors[i]);

            if (conn == nullptr) {
                throw Error(std::string("Unable to get DRM connector: ") + strerror(errno));
            }

            uint32_t cti = conn->connector_type_id;
            if (decrement) {
                cti--;
            }
            result.push_back(get_drm_name(names_map, conn->connector_type, cti));
        }

        return result;
    }

private:
    int drm_fd = -1;
    drmModeResPtr drm_resources = nullptr;
};

std::vector<OutputInfo> get_outputs()
{
    std::vector<OutputInfo> result;

    for (uint8_t card_id = 0; card_id < 10; ++card_id) {
        std::string card_name = "card" + std::to_string(card_id);

        char drm_path[64];
        snprintf(drm_path, sizeof(drm_path), DRM_DEV_NAME, DRM_DIR_NAME, card_id);

        struct stat stat_buf;
        if (stat(drm_path, &stat_buf) != 0) {
            if (errno == ENOENT) {
                // we're done searching
                break;
            }
            throw Error(std::string("Error accessing DRM node for card ") + drm_path);
        }

        std::string sys_path = "/sys/class/drm/" + card_name;

        uint32_t vendor_id = read_hex_number_from_file(sys_path + "/device/vendor");
        uint32_t device_id = read_hex_number_from_file(sys_path + "/device/device");

        std::vector<std::string> outputs;

        if (vendor_id == pci_vendor_id_redhat && device_id == pci_device_id_qxl) {
            // to find out whether QXL output names need decrementing, look for
            // an output called Virtual-0 in /sys/class/drm and if we find one,
            // decrement the number in the output names
            const auto globs = utils::glob("/sys/class/drm/card*-Virtual-0");
            outputs = DrmOutputGetter(drm_path).get_output_names(qxl_output_names, globs.size() > 0);
        } else {
            outputs = DrmOutputGetter(drm_path).get_output_names(modesetting_output_names);
        }

        for (uint32_t i = 0; i < outputs.size(); ++i) {
            result.push_back({outputs[i],
                              sys_path,
                              vendor_id,
                              device_id,
                              i});
        }
    }

    return result;
}

} // namespace

std::vector<std::string> get_xrandr_outputs(Display *display, Window window)
{
    XRRScreenResources *screen_resources = XRRGetScreenResources(display, window);
    std::vector<std::string> result;
    for (int i = 0; i < screen_resources->noutput; ++i) {
        XRROutputInfo *output_info = XRRGetOutputInfo(display,
                                                      screen_resources,
                                                      screen_resources->outputs[i]);

        result.emplace_back(output_info->name);

        XRRFreeOutputInfo(output_info);
    }

    XRRFreeScreenResources(screen_resources);
    return result;
}

std::vector<DeviceDisplayInfo> get_device_display_info_drm(Display *display)
{
    auto outputs = get_outputs();

    // sorting by output name, may not be necessary (the list should be sorted out of glob)
    std::sort(outputs.begin(), outputs.end(), [](const OutputInfo &oi1, const OutputInfo &oi2) {
            return oi1.output_name > oi2.output_name;
        }
    );

    // Check if the output names of QXL cards start at Virtual-1 and if so,
    // subtract one from each of them
    for (auto &output : outputs) {
        // if we find a QXL card
        if (output.card_vendor_id == pci_vendor_id_redhat &&
            output.card_device_id == pci_device_id_qxl)
        {
            // if the first name is Virtual-0, we know we are good, quit the loop
            if (output.output_name == "Virtual-0") {
                break;
            }

            // if the name starts with "Virtual-"
            if (output.output_name.compare(0, 8, "Virtual-") == 0) {
                // decrement the last digit by one
                // fails if the number at the end is more than one digit,
                // which would imply multiple QXL devices, an unsupported case
                output.output_name[output.output_name.length() - 1]--;
            }
        }
    }

    std::map<std::string, OutputInfo> output_map;
    for (const auto &output : outputs) {
        output_map[output.output_name] = output;
    }

    auto xrandr_outputs = get_xrandr_outputs(display, RootWindow(display, XDefaultScreen(display)));

    std::vector<DeviceDisplayInfo> result;

    for (uint32_t i = 0; i < xrandr_outputs.size(); ++i) {
        const auto &xoutput = xrandr_outputs[i];
        const auto it = output_map.find(xoutput);
        if (it == output_map.end()) {
            throw Error("Could not find card for output " + xoutput);
        }

        std::string device_address = get_device_address(it->second.card_path);

        result.push_back({i, device_address, it->second.device_display_id});
    }

    return result;
}

std::vector<DeviceDisplayInfo> get_device_display_info_no_drm(Display *display)
{
    // look for the first card that doesn't list its outputs (and therefore
    // doesn't support DRM), that's the best we can do...
    const auto globs = utils::glob("/sys/class/drm/card*");

    std::string found_card_path;
    for (const auto &path : globs) {
        std::string card_output = path.substr(path.rfind("/") + 1, path.npos);
        auto dash_it = card_output.find("-");
        // if this file is a card (and not an output)
        if (dash_it == card_output.npos) {
            // if we already have a found card, this means it doesn't have any outputs,
            // we have found our card
            if (!found_card_path.empty()) {
                break;
            }
            found_card_path = card_output;
        } else {
            found_card_path = "";
        }
    }

    std::string device_address = get_device_address(found_card_path);

    auto xrandr_outputs = get_xrandr_outputs(display, RootWindow(display, XDefaultScreen(display)));

    std::vector<DeviceDisplayInfo> result;

    for (uint32_t i = 0; i < xrandr_outputs.size(); ++i) {
        result.push_back({i, device_address, i});
    }

    return result;
}

}} // namespace spice::streaming_agent
