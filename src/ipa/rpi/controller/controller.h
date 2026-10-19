/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 *
 * ISP controller interface
 */
#pragma once

/*
 * The Controller is simply a container for a collecting together a number of
 * "control algorithms" (such as AWB etc.) and for running them all in a
 * convenient manner.
 */

#include <cstdint>
#include <vector>
#include <string>

#include <libcamera/base/utils.h>
#include "libcamera/internal/value_node.h"

#include "camera_mode.h"
#include "device_status.h"
#include "metadata.h"
#include "statistics.h"

namespace RPiController {

/*
 * The following structures are used to export the CNN input/output tensor information
 * through the rpi::CnnOutputTensorInfo and rpi::CnnInputTensorInfo controls.
 * Applications must cast the span to these structures exactly.
 */
static constexpr unsigned int NetworkNameLen = 64;
static constexpr unsigned int MaxNumTensors = 16;
static constexpr unsigned int MaxNumDimensions = 16;

struct OutputTensorInfo {
	uint32_t tensorDataNum;
	uint32_t numDimensions;
	uint16_t size[MaxNumDimensions];
};

struct CnnOutputTensorInfo {
	char networkName[NetworkNameLen];
	uint32_t numTensors;
	OutputTensorInfo info[MaxNumTensors];
	uint8_t frameCount;
};

struct CnnInputTensorInfo {
	char networkName[NetworkNameLen];
	uint32_t width;
	uint32_t height;
	uint32_t numChannels;
	uint8_t frameCount;
};

struct CnnKpiInfo {
	uint32_t dnnRuntime;
	uint32_t dspRuntime;
};

class Algorithm;
typedef std::unique_ptr<Algorithm> AlgorithmPtr;

/*
 * Default near-infrared processing configuration for RGB-IR sensors. This is
 * read from the "pipeline_handler.nir" section of the sensor tuning file.
 */
struct NirConfig {
	enum class Mode {
		Off,
		Day,
		Night,
	};

	Mode mode = Mode::Day;
	uint8_t dayRed = 7;
	uint8_t dayGreen = 4;
	uint8_t dayBlue = 3;
	uint8_t nightRed = 6;
	uint8_t nightGreen = 3;
	uint8_t nightBlue = 0;
};

/*
 * The Controller holds a pointer to some global_metadata, which is how
 * different controllers and control algorithms within them can exchange
 * information. The Prepare function returns a pointer to metadata for this
 * specific image, and which should be passed on to the Process function.
 */

class Controller
{
public:
	struct HardwareConfig {
		libcamera::Size agcRegions;
		libcamera::Size agcZoneWeights;
		libcamera::Size awbRegions;
		libcamera::Size cacRegions;
		libcamera::Size focusRegions;
		unsigned int numHistogramBins;
		unsigned int numGammaPoints;
		unsigned int pipelineWidth;
		bool statsInline;
		libcamera::utils::Duration minPixelProcessingTime;
		bool dataBufferStrided;
	};

	Controller();
	~Controller();
	int read(char const *filename);
	void initialise();
	void switchMode(CameraMode const &cameraMode, Metadata *metadata);
	void prepare(Metadata *imageMetadata);
	void process(StatisticsPtr stats, Metadata *imageMetadata);
	Metadata &getGlobalMetadata();
	Algorithm *getAlgorithm(std::string const &name) const;
	const std::string &getTarget() const;
	const HardwareConfig &getHardwareConfig() const;
	const NirConfig &getNirConfig() const;

protected:
	int createAlgorithm(const std::string &name, const libcamera::ValueNode &params);

	Metadata globalMetadata_;
	std::vector<AlgorithmPtr> algorithms_;
	bool switchModeCalled_;

private:
	std::string target_;
	NirConfig nirConfig_;
};

} /* namespace RPiController */
