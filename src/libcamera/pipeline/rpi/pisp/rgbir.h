/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * RGB-IR to standard Bayer conversion for the Raspberry Pi PiSP pipeline.
 */

#pragma once

#include "../common/nir.h"

namespace libcamera {

namespace RPi {

/*
 * Convert an unpacked 16-bit RGGB RGB-IR frame to a standard RGGB Bayer frame
 * in place. The frame must have no horizontal or vertical flips applied. The
 * \a bitDepth parameter is the native sensor bit depth (the samples are
 * assumed to be left-aligned in the 16-bit words).
 */
void rgbIrToBayer(void *mem, unsigned int width, unsigned int height,
		  unsigned int stride, const NirParams &params,
		  unsigned int bitDepth);

/*
 * Scalar reference implementation, used by the unit tests and as the generic
 * fallback on non-AArch64 platforms.
 */
void rgbIrToBayerReference(void *mem, unsigned int width, unsigned int height,
			   unsigned int stride, const NirParams &params,
			   unsigned int bitDepth);

} /* namespace RPi */

} /* namespace libcamera */
