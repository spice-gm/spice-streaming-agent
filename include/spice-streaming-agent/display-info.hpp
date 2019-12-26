/* \copyright
 * Copyright 2018 Red Hat Inc. All rights reserved.
 */

#pragma once

#include <vector>
#include <string>


namespace spice __attribute__ ((visibility ("default"))) {
namespace streaming_agent {

/**
 * Lists graphics cards listed in the DRM sybsystem in /sys/class/drm.
 * Throws an instance of Error in case of an I/O error.
 *
 * @return a vector of paths of all graphics cards present in /sys/class/drm
 */
std::vector<std::string> list_cards();

/**
 * Reads a single number in hex format from a file.
 * Throws an instance of Error in case of an I/O or parsing error.
 *
 * @param path the path to the file
 * @return the number parsed from the file
 */
uint32_t read_hex_number_from_file(const std::string &path);

/**
 * Resolves any symlinks and then extracts the PCI path from the canonical path
 * to a card. Returns the path in the following format:
 * "pci/<DOMAIN>/<SLOT>.<FUNCTION>/.../<SLOT>.<FUNCTION>"
 *
 * <DOMAIN> is the PCI domain, followed by <SLOT>.<FUNCTION> of any PCI bridges
 * in the chain leading to the device. The last <SLOT>.<FUNCTION> is the
 * graphics device. All of <DOMAIN>, <SLOT>, <FUNCTION> are hexadecimal numbers
 * with the following number of digits:
 *   <DOMAIN>: 4
 *   <SLOT>: 2
 *   <FUNCTION>: 1
 *
 * @param device_path the path to the card
 * @return the device address
 */
std::string get_device_address(const std::string &card_path);

}} // namespace spice::streaming_agent
