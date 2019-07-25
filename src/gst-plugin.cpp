/* Plugin implementation for gstreamer encoder
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#include <config.h>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <memory>
#include <syslog.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define XLIB_CAPTURE 1
#if XLIB_CAPTURE
#include <gst/app/gstappsrc.h>
#endif

#include <spice-streaming-agent/plugin.hpp>
#include <spice-streaming-agent/frame-capture.hpp>
#include <spice-streaming-agent/x11-display-info.hpp>


#define gst_syslog(priority, str, ...) syslog(priority, "Gstreamer plugin: " str, ## __VA_ARGS__);

namespace spice {
namespace streaming_agent {
namespace gstreamer_plugin {

struct GstreamerEncoderSettings
{
    int fps = 25;
    SpiceVideoCodecType codec = SPICE_VIDEO_CODEC_TYPE_H264;
    std::string encoder;
    std::vector<std::pair<std::string, std::string>> prop_pairs;
};

template <typename T>
struct GstObjectDeleter {
    void operator()(T* p)
    {
        gst_object_unref(p);
    }
};

template <typename T>
using GstObjectUPtr = std::unique_ptr<T, GstObjectDeleter<T>>;

struct GstCapsDeleter {
    void operator()(GstCaps* p)
    {
        gst_caps_unref(p);
    }
};

using GstCapsUPtr = std::unique_ptr<GstCaps, GstCapsDeleter>;

struct GstSampleDeleter {
    void operator()(GstSample* p)
    {
        gst_sample_unref(p);
    }
};

using GstSampleUPtr = std::unique_ptr<GstSample, GstSampleDeleter>;

class GstreamerFrameCapture final : public FrameCapture
{
public:
    GstreamerFrameCapture(const GstreamerEncoderSettings &settings);
    ~GstreamerFrameCapture();
    FrameInfo CaptureFrame() override;
    void Reset() override;
    SpiceVideoCodecType VideoCodecType() const override {
        return settings.codec;
    }
    std::vector<DeviceDisplayInfo> get_device_display_info() const override;
private:
    void free_sample();
    GstElement *get_encoder_plugin(const GstreamerEncoderSettings &settings, GstCapsUPtr &sink_caps);
    GstElement *get_capture_plugin(const GstreamerEncoderSettings &settings);
    void pipeline_init(const GstreamerEncoderSettings &settings);
    Display *const dpy;
#if XLIB_CAPTURE
    void xlib_capture();
    XImage *image = nullptr;
#endif
    GstObjectUPtr<GstElement> pipeline, capture, sink;
    GstSampleUPtr sample;
    GstMapInfo map = {};
    uint32_t last_width = ~0u, last_height = ~0u;
    uint32_t cur_width = 0, cur_height = 0;
    bool is_first = true;
    GstreamerEncoderSettings settings; // will be set by plugin settings
};

class GstreamerPlugin final: public Plugin
{
public:
    FrameCapture *CreateCapture() override;
    unsigned Rank() override;
    void ParseOptions(const ConfigureOption *options);
    SpiceVideoCodecType VideoCodecType() const override {
        return settings.codec;
    }
private:
    GstreamerEncoderSettings settings;
};

GstElement *GstreamerFrameCapture::get_capture_plugin(const GstreamerEncoderSettings &settings)
{
    GstElement *capture = nullptr;

#if XLIB_CAPTURE
    capture = gst_element_factory_make("appsrc", "capture");
#else
    capture = gst_element_factory_make("ximagesrc", "capture");
    g_object_set(capture,
                "use-damage", 0,
                 nullptr);
#endif
    return capture;
}

GstElement *GstreamerFrameCapture::get_encoder_plugin(const GstreamerEncoderSettings &settings,
                                                      GstCapsUPtr &sink_caps)
{
    GList *encoders;
    GList *filtered;
    GstElement *encoder;
    GstElementFactory *factory = nullptr;

    switch (settings.codec) {
    case SPICE_VIDEO_CODEC_TYPE_H264:
        sink_caps.reset(gst_caps_new_simple("video/x-h264",
                                            "stream-format", G_TYPE_STRING, "byte-stream",
                                            nullptr));
        break;
    case SPICE_VIDEO_CODEC_TYPE_MJPEG:
        sink_caps.reset(gst_caps_new_empty_simple("image/jpeg"));
        break;
    case SPICE_VIDEO_CODEC_TYPE_VP8:
        sink_caps.reset(gst_caps_new_empty_simple("video/x-vp8"));
        break;
    case SPICE_VIDEO_CODEC_TYPE_VP9:
        sink_caps.reset(gst_caps_new_empty_simple("video/x-vp9"));
        break;
    case SPICE_VIDEO_CODEC_TYPE_H265:
        sink_caps.reset(gst_caps_new_empty_simple("video/x-h265"));
        break;
    default : /* Should not happen - just to avoid compiler's complaint */
        throw std::logic_error("Unknown codec");
    }
    gst_caps_set_simple(sink_caps.get(), "framerate", GST_TYPE_FRACTION, settings.fps, 1, nullptr);
    std::unique_ptr<gchar, decltype(&g_free)> caps_str(gst_caps_to_string(sink_caps.get()), g_free);

    encoders = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_VIDEO_ENCODER, GST_RANK_NONE);
    filtered = gst_element_factory_list_filter(encoders, sink_caps.get(), GST_PAD_SRC, false);
    if (filtered) {
        gst_syslog(LOG_NOTICE, "Looking for encoder plugins which can produce a '%s' stream", caps_str.get());
        for (GList *l = filtered; l != nullptr; l = l->next) {
            if (!factory && !settings.encoder.compare(GST_ELEMENT_NAME(l->data))) {
                factory = (GstElementFactory*)l->data;
            }
            gst_syslog(LOG_NOTICE, "'%s' plugin is available", GST_ELEMENT_NAME(l->data));
        }
        if (!factory && !settings.encoder.empty()) {
            gst_syslog(LOG_WARNING,
                       "Specified encoder named '%s' cannot produce '%s' stream, make sure matching gst.codec is specified and plugin's availability",
                       settings.encoder.c_str(), caps_str.get());
        }
        factory = factory ? factory : (GstElementFactory*)filtered->data;
        gst_syslog(LOG_NOTICE, "'%s' encoder plugin is used", GST_ELEMENT_NAME(factory));

    } else {
        gst_syslog(LOG_ERR, "No suitable encoder was found for '%s'", caps_str.get());
    }

    encoder = factory ? gst_element_factory_create(factory, "encoder") : nullptr;
    if (encoder) { // Set encoder properties
        for (const auto &prop : settings.prop_pairs) {
            const auto &name = prop.first;
            const auto &value = prop.second;
            if (!g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), name.c_str())) {
                gst_syslog(LOG_WARNING, "'%s' property was not found for this encoder",
                           name.c_str());
                continue;
            }
            gst_syslog(LOG_NOTICE, "Trying to set encoder property: '%s = %s'",
                       name.c_str(), value.c_str());
            /* Invalid properties will be ignored silently */
            gst_util_set_object_arg(G_OBJECT(encoder), name.c_str(), value.c_str());
        }
    }
    gst_plugin_feature_list_free(filtered);
    gst_plugin_feature_list_free(encoders);
    return encoder;
}

