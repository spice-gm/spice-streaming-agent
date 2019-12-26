/*
 * Include catch/catch.hpp or catch2/catch.hpp
 * according to what configure found
 *
 * \copyright
 * Copyright 2019 Red Hat Inc. All rights reserved.
 */

#pragma once
#include <config.h>

#if   defined(HAVE_CATCH2_CATCH_HPP)
#include <catch2/catch.hpp>
#elif defined(HAVE_CATCH_CATCH_HPP)
#include <catch/catch.hpp>
#endif
