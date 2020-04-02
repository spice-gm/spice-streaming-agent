/* Plugin implementation for gstreamer encoder
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#include <config.h>
#include <cstring>
#include <sstream>
#include <exception>
#include <stdexcept>
#include <memory>
#include <map>
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
    SpiceVideoCodecType codec = SPICE_VIDEO_CODEC_TYPE_VP8;
    std::string encoder;
    std::map<std::string, std::string> enc_props;
};

#define DECLARE_UPTR(type, func) \
    struct type##Deleter {       \
        void operator()(type* p) \
        {                        \
            func(p);             \
        }                        \
    };                           \
    using type##UPtr = std::unique_ptr<type, type##Deleter>;

DECLARE_UPTR(GstBuffer, gst_buffer_unref)
DECLARE_UPTR(GstCaps, gst_caps_unref)
DECLARE_UPTR(GstSample, gst_sample_unref)
DECLARE_UPTR(GstElement, gst_object_unref)

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
#endif
    GstElementUPtr pipeline, capture, sink;
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
    void ParseOptions(const ConfigureOption *options, const std::string &codec_name,
                      const std::string &encoder_cfg);
    SpiceVideoCodecType VideoCodecType() const override {
        return settings.codec;
    }
private:
    void StoreEncodingOptions(const std::string &encoder_options);
    bool StorePluginOption(const std::string &name, const std::string &value);
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
                       "Specified encoder named '%s' cannot produce '%s' streams. Make sure that "
                       "gst.CODEC=ENCODER is correctly specified and that the encoder is available.",
                       settings.encoder.c_str(), caps_str.get());
        }
        factory = factory ? factory : (GstElementFactory*)filtered->data;
        gst_syslog(LOG_NOTICE, "'%s' encoder plugin is used", GST_ELEMENT_NAME(factory));

    } else {
        gst_syslog(LOG_ERR, "No suitable encoder was found for '%s'", caps_str.get());
    }

    encoder = factory ? gst_element_factory_create(factory, "encoder") : nullptr;
    if (encoder) { // Set encoder properties
        for (const auto &prop : settings.enc_props) {
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
void gst_bin_add(GstBin *bin, const GstElementUPtr &elem)
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

    GstElementUPtr pipeline(gst_pipeline_new("pipeline"));
    if (!pipeline) {
        throw std::runtime_error("Gstreamer's pipeline element cannot be created");
    }
    GstElementUPtr capture(get_capture_plugin(settings));
    if (!capture) {
        throw std::runtime_error("Gstreamer's capture element cannot be created");
    }
    GstElementUPtr convert(gst_element_factory_make("autovideoconvert", "convert"));
    if (!convert) {
        throw std::runtime_error("Gstreamer's 'autovideoconvert' element cannot be created");
    }
    GstCapsUPtr sink_caps;
    GstElementUPtr encoder(get_encoder_plugin(settings, sink_caps));
    if (!encoder) {
        throw std::runtime_error("Gstreamer's encoder element cannot be created");
    }
    GstElementUPtr sink(gst_element_factory_make("appsink", "sink"));
    if (!sink) {
        throw std::runtime_error("Gstreamer's appsink element cannot be created");
    }

    g_object_set(sink.get(),
                 "sync", FALSE,
                 "drop", FALSE,
                 "max-buffers", 1,
                 nullptr);

    GstBin *bin = GST_BIN(pipeline.get());
    gst_bin_add(bin, capture);
    gst_bin_add(bin, convert);
    gst_bin_add(bin, encoder);
    gst_bin_add(bin, sink);

    GstCapsUPtr caps(gst_caps_from_string("video/x-raw(ANY)"));
    GstCapsUPtr convert_caps(gst_caps_new_simple("video/x-raw",
                                                 "framerate", GST_TYPE_FRACTION, settings.fps, 1,
                                                 nullptr));
    link = gst_element_link_filtered(capture.get(), convert.get(), convert_caps.get()) &&
           gst_element_link_filtered(convert.get(), encoder.get(), caps.get()) &&
           gst_element_link_filtered(encoder.get(), sink.get(), sink_caps.get());
    if (!link) {
        throw std::runtime_error("Linking gstreamer's elements failed");
    }

    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG_BIN_TO_DOT_FILE(bin, GST_DEBUG_GRAPH_SHOW_VERBOSE, "gst-plugin-pipeline-debug");
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
void free_ximage(gpointer data)
{
    XImage *image = (XImage*)data;
    image->f.destroy_image(image);
}

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

    XImage *image = XGetImage(dpy, win, 0, 0,
                              cur_width, cur_height, AllPlanes, ZPixmap);
    if (!image) {
        throw std::runtime_error("Cannot capture from X");
    }

    GstBufferUPtr buf(gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, image->data,
                                                  image->height * image->bytes_per_line, 0,
                                                  image->height * image->bytes_per_line, image,
                                                  free_ximage));
    if (!buf) {
        throw std::runtime_error("Failed to wrap image in gstreamer buffer");
    }

    GstCapsUPtr caps(gst_caps_new_simple("video/x-raw",
                                         "format", G_TYPE_STRING, "BGRx",
                                         "width", G_TYPE_INT, image->width,
                                         "height", G_TYPE_INT, image->height,
                                         "framerate", GST_TYPE_FRACTION, settings.fps, 1,
                                         nullptr));

    // Push sample (gst_app_src_push_sample does not take buffer ownership)
    GstSampleUPtr appsrc_sample(gst_sample_new(buf.get(), caps.get(), nullptr, nullptr));
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

bool GstreamerPlugin::StorePluginOption(const std::string &name, const std::string &value)
{

    if (name == "framerate") {
        try {
            settings.fps = std::stoi(value);
            return true;
        } catch (const std::exception &e) {
            throw std::runtime_error("Invalid value '" + value + "' for option 'framerate'.");
        }
    }

    return false;
}

void GstreamerPlugin::StoreEncodingOptions(const std::string &encoder_options)
{
    std::stringstream encoder_options_ss(encoder_options);
    std::string option_token;

    while (std::getline(encoder_options_ss, option_token, ',')) {
        size_t has_sep = option_token.find('=');
        if (has_sep == std::string::npos) {
            throw std::runtime_error("Invalid parameter for GStreamer encoder '" + settings.encoder
                                     + "': " + "separator not found in '" + option_token + "'.");

        }
        std::string name = option_token.substr(0, has_sep);
        std::string value = option_token.substr(has_sep + 1);

        if (StorePluginOption(name, value)) {
            continue;
        }

        settings.enc_props[name] = value;
    }
}

void GstreamerPlugin::ParseOptions(const ConfigureOption *options, const std::string &codec_name,
                                   const std::string &encoder_cfg)
{
    if (codec_name == "h264") {
        settings.codec = SPICE_VIDEO_CODEC_TYPE_H264;
    } else if (codec_name == "vp9") {
        settings.codec = SPICE_VIDEO_CODEC_TYPE_VP9;
    } else if (codec_name == "vp8") {
        settings.codec = SPICE_VIDEO_CODEC_TYPE_VP8;
    } else if (codec_name == "mjpeg") {
        settings.codec = SPICE_VIDEO_CODEC_TYPE_MJPEG;
    } else if (codec_name == "h265") {
        settings.codec = SPICE_VIDEO_CODEC_TYPE_H265;
    } else {
        throw std::runtime_error("Invalid value '" + codec_name + "' for GStreamer codec.");
    }

    size_t config_sep_pos = encoder_cfg.find(':');

    if (config_sep_pos == std::string::npos) {
        config_sep_pos = encoder_cfg.length();
    }

    settings.encoder = encoder_cfg.substr(0, config_sep_pos);

    if (settings.encoder.empty()) {
         throw std::runtime_error("Invalid GStreamer parameter 'gst." + codec_name + "=" +
                                  encoder_cfg + "': encoder cannot be empty. " +
                                  "Use 'auto' to pick up GST default encoder.");
    }

    if (settings.encoder == "auto") {
        if (config_sep_pos != encoder_cfg.length()) {
            throw std::runtime_error("Invalid parameter 'gst." + codec_name + "=" + encoder_cfg +
                                     "': automatically-selected encoder cannot be configured.");
        }
        settings.encoder = "";
    }

    for (; options->name; ++options) {
        StorePluginOption(options->name, options->value);
    }

    if (config_sep_pos == encoder_cfg.length()) {
        return;
    }

    std::string encoder_options(encoder_cfg.substr(config_sep_pos + 1));

    StoreEncodingOptions(encoder_options);
}

}}} //namespace spice::streaming_agent::gstreamer_plugin

using namespace spice::streaming_agent::gstreamer_plugin;

SPICE_STREAMING_AGENT_PLUGIN(agent)
{
    const std::string gst_prefix = "gst.";
    auto options = agent->Options();
    bool registered = false;

    gst_init(nullptr, nullptr);

    for (; options->name; ++options) {
        const std::string name = options->name;
        const std::string value = options->value;

        if (name.rfind(gst_prefix, 0) == 0) {
            auto plugin = std::make_shared<GstreamerPlugin>();
            const std::string codec_name = name.substr(gst_prefix.length());

            plugin->ParseOptions(agent->Options(), codec_name, value);
            agent->Register(plugin);
            registered = true;
        }
    }

    if (!registered) {
        auto plugin = std::make_shared<GstreamerPlugin>();
        plugin->ParseOptions(agent->Options(), "vp8", "auto");
        agent->Register(plugin);
    }

    return true;
}
