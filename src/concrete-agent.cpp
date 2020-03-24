/* Implementation of the agent
 *
 * \copyright
 * Copyright 2017 Red Hat Inc. All rights reserved.
 */

#include <config.h>
#include "concrete-agent.hpp"
#include "frame-log.hpp"

#include <algorithm>
#include <syslog.h>
#include <glob.h>
#include <dlfcn.h>
#include <string>
#include <cstdarg>
#include <stdexcept>

using namespace spice::streaming_agent;

static inline unsigned MajorVersion(unsigned version)
{
    return version >> 8;
}

static inline unsigned MinorVersion(unsigned version)
{
    return version & 0xffu;
}

ConcreteAgent::ConcreteAgent(const std::vector<ConcreteConfigureOption> &options, FrameLog *logger):
    options(options),
    logger(logger)
{
    this->options.push_back(ConcreteConfigureOption(nullptr, nullptr));
}

bool ConcreteAgent::PluginVersionIsCompatible(unsigned pluginVersion) const
{
    unsigned version = PluginVersion;
    return MajorVersion(version) == MajorVersion(pluginVersion) &&
        MinorVersion(version) >= MinorVersion(pluginVersion);
}

void ConcreteAgent::Register(const std::shared_ptr<Plugin>& plugin)
{
    plugins.push_back(plugin);
}

const ConfigureOption* ConcreteAgent::Options() const
{
    static_assert(sizeof(ConcreteConfigureOption) == sizeof(ConfigureOption),
                  "ConcreteConfigureOption should be binary compatible with ConfigureOption");
    return static_cast<const ConfigureOption*>(&options[0]);
}

void ConcreteAgent::LoadPlugins(const std::string &directory)
{
    std::string pattern = directory + "/*.so";
    glob_t globbuf;

    int glob_result = glob(pattern.c_str(), 0, NULL, &globbuf);
    if (glob_result == GLOB_NOMATCH)
        return;
    if (glob_result != 0) {
        syslog(LOG_ERR, "glob FAILED with %d", glob_result);
        return;
    }

    for (size_t n = 0; n < globbuf.gl_pathc; ++n) {
        LoadPlugin(globbuf.gl_pathv[n]);
    }
    globfree(&globbuf);
}

void ConcreteAgent::LoadPlugin(const std::string &plugin_filename)
{
    void *dl = dlopen(plugin_filename.c_str(), RTLD_LOCAL|RTLD_NOW);
    if (!dl) {
        syslog(LOG_ERR, "error loading plugin %s: %s",
               plugin_filename.c_str(), dlerror());
        return;
    }

    unsigned *version =
        (unsigned *) dlsym(dl, "spice_streaming_agent_plugin_interface_version");
    if (!version) {
        syslog(LOG_ERR, "error loading plugin %s: no version information",
               plugin_filename.c_str());
        dlclose(dl);
        return;
    }
    if (!PluginVersionIsCompatible(*version)) {
        syslog(LOG_ERR,
               "error loading plugin %s: plugin interface version %u.%u not accepted",
               plugin_filename.c_str(),
               MajorVersion(*version), MinorVersion(*version));
        dlclose(dl);
        return;
    }

    try {
        PluginInitFunc* init_func =
            (PluginInitFunc *) dlsym(dl, "spice_streaming_agent_plugin_init");
        if (!init_func || !init_func(this)) {
            dlclose(dl);
        }
    }
    catch (std::runtime_error &err) {
        syslog(LOG_ERR, "%s", err.what());
        dlclose(dl);
    }
}

FrameCapture *ConcreteAgent::GetBestFrameCapture(const std::set<SpiceVideoCodecType>& codecs)
{
    std::vector<std::pair<unsigned, std::shared_ptr<Plugin>>> sorted_plugins;

    // sort plugins base on ranking, reverse order
    for (const auto& plugin: plugins) {
        sorted_plugins.push_back(make_pair(plugin->Rank(), plugin));
    }
    sort(sorted_plugins.rbegin(), sorted_plugins.rend());

    // return first not null
    for (const auto& plugin: sorted_plugins) {
        if (plugin.first == DontUse) {
            break;
        }
        // check client supports the codec
        if (codecs.find(plugin.second->VideoCodecType()) == codecs.end())
            continue;

        FrameCapture *capture;
        try {
            capture = plugin.second->CreateCapture();
        } catch (const std::exception &err) {
            syslog(LOG_ERR, "Error creating capture engine: %s", err.what());
            continue;
        }
        if (capture) {
            return capture;
        }
    }
    return nullptr;
}

void ConcreteAgent::LogStat(const char* format, ...)
{
    if (logger) {
        va_list ap;
        va_start(ap, format);
        logger->log_statv(format, ap);
        va_end(ap);
    }
}
