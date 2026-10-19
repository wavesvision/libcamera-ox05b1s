/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * RGB-IR to standard Bayer conversion.
 *
 * The OX05B1S 4x4 colour filter array has the following layout:
 *
 *     R  G  R  G
 *     G  B  G  B
 *     IR G  IR G
 *     G  B  G  B
 *
 * The IR row is replaced with the red/green row above it, producing a standard
 * RGGB Bayer frame. The infrared component is estimated from the IR pixels
 * before they are overwritten and can be subtracted (day) or added (night) to
 * the colour channels.
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

inline Coefficients getCoefficients(const NirParams &params)
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

inline uint32_t irValue(uint32_t ir, uint32_t coefficient)
{
	return ir * coefficient;
}

inline uint16_t adjust(uint16_t value, uint32_t ir, NirMode mode,
		       uint16_t maxValue)
{
	if (mode == NirMode::Day)
		return saturatingSub(value, ir);

	return saturatingAdd(value, ir, maxValue);
}

/*
 * Process one 4x4 block. The IR pixels are in the third row, at even
 * columns. They are read before row 0 is copied over that row.
 */
void processQuad(uint16_t *row0, uint16_t *row1, uint16_t *row2,
		 uint16_t *row3, unsigned int width, const NirParams &params,
		 const Coefficients &coeffs, unsigned int shift,
		 uint16_t maxValue)
{
	for (unsigned int x = 0; x + 4 <= width; x += 4) {
		uint32_t ir = 0;
		uint32_t irR = 0;
		uint32_t irG = 0;
		uint32_t irB = 0;

		if (params.mode != NirMode::Off) {
			ir = infrared(row2[x], row2[x + 2], shift);
			irR = irValue(ir, coeffs.red);
			irG = irValue(ir, coeffs.green);
			irB = irValue(ir, coeffs.blue);
		}

		/* Replace the IR row with the red/green row above it. */
		row2[x] = row0[x];
		row2[x + 1] = row0[x + 1];
		row2[x + 2] = row0[x + 2];
		row2[x + 3] = row0[x + 3];

		if (params.mode == NirMode::Off)
			continue;

		/* Rows 0 and 2 are R G R G, rows 1 and 3 are G B G B. */
		for (unsigned int i = 0; i < 4; i++) {
			uint16_t *rows[4] = { row0, row1, row2, row3 };
			uint32_t channel[4] = {
				(i & 1) ? irG : irR,
				(i & 1) ? irB : irG,
				(i & 1) ? irG : irR,
				(i & 1) ? irB : irG,
			};

			for (unsigned int row = 0; row < 4; row++)
				rows[row][x + i] =
					adjust(rows[row][x + i], channel[row],
					       params.mode, maxValue);
		}
	}
}

} /* namespace */

void rgbIrToBayerReference(void *mem, unsigned int width, unsigned int height,
			   unsigned int stride, const NirParams &params,
			   unsigned int bitDepth)
{
	if (width % 4 || height % 4 || bitDepth < 8 || bitDepth > 16)
		return;

	const unsigned int shift = 16 - bitDepth;
	const uint16_t maxValue = static_cast<uint16_t>(((1U << bitDepth) - 1)
							<< shift);
	Coefficients coeffs = getCoefficients(params);

	for (unsigned int y = 0; y < height; y += 4) {
		uint8_t *base = static_cast<uint8_t *>(mem) + y * stride;
		uint16_t *row0 = reinterpret_cast<uint16_t *>(base);
		uint16_t *row1 = reinterpret_cast<uint16_t *>(base + stride);
		uint16_t *row2 = reinterpret_cast<uint16_t *>(base + 2 * stride);
		uint16_t *row3 = reinterpret_cast<uint16_t *>(base + 3 * stride);

		processQuad(row0, row1, row2, row3, width, params, coeffs,
			    shift, maxValue);
	}
}

#if defined(__aarch64__)