// Utility to add an element to a GstBin
// This checks return value and update reference correctly
void gst_bin_add(GstBin *bin, const GstObjectUPtr<GstElement> &elem)
{
    if (::gst_bin_add(bin, elem.get())) {
        // ::gst_bin_add take ownership using floating references but
        // we still hold a reference in elem so update the reference
        // accordingly
        g_object_ref(elem.get());
    } else {
        throw std::runtime_error("Gstreamer's element cannot be added to pipeline");
    }
}

void GstreamerFrameCapture::pipeline_init(const GstreamerEncoderSettings &settings)
{
    gboolean link;

    GstObjectUPtr<GstElement> pipeline(gst_pipeline_new("pipeline"));
    if (!pipeline) {
        throw std::runtime_error("Gstreamer's pipeline element cannot be created");
    }
    GstObjectUPtr<GstElement> capture(get_capture_plugin(settings));
    if (!capture) {
        throw std::runtime_error("Gstreamer's capture element cannot be created");
    }
    GstObjectUPtr<GstElement> convert(gst_element_factory_make("autovideoconvert", "convert"));
    if (!convert) {
        throw std::runtime_error("Gstreamer's 'autovideoconvert' element cannot be created");
    }
    GstCapsUPtr sink_caps;
    GstObjectUPtr<GstElement> encoder(get_encoder_plugin(settings, sink_caps));
    if (!encoder) {
        throw std::runtime_error("Gstreamer's encoder element cannot be created");
    }
    GstObjectUPtr<GstElement> sink(gst_element_factory_make("appsink", "sink"));
    if (!sink) {
        throw std::runtime_error("Gstreamer's appsink element cannot be created");
    }

    g_object_set(sink.get(),
                 "sync", FALSE,
                 "drop", TRUE,
                 "max-buffers", 1,
                 nullptr);

    GstBin *bin = GST_BIN(pipeline.get());
    gst_bin_add(bin, capture);
    gst_bin_add(bin, convert);
    gst_bin_add(bin, encoder);
    gst_bin_add(bin, sink);

    GstCapsUPtr caps(gst_caps_from_string("video/x-raw(ANY)"));
    link = gst_element_link(capture.get(), convert.get()) &&
           gst_element_link_filtered(convert.get(), encoder.get(), caps.get()) &&
           gst_element_link_filtered(encoder.get(), sink.get(), sink_caps.get());
    if (!link) {
        throw std::runtime_error("Linking gstreamer's elements failed");
    }

    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);

