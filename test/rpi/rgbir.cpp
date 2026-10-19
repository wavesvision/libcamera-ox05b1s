/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Waves Vision Ltd.
 *
 * Test the RGB-IR to Bayer converter.
 *
 * The OX05B1S 4x4 pattern is:
 *
 *     R  G  R  G
 *     G  B  G  B
 *     IR G  IR G
 *     G  B  G  B
 *
 * The conversion replaces the IR row with the red/green row above it.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/libcamera/pipeline/rpi/pisp/rgbir.h"

using namespace libcamera::RPi;

namespace {

constexpr unsigned int width = 256;
constexpr unsigned int height = 16;
constexpr unsigned int stride = width * 2;
constexpr unsigned int bitDepth = 10;
constexpr unsigned int shift = 16 - bitDepth;

std::vector<uint8_t> makePattern()
{
	std::vector<uint8_t> frame(stride * height);
	uint16_t *pixels = reinterpret_cast<uint16_t *>(frame.data());

	for (unsigned int y = 0; y < height; y++) {
		for (unsigned int x = 0; x < width; x++) {
			uint16_t value;

			switch (y % 4) {
			case 0:
				value = (x & 1) ? 200 : 100;
				break;
			case 1:
				value = (x & 1) ? 50 : 200;
				break;
			case 2:
				value = (x & 1) ? 200 : 10;
				break;
			default:
				value = (x & 1) ? 50 : 200;
				break;
			}

			pixels[y * width + x] = value << shift;
		}
	}

	return frame;
}

bool compare(const std::vector<uint8_t> &got,
	     const std::vector<uint8_t> &expected, const char *what)
{
	if (got == expected)
		return true;

	for (size_t i = 0; i + 1 < got.size(); i += 2) {
		uint16_t a, b;
		memcpy(&a, got.data() + i, 2);
		memcpy(&b, expected.data() + i, 2);
		if (a != b) {
			fprintf(stderr, "%s mismatch at pixel %zu: %u != %u\n",
				what, i / 2, a, b);
			return false;
		}
	}

	return false;
}

} /* namespace */

int main()
{
	const std::vector<uint8_t> input = makePattern();

	struct TestCase {
		NirMode mode;
		unsigned int red;
		unsigned int green;
		unsigned int blue;
		const char *name;
	} cases[] = {
		{ NirMode::Off, 0, 0, 0, "off" },
		{ NirMode::Day, 7, 4, 3, "day" },
		{ NirMode::Night, 6, 3, 0, "night" },
	};

	for (const TestCase &testCase : cases) {
		NirParams params{ testCase.mode,
				  static_cast<uint8_t>(testCase.red),
				  static_cast<uint8_t>(testCase.green),
				  static_cast<uint8_t>(testCase.blue) };

		std::vector<uint8_t> expected = input;
		rgbIrToBayerReference(expected.data(), width, height, stride,
				      params, bitDepth);

		std::vector<uint8_t> got = input;
		rgbIrToBayer(got.data(), width, height, stride, params, bitDepth);

		if (!compare(got, expected, testCase.name))
			return 1;

		if (testCase.mode == NirMode::Off) {
			const uint16_t *in = reinterpret_cast<const uint16_t *>(input.data());
			const uint16_t *out = reinterpret_cast<const uint16_t *>(got.data());

			for (unsigned int y = 0; y < height; y++) {
				for (unsigned int x = 0; x < width; x++) {
					uint16_t outValue = out[y * width + x];
					uint16_t expectedValue;

					if (y % 4 == 2)
						expectedValue = out[(y - 2) * width + x];
					else
						expectedValue = in[y * width + x];

					if (outValue != expectedValue) {
						fprintf(stderr,
							"unexpected value at %u,%u: %u != %u\n",
							x, y, outValue,
							expectedValue);
						return 1;
					}
				}
			}
		}
	}

	printf("RGB-IR conversion test passed\n");

	return 0;
}