namespace {

void processQuadNeon(uint16_t *row0, uint16_t *row1, uint16_t *row2,
		     uint16_t *row3, unsigned int width, const NirParams &params,
		     unsigned int shift, uint16_t maxValue)
{
	unsigned int x = 0;

	for (; x + 8 <= width; x += 8) {
		uint16x8_t v0 = vld1q_u16(row0 + x);
		uint16x8_t v1 = vld1q_u16(row1 + x);
		uint16x8_t v2 = vld1q_u16(row2 + x);
		uint16x8_t v3 = vld1q_u16(row3 + x);

		if (params.mode == NirMode::Off) {
			vst1q_u16(row2 + x, v0);
			continue;
		}

		const uint32_t ir0 = infrared(vgetq_lane_u16(v2, 0),
					      vgetq_lane_u16(v2, 2), shift);
		const uint32_t ir1 = infrared(vgetq_lane_u16(v2, 4),
					      vgetq_lane_u16(v2, 6), shift);

		const uint32_t irR0 = irValue(ir0, params.red);
		const uint32_t irG0 = irValue(ir0, params.green);
		const uint32_t irB0 = irValue(ir0, params.blue);
		const uint32_t irR1 = irValue(ir1, params.red);
		const uint32_t irG1 = irValue(ir1, params.green);
		const uint32_t irB1 = irValue(ir1, params.blue);

		/* Replace the IR row with the red/green row above it. */
		vst1q_u16(row2 + x, v0);

		uint16x4_t rg0 = { static_cast<uint16_t>(irR0),
				   static_cast<uint16_t>(irG0),
				   static_cast<uint16_t>(irR0),
				   static_cast<uint16_t>(irG0) };
		uint16x4_t rg1 = { static_cast<uint16_t>(irR1),
				   static_cast<uint16_t>(irG1),
				   static_cast<uint16_t>(irR1),
				   static_cast<uint16_t>(irG1) };
		uint16x4_t gb0 = { static_cast<uint16_t>(irG0),
				   static_cast<uint16_t>(irB0),
				   static_cast<uint16_t>(irG0),
				   static_cast<uint16_t>(irB0) };
		uint16x4_t gb1 = { static_cast<uint16_t>(irG1),
				   static_cast<uint16_t>(irB1),
				   static_cast<uint16_t>(irG1),
				   static_cast<uint16_t>(irB1) };

		uint16x8_t irRG = vcombine_u16(rg0, rg1);
		uint16x8_t irGB = vcombine_u16(gb0, gb1);
		uint16x8_t orig0 = v0;

		if (params.mode == NirMode::Day) {
			v0 = vqsubq_u16(v0, irRG);
			v1 = vqsubq_u16(v1, irGB);
			v2 = vqsubq_u16(orig0, irRG);
			v3 = vqsubq_u16(v3, irGB);
		} else {
			uint16x8_t maxV = vdupq_n_u16(maxValue);

			v0 = vminq_u16(vqaddq_u16(v0, irRG), maxV);
			v1 = vminq_u16(vqaddq_u16(v1, irGB), maxV);
			v2 = vminq_u16(vqaddq_u16(orig0, irRG), maxV);
			v3 = vminq_u16(vqaddq_u16(v3, irGB), maxV);
		}

		vst1q_u16(row0 + x, v0);
		vst1q_u16(row1 + x, v1);
		vst1q_u16(row2 + x, v2);
		vst1q_u16(row3 + x, v3);
	}

	/* Process the remaining pixels with the scalar implementation. */
	if (x < width) {
		Coefficients coeffs = getCoefficients(params);

		processQuad(row0 + x, row1 + x, row2 + x, row3 + x,
			    width - x, params, coeffs, shift, maxValue);
	}
}

} /* namespace */

#endif /* __aarch64__ */

void rgbIrToBayer(void *mem, unsigned int width, unsigned int height,
		  unsigned int stride, const NirParams &params,
		  unsigned int bitDepth)
{
	if (width % 4 || height % 4 || bitDepth < 8 || bitDepth > 16)
		return;

#if defined(__aarch64__)
	const unsigned int shift = 16 - bitDepth;
	const uint16_t maxValue = static_cast<uint16_t>(((1U << bitDepth) - 1)
							<< shift);

	for (unsigned int y = 0; y < height; y += 4) {
		uint8_t *base = static_cast<uint8_t *>(mem) + y * stride;
		uint16_t *row0 = reinterpret_cast<uint16_t *>(base);
		uint16_t *row1 = reinterpret_cast<uint16_t *>(base + stride);
		uint16_t *row2 = reinterpret_cast<uint16_t *>(base + 2 * stride);
		uint16_t *row3 = reinterpret_cast<uint16_t *>(base + 3 * stride);

		processQuadNeon(row0, row1, row2, row3, width, params, shift,
				maxValue);
	}
#else
	rgbIrToBayerReference(mem, width, height, stride, params, bitDepth);
#endif
}

} /* namespace RPi */

} /* namespace libcamera */
