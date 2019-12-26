/* Agent implementation
 *
 * \copyright
 * Copyright 2017 Red Hat Inc. All rights reserved.
 */

#pragma once

#include <vector>
#include <set>
#include <memory>
#include <spice-streaming-agent/plugin.hpp>

namespace spice {
namespace streaming_agent {

class FrameLog;

struct ConcreteConfigureOption: ConfigureOption
{
    ConcreteConfigureOption(const char *name, const char *value)
    {
        this->name = name;
        this->value = value;
    }
};

class ConcreteAgent final : public Agent
{
public:
    ConcreteAgent(const std::vector<ConcreteConfigureOption> &options,
                  FrameLog *logger=nullptr);
    void Register(const std::shared_ptr<Plugin>& plugin) override;
    const ConfigureOption* Options() const override;
    void LoadPlugins(const std::string &directory);
    FrameCapture *GetBestFrameCapture(const std::set<SpiceVideoCodecType>& codecs);
    __attribute__ ((format (printf, 2, 3)))
    void LogStat(const char* format, ...) override;
private:
    bool PluginVersionIsCompatible(unsigned pluginVersion) const;
    void LoadPlugin(const std::string &plugin_filename);
    std::vector<std::shared_ptr<Plugin>> plugins;
    std::vector<ConcreteConfigureOption> options;
    FrameLog *const logger = nullptr;
};

}} // namespace spice::streaming_agent