#if !XLIB_CAPTURE
    int sx, sy, ex, ey;
    g_object_get(capture.get(),
                 "endx", &ex,
                 "endy", &ey,
                 "startx", &sx,
                 "starty", &sy,
                 nullptr);
    cur_width = ex - sx;
    cur_height = ey - sy;
    if (cur_width < 16 || cur_height < 16) {
         throw std::runtime_error("Invalid screen size");
    }
    g_object_set(capture.get(),
    /* Some encoders cannot handle odd resolution make sure it's even number of pixels
     * pixel counting starts from zero here!
     */
                 "endx", cur_width - !(cur_width % 2),
                 "endy", cur_height - !(cur_height % 2),
                 "startx", 0,
                 "starty", 0,
                 nullptr);
#endif

    this->sink.swap(sink);
    this->capture.swap(capture);
    this->pipeline.swap(pipeline);
}

GstreamerFrameCapture::GstreamerFrameCapture(const GstreamerEncoderSettings &settings):
    dpy(XOpenDisplay(nullptr)),settings(settings)
{
    if (!dpy) {
        throw std::runtime_error("Unable to initialize X11");
    }
    pipeline_init(settings);
}

void GstreamerFrameCapture::free_sample()
{
    if (sample) {
        gst_buffer_unmap(gst_sample_get_buffer(sample.get()), &map);
        sample.reset();
    }
#if XLIB_CAPTURE
    if(image) {
        image->f.destroy_image(image);
        image = nullptr;
    }
#endif
}

GstreamerFrameCapture::~GstreamerFrameCapture()
{
    free_sample();
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    XCloseDisplay(dpy);
}

void GstreamerFrameCapture::Reset()
{
    //TODO
}

#if XLIB_CAPTURE
void GstreamerFrameCapture::xlib_capture()
{
    int screen = XDefaultScreen(dpy);

    Window win = RootWindow(dpy, screen);
    XWindowAttributes win_info;
    XGetWindowAttributes(dpy, win, &win_info);

    /* Some encoders cannot handle odd resolution make sure it's even number of pixels */
    cur_width = win_info.width - win_info.width % 2;
    cur_height =  win_info.height - win_info.height % 2;

    if (cur_width != last_width || cur_height != last_height) {
        last_width = cur_width;
        last_height = cur_height;
        is_first = true;

        gst_app_src_end_of_stream(GST_APP_SRC(capture.get()));
        gst_element_set_state(pipeline.get(), GST_STATE_NULL);//maybe ximagesrc needs eos as well
        gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    }

    image = XGetImage(dpy, win, 0, 0,
                      cur_width, cur_height, AllPlanes, ZPixmap);
    if (!image) {
        throw std::runtime_error("Cannot capture from X");
    }

    GstBuffer *buf;
    buf = gst_buffer_new_wrapped(image->data, image->height * image->bytes_per_line);
    if (!buf) {
        throw std::runtime_error("Failed to wrap image in gstreamer buffer");
    }

    GstCapsUPtr caps(gst_caps_new_simple("video/x-raw",
                                         "format", G_TYPE_STRING, "BGRx",
                                         "width", G_TYPE_INT, image->width,
                                         "height", G_TYPE_INT, image->height,
                                         "framerate", GST_TYPE_FRACTION, settings.fps, 1,
                                         nullptr));

    // Push sample
    GstSampleUPtr appsrc_sample(gst_sample_new(buf, caps.get(), nullptr, nullptr));
    if (gst_app_src_push_sample(GST_APP_SRC(capture.get()), appsrc_sample.get()) != GST_FLOW_OK) {
        throw std::runtime_error("gstramer appsrc element cannot push sample");
    }
}
#endif

