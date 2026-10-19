/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * Database of camera sensor properties
 */

#pragma once

#include <map>
#include <stdint.h>
#include <string>

#include <libcamera/control_ids.h>
#include <libcamera/geometry.h>

namespace libcamera {

struct CameraSensorProperties {
	struct SensorDelays {
		uint8_t exposureDelay;
		uint8_t gainDelay;
		uint8_t vblankDelay;
		uint8_t hblankDelay;
	};

	static const CameraSensorProperties *get(const std::string &sensor);

	Size unitCellSize;
	std::map<controls::draft::TestPatternModeEnum, int32_t> testPatternModes;
	SensorDelays sensorDelays;
	/*
	 * Set for sensors whose colour filter array contains infrared
	 * sensitive pixels. Such sensors require a software RGB-IR to Bayer
	 * conversion before the image can be processed by the ISP.
	 */
	bool isRgbIr = false;
};

} /* namespace libcamera */
