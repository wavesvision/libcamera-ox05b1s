/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * Near-infrared processing parameters shared by the Raspberry Pi pipeline
 * handlers.
 */

#pragma once

#include <array>
#include <cstdint>

namespace libcamera {

namespace RPi {

enum class NirMode : uint8_t {
	Off = 0,
	Day = 1,
	Night = 2,
};

struct NirParams {
	NirMode mode;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct NirDefaults {
	NirMode mode;
	std::array<uint8_t, 3> day;
	std::array<uint8_t, 3> night;
};

inline NirParams nirParams(NirMode mode, const NirDefaults &defaults)
{
	const std::array<uint8_t, 3> &coefficients =
		mode == NirMode::Night ? defaults.night : defaults.day;

	return { mode, coefficients[0], coefficients[1], coefficients[2] };
}

} /* namespace RPi */

} /* namespace libcamera */