FrameInfo GstreamerFrameCapture::CaptureFrame()
{
    FrameInfo info;

    free_sample(); // free prev if exist

#if XLIB_CAPTURE
    xlib_capture();
#endif
    info.size.width = cur_width;
    info.size.height = cur_height;
    info.stream_start = is_first;
    if (is_first) {
        is_first = false;
    }

    // Pull sample
    sample.reset(gst_app_sink_pull_sample(GST_APP_SINK(sink.get()))); // blocking

    if (sample) { // map after pipeline
        if (!gst_buffer_map(gst_sample_get_buffer(sample.get()), &map, GST_MAP_READ)) {
            free_sample();
            throw std::runtime_error("Buffer mapping failed");
        }

        info.buffer = map.data;
        info.buffer_size = map.size;
    } else {
        throw std::runtime_error("No sample- EOS or state change");
    }

    return info;
}

std::vector<DeviceDisplayInfo> GstreamerFrameCapture::get_device_display_info() const
{
    try {
        return get_device_display_info_drm(dpy);
    } catch (const std::exception &e) {
        syslog(LOG_WARNING, "Failed to get device info using DRM: %s. Using no-DRM fallback.",
               e.what());
        return get_device_display_info_no_drm(dpy);
    }
}

FrameCapture *GstreamerPlugin::CreateCapture()
{
    return new GstreamerFrameCapture(settings);
}

unsigned GstreamerPlugin::Rank()
{
    return SoftwareMin;
}

void GstreamerPlugin::ParseOptions(const ConfigureOption *options)
{
    for (; options->name; ++options) {
        const std::string name = options->name;
        const std::string value = options->value;

        if (name == "framerate") {
            try {
                settings.fps = std::stoi(value);
            } catch (const std::exception &e) {
                throw std::runtime_error("Invalid value '" + value + "' for option 'framerate'.");
            }
        } else if (name == "gst.codec") {
            if (value == "h264") {
                settings.codec = SPICE_VIDEO_CODEC_TYPE_H264;
            } else if (value == "vp9") {
                settings.codec = SPICE_VIDEO_CODEC_TYPE_VP9;
            } else if (value == "vp8") {
                settings.codec = SPICE_VIDEO_CODEC_TYPE_VP8;
            } else if (value == "mjpeg") {
                settings.codec = SPICE_VIDEO_CODEC_TYPE_MJPEG;
            } else if (value == "h265") {
                settings.codec = SPICE_VIDEO_CODEC_TYPE_H265;
            } else {
                throw std::runtime_error("Invalid value '" + value + "' for option 'gst.codec'.");
            }
        } else if (name == "gst.encoder") {
            settings.encoder = value;
        } else if (name == "gst.prop") {
            size_t pos = value.find('=');
            if (pos == 0 || pos >= value.size() - 1) {
                gst_syslog(LOG_WARNING, "Property input is invalid ('%s' Ignored)", value.c_str());
                continue;
            }
            settings.prop_pairs.push_back(make_pair(value.substr(0, pos), value.substr(pos + 1)));
        }
    }
}

}}} //namespace spice::streaming_agent::gstreamer_plugin

using namespace spice::streaming_agent::gstreamer_plugin;

SPICE_STREAMING_AGENT_PLUGIN(agent)
{
    gst_init(nullptr, nullptr);

    auto plugin = std::make_shared<GstreamerPlugin>();

    plugin->ParseOptions(agent->Options());

    agent->Register(plugin);

    return true;
}
