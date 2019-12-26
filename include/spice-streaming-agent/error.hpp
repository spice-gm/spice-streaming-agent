/* The errors module.
 *
 * \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include <stdexcept>
#include <string>


namespace spice __attribute__ ((visibility ("default"))) {
namespace streaming_agent {

class Error : public std::runtime_error
{
public:
    Error(const std::string &message) : std::runtime_error(message) {}
};

}} // namespace spice::streaming_agent
