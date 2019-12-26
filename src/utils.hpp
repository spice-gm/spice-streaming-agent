/* \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include <vector>
#include <string>
#include <syslog.h>


namespace spice {
namespace streaming_agent {
namespace utils {

std::vector<std::string> glob(const std::string& pattern);

template<class T>
const T &syslog(const T &error) noexcept
{
    ::syslog(LOG_ERR, "%s\n", error.what());
    return error;
}

}}} // namespace spice::streaming_agent::utils
