/* Hex dump utility
 *
 * \copyright
 * Copyright 2016-2017 Red Hat Inc. All rights reserved.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

void hexdump(const void *buffer, size_t size, FILE *f_out);

#ifdef __cplusplus
}
#endif
