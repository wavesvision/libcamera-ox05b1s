/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * Test the RGB-IR to Bayer converter against a direct port of the
 * bayerir2bayer "rgbir-fast" reference implementation.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "../../src/libcamera/pipeline/rpi/pisp/rgbir.h"

using namespace libcamera::RPi;

namespace {

constexpr uint64_t M1 = 0x000000000000ffffULL;
constexpr uint64_t M3 = 0x0000ffff00000000ULL;
constexpr uint64_t M31 = 0x0000ffff0000ffffULL;
constexpr uint64_t M421 = 0xffff0000ffffffffULL;
constexpr uint64_t M432 = 0xffffffffffff0000ULL;

/*
 * Direct port of gstbayer2bayer's gst_bayerir2bayer_process_16b_fast() for
 * the BGGR case. The source is a 10-bit image stored in a 16-bit container.
 */
void reference(const uint8_t *src, uint8_t *dst, unsigned int width,
	       unsigned int height, unsigned int stride, NirMode mode,
	       unsigned int red, unsigned int green, unsigned int blue,
	       unsigned int shift)
{
	const uint16_t maxValue = static_cast<uint16_t>(((1U << (16 - shift)) - 1) << shift);
	std::vector<uint64_t> ir0(width / 4);
	std::vector<uint64_t> ir1(width / 4);

	for (unsigned int y = 0; y < height; y += 2) {
		const uint16_t *s0 = reinterpret_cast<const uint16_t *>(src + y * stride);
		const uint16_t *s1 = reinterpret_cast<const uint16_t *>(src + (y + 1) * stride);
		uint16_t *d0 = reinterpret_cast<uint16_t *>(dst + y * stride);
		uint16_t *d1 = reinterpret_cast<uint16_t *>(dst + (y + 1) * stride);

		for (unsigned int x = 0, n = 0; x < width; x += 4, n++) {
			uint64_t p0, p1;

			memcpy(&p0, s0 + x, sizeof(p0));
			memcpy(&p1, s1 + x, sizeof(p1));

			if (mode != NirMode::Off) {
				uint64_t t = (p1 & M1) >> 1;
				t += (p1 & M3) >> 33;
				t /= 10;
				t <<= shift;

				uint64_t irR = t * red;
				uint64_t irG = t * green;
				uint64_t irB = t * blue;

				irR |= irR << 32;
				irG |= irG << 32;
				irB |= irB << 32;

				ir0[n] = irB | (irG << 16);
				ir1[n] = irG | (irR << 16);
			}

			if (y & 2) {
				uint64_t t = (p0 & M1) << 16;
				t |= t << 32;
				p1 &= M31;
				p1 |= t;
				t = (p0 & M3) >> 32;
				p0 &= M432;
				p0 |= t;
			} else {
				uint64_t t = (p0 & M3) << 16;
				t |= t >> 32;
				p1 &= M31;
				p1 |= t;
				t = (p0 & M1) << 32;
				p0 &= M421;
				p0 |= t;
			}

			if (shift) {
				p0 <<= shift;
				p1 <<= shift;
			}

			memcpy(d0 + x, &p0, sizeof(p0));
			memcpy(d1 + x, &p1, sizeof(p1));
		}

		if (mode == NirMode::Off)
			continue;

		for (unsigned int i = 0; i < width - 1; i++) {
			uint16_t v0, v1;
			uint16_t i0 = (ir0[i / 4] >> (16 * (i % 4))) & 0xffff;
			uint16_t i1 = (ir1[i / 4] >> (16 * (i % 4))) & 0xffff;

			memcpy(&v0, d0 + i, sizeof(v0));
			memcpy(&v1, d1 + i, sizeof(v1));

			if (mode == NirMode::Day) {
				v0 = v0 > i0 ? v0 - i0 : 0;
				v1 = v1 > i1 ? v1 - i1 : 0;
			} else {
				v0 = std::min<unsigned int>(v0 + i0, maxValue);
				v1 = std::min<unsigned int>(v1 + i1, maxValue);
			}

			memcpy(d0 + i, &v0, sizeof(v0));
			memcpy(d1 + i, &v1, sizeof(v1));
		}
	}
}

bool compare(const std::vector<uint8_t> &got, const std::vector<uint8_t> &expected,
	     NirMode mode)
{
	if (got == expected)
		return true;

	for (size_t i = 0; i + 1 < got.size(); i += 2) {
		uint16_t a, b;
		memcpy(&a, got.data() + i, 2);
		memcpy(&b, expected.data() + i, 2);
		if (a != b) {
			fprintf(stderr, "mode %u mismatch at pixel %zu: %u != %u\n",
				static_cast<unsigned int>(mode), i / 2, a, b);
			return false;
		}
	}

	return false;
}

} /* namespace */

int main()
{
	constexpr unsigned int width = 2592;
	constexpr unsigned int height = 1944;
	constexpr unsigned int stride = width * 2;
	constexpr size_t size = stride * height;
	constexpr unsigned int bitDepth = 10;
	constexpr unsigned int shift = 16 - bitDepth;

	std::vector<uint8_t> input(size);
	std::vector<uint8_t> rawInput(size);
	std::vector<uint8_t> expected(size);

	std::mt19937 rng(1234);
	uint16_t *in = reinterpret_cast<uint16_t *>(input.data());
	uint16_t *raw = reinterpret_cast<uint16_t *>(rawInput.data());

	for (unsigned int i = 0; i < width * height; i++) {
		in[i] = (64 + (rng() % 960)) << shift;
		raw[i] = in[i] >> shift;
	}

	struct TestCase {
		NirMode mode;
		unsigned int red;
		unsigned int green;
		unsigned int blue;
	} cases[] = {
		{ NirMode::Off, 0, 0, 0 },
		{ NirMode::Day, 7, 4, 3 },
		{ NirMode::Night, 6, 3, 0 },
		{ NirMode::Day, 10, 10, 10 },
	};

	for (const TestCase &testCase : cases) {
		NirParams params{ testCase.mode,
				  static_cast<uint8_t>(testCase.red),
				  static_cast<uint8_t>(testCase.green),
				  static_cast<uint8_t>(testCase.blue) };

		reference(rawInput.data(), expected.data(), width, height, stride,
			  testCase.mode, testCase.red, testCase.green,
			  testCase.blue, shift);

		std::vector<uint8_t> gotReference = input;
		rgbIrToBayerReference(gotReference.data(), width, height, stride,
				      params, bitDepth);
		if (!compare(gotReference, expected, testCase.mode))
			return 1;

		std::vector<uint8_t> gotProduction = input;
		rgbIrToBayer(gotProduction.data(), width, height, stride, params,
			     bitDepth);
		if (!compare(gotProduction, expected, testCase.mode))
			return 1;
	}

	printf("RGB-IR conversion matches the reference implementation\n");

	return 0;
}
