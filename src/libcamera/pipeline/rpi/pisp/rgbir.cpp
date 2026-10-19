/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * RGB-IR to standard Bayer conversion.
 *
 * The algorithm is a port of the "rgbir-fast" mode of the bayerir2bayer
 * GStreamer element. It processes 4x2 pixel groups and produces a standard
 * BGGR Bayer frame. The very last pixel of each line is intentionally left
 * untouched by the infrared compensation, matching the reference element.
 */

#include "rgbir.h"

#include <algorithm>
#include <cstdint>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace libcamera {

namespace RPi {

namespace {

/* Exact unsigned division by ten for 16-bit values. */
inline uint32_t div10(uint32_t value)
{
	return (value * 0xcccdU) >> 19;
}

inline uint16_t saturatingAdd(uint16_t value, uint32_t delta,
			      uint16_t maxValue)
{
	uint32_t result = value + delta;

	return std::min<uint32_t>(result, maxValue);
}

inline uint16_t saturatingSub(uint16_t value, uint32_t delta)
{
	return value > delta ? value - delta : 0;
}

struct Coefficients {
	uint32_t red;
	uint32_t green;
	uint32_t blue;
};

Coefficients getCoefficients(const NirParams &params)
{
	return {
		params.red,
		params.green,
		params.blue,
	};
}

inline uint32_t infrared(uint16_t a, uint16_t b, unsigned int shift)
{
	/* Estimate 1/10 of the average of the two IR pixels. */
	uint32_t value = div10((a >> (1 + shift)) + (b >> (1 + shift)));

	return value << shift;
}

/*
 * Process a 4x2 group. \a secondPair selects the colour order used by the
 * second pair of lines in the 4-line RGB-IR pattern.
 */
void processGroup(uint16_t *row0, uint16_t *row1, bool secondPair,
		  const NirParams &params, const Coefficients &coeffs,
		  unsigned int shift, uint16_t maxValue, unsigned int count)
{
	const uint16_t p00 = row0[0];
	const uint16_t p02 = row0[2];
	const uint16_t p10 = row1[0];
	const uint16_t p12 = row1[2];

	if (secondPair) {
		row0[0] = p02;
		row1[1] = p00;
		row1[3] = p00;
	} else {
		row0[2] = p00;
		row1[1] = p02;
		row1[3] = p02;
	}

	if (params.mode == NirMode::Off)
		return;

	const uint32_t ir = infrared(p10, p12, shift);
	const uint32_t irR = ir * coeffs.red;
	const uint32_t irG = ir * coeffs.green;
	const uint32_t irB = ir * coeffs.blue;

	uint16_t *rows[2] = { row0, row1 };
	const uint32_t irValues[2][4] = {
		{ irB, irG, irB, irG },
		{ irG, irR, irG, irR },
	};

	for (unsigned int r = 0; r < 2; r++) {
		for (unsigned int i = 0; i < count; i++) {
			if (params.mode == NirMode::Day)
				rows[r][i] = saturatingSub(rows[r][i],
							   irValues[r][i]);
			else
				rows[r][i] = saturatingAdd(rows[r][i],
							   irValues[r][i],
							   maxValue);
		}
	}
}

void processRowPair(uint16_t *row0, uint16_t *row1, unsigned int width,
		    bool secondPair, const NirParams &params,
		    unsigned int shift, uint16_t maxValue)
{
	Coefficients coeffs = getCoefficients(params);

	for (unsigned int x = 0; x + 4 <= width; x += 4) {
		unsigned int count = std::min(4u, width - 1 - x);

		processGroup(row0 + x, row1 + x, secondPair, params, coeffs,
			     shift, maxValue, count);
	}
}

} /* namespace */

void rgbIrToBayerReference(void *mem, unsigned int width, unsigned int height,
			   unsigned int stride, const NirParams &params,
			   unsigned int bitDepth)
{
	if (width % 4 || height % 2 || bitDepth < 8 || bitDepth > 16)
		return;

	const unsigned int shift = 16 - bitDepth;
	const uint16_t maxValue = static_cast<uint16_t>(((1U << bitDepth) - 1)
							<< shift);

	for (unsigned int y = 0; y < height; y += 2) {
		uint8_t *base = static_cast<uint8_t *>(mem) + y * stride;
		uint16_t *row0 = reinterpret_cast<uint16_t *>(base);
		uint16_t *row1 = reinterpret_cast<uint16_t *>(base + stride);

		processRowPair(row0, row1, width, (y & 2) != 0, params,
			       shift, maxValue);
	}
}

#if defined(__aarch64__)

namespace {

void processRowPairNeon(uint16_t *row0, uint16_t *row1, unsigned int width,
			bool secondPair, const NirParams &params,
			unsigned int shift, uint16_t maxValue)
{
	const uint32_t red = params.red;
	const uint32_t green = params.green;
	const uint32_t blue = params.blue;

	unsigned int x = 0;

	/*
	 * Leave at least one 4-pixel group for the scalar tail so that the
	 * final pixel of the line keeps the reference behaviour.
	 */
	for (; x + 8 <= width - 4; x += 8) {
		uint16x8_t v0 = vld1q_u16(row0 + x);
		uint16x8_t v1 = vld1q_u16(row1 + x);

		uint16_t *n0 = row0 + x;
		uint16_t *n1 = row1 + x;

		if (secondPair) {
			const uint16_t b0 = vgetq_lane_u16(v0, 0);
			const uint16_t b4 = vgetq_lane_u16(v0, 4);
			const uint16_t b2 = vgetq_lane_u16(v0, 2);
			const uint16_t b6 = vgetq_lane_u16(v0, 6);

			n1[1] = b0;
			n1[3] = b0;
			n1[5] = b4;
			n1[7] = b4;
			n0[0] = b2;
			n0[4] = b6;

			v0 = vld1q_u16(n0);
			v1 = vld1q_u16(n1);
		} else {
			const uint16_t b0 = vgetq_lane_u16(v0, 0);
			const uint16_t b4 = vgetq_lane_u16(v0, 4);
			const uint16_t b2 = vgetq_lane_u16(v0, 2);
			const uint16_t b6 = vgetq_lane_u16(v0, 6);

			n1[1] = b2;
			n1[3] = b2;
			n1[5] = b6;
			n1[7] = b6;
			n0[2] = b0;
			n0[6] = b4;

			v0 = vld1q_u16(n0);
			v1 = vld1q_u16(n1);
		}

		if (params.mode != NirMode::Off) {
			const uint32_t ir0 = infrared(n1[0], n1[2], shift);
			const uint32_t ir1 = infrared(n1[4], n1[6], shift);

			const uint32_t irR0 = ir0 * red;
			const uint32_t irG0 = ir0 * green;
			const uint32_t irB0 = ir0 * blue;
			const uint32_t irR1 = ir1 * red;
			const uint32_t irG1 = ir1 * green;
			const uint32_t irB1 = ir1 * blue;

			uint16x4_t lo0 = { static_cast<uint16_t>(irB0),
					   static_cast<uint16_t>(irG0),
					   static_cast<uint16_t>(irB0),
					   static_cast<uint16_t>(irG0) };
			uint16x4_t hi0 = { static_cast<uint16_t>(irB1),
					   static_cast<uint16_t>(irG1),
					   static_cast<uint16_t>(irB1),
					   static_cast<uint16_t>(irG1) };
			uint16x4_t lo1 = { static_cast<uint16_t>(irG0),
					   static_cast<uint16_t>(irR0),
					   static_cast<uint16_t>(irG0),
					   static_cast<uint16_t>(irR0) };
			uint16x4_t hi1 = { static_cast<uint16_t>(irG1),
					   static_cast<uint16_t>(irR1),
					   static_cast<uint16_t>(irG1),
					   static_cast<uint16_t>(irR1) };

			uint16x8_t irV0 = vcombine_u16(lo0, hi0);
			uint16x8_t irV1 = vcombine_u16(lo1, hi1);

			if (params.mode == NirMode::Day) {
				v0 = vqsubq_u16(v0, irV0);
				v1 = vqsubq_u16(v1, irV1);
			} else {
				uint16x8_t maxV = vdupq_n_u16(maxValue);
				v0 = vminq_u16(vqaddq_u16(v0, irV0), maxV);
				v1 = vminq_u16(vqaddq_u16(v1, irV1), maxV);
			}
		}

		vst1q_u16(n0, v0);
		vst1q_u16(n1, v1);
	}

	/* Process the remaining groups with the reference implementation. */
	Coefficients coeffs = getCoefficients(params);
	for (; x + 4 <= width; x += 4) {
		unsigned int count = std::min(4u, width - 1 - x);

		processGroup(row0 + x, row1 + x, secondPair, params, coeffs,
			     shift, maxValue, count);
	}
}

} /* namespace */

#endif /* __aarch64__ */

void rgbIrToBayer(void *mem, unsigned int width, unsigned int height,
		  unsigned int stride, const NirParams &params,
		  unsigned int bitDepth)
{
	if (width % 4 || height % 2 || bitDepth < 8 || bitDepth > 16)
		return;

#if defined(__aarch64__)
	const unsigned int shift = 16 - bitDepth;
	const uint16_t maxValue = static_cast<uint16_t>(((1U << bitDepth) - 1)
							<< shift);

	for (unsigned int y = 0; y < height; y += 2) {
		uint8_t *base = static_cast<uint8_t *>(mem) + y * stride;
		uint16_t *row0 = reinterpret_cast<uint16_t *>(base);
		uint16_t *row1 = reinterpret_cast<uint16_t *>(base + stride);

		processRowPairNeon(row0, row1, width, (y & 2) != 0, params,
				   shift, maxValue);
	}
#else
	rgbIrToBayerReference(mem, width, height, stride, params, bitDepth);
#endif
}

} /* namespace RPi */

} /* namespace libcamera */
