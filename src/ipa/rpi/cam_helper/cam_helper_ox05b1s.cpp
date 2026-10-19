/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * Camera helper for the OmniVision OX05B1S RGB-IR sensor.
 */

#include <algorithm>
#include <cmath>
#include <stddef.h>
#include <stdint.h>

#include <libcamera/base/log.h>

#include "cam_helper.h"

using namespace RPiController;
using namespace libcamera;

namespace libcamera {
LOG_DECLARE_CATEGORY(IPARPI)
}

/*
 * The driver writes ((gain_code & 0xff) << 4) to the sensor's 16-bit AGAIN
 * register. A register value of 0x0100 (gain code 16) is unity gain, so the
 * gain code is 16 times the linear gain.
 */
static constexpr uint32_t minGainCode = 16;
static constexpr uint32_t maxGainCode = 255;

/*
 * The sensor requires the frame length (VTS) to exceed the exposure time by at
 * least this many lines. This matches OX05B1S_CTRL_EXPOSURE_MAX_VTS_OFFSET in
 * the kernel driver.
 */
static constexpr unsigned int frameIntegrationDiff = 30;

class CamHelperOx05b1s : public CamHelper
{
public:
	CamHelperOx05b1s();

	uint32_t gainCode(double gain) const override;
	double gain(uint32_t gainCode) const override;
	bool sensorEmbeddedDataPresent() const override;
	double getModeSensitivity(const CameraMode &mode) const override;
};

CamHelperOx05b1s::CamHelperOx05b1s()
	: CamHelper(nullptr, frameIntegrationDiff)
{
}

uint32_t CamHelperOx05b1s::gainCode(double gain) const
{
	unsigned int code = static_cast<unsigned int>(std::lround(gain * 16.0));

	return std::clamp(code, minGainCode, maxGainCode);
}

double CamHelperOx05b1s::gain(uint32_t gainCode) const
{
	gainCode = std::clamp(gainCode, minGainCode, maxGainCode);

	return gainCode / 16.0;
}

bool CamHelperOx05b1s::sensorEmbeddedDataPresent() const
{
	/* The OX05B1S driver does not expose an embedded data stream. */
	return false;
}

double CamHelperOx05b1s::getModeSensitivity([[maybe_unused]] const CameraMode &mode) const
{
	/* The sensor has a single full-resolution mode. */
	return 1.0;
}

static CamHelper *create()
{
	return new CamHelperOx05b1s();
}

static RegisterCamHelper reg("ox05b1s", &create);
