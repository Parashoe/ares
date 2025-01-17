/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stdint.h>
#include "device.hpp"
#include "rdp_common.hpp"

namespace RDP
{
struct ScanoutOptions
{
	// Simple (obsolete) crop method. If crop_rect.enable is false, this
	// crops top / bottom with number of pixels (doubled if interlace),
	// and left / right are cropped in an aspect preserving way.
	// If crop_rect.enable is true,
	// this is ignored and the crop_rect struct is used instead.
	// Crop pixels are adjusted for upscaling, pixels are assumed to
	// be specified for the original resolution.
	unsigned crop_overscan_pixels = 0;

	struct CropRect
	{
		unsigned left = 0;
		unsigned right = 0;
		unsigned top = 0; // Doubled if interlace
		unsigned bottom = 0; // Doubled if interlace
		bool enable = false;
	} crop_rect;

	unsigned downscale_steps = 0;

	// Works around certain game bugs. Considered a hack if enabled.
	bool persist_frame_on_invalid_input = false;

	// To be equivalent to reference behavior where
	// pixels persist for an extra frame.
	// Not hardware accurate, but needed for weave interlace mode.
	bool blend_previous_frame = false;

	// Upscale deinterlacing deinterlaces by upscaling in Y, with an Y coordinate offset matching the field.
	// If disabled, weave interlacing is used.
	// Weave deinterlacing should *not* be used, except to run test suite!
	bool upscale_deinterlacing = true;

	struct
	{
		bool aa = true;
		bool scale = true;
		bool serrate = true;
		bool dither_filter = true;
		bool divot_filter = true;
		bool gamma_dither = true;
	} vi;

	// External memory support.
	// If true, the scanout image will be created with external memory support.
	// presist_frame_on_invalid_input must be false when using exports.
	VkExternalMemoryHandleTypeFlagBits export_handle_type = {};
	bool export_scanout = false;
};

struct VIScanoutBuffer
{
	Vulkan::BufferHandle buffer;
	Vulkan::Fence fence;
	unsigned width = 0;
	unsigned height = 0;
};

class Renderer;

class VideoInterface : public Vulkan::DebugChannelInterface
{
public:
	void set_device(Vulkan::Device *device);
	void set_renderer(Renderer *renderer);
	void set_vi_register(VIRegister reg, uint32_t value);

	void set_rdram(const Vulkan::Buffer *rdram, size_t offset, size_t size);
	void set_hidden_rdram(const Vulkan::Buffer *hidden_rdram);

	int resolve_shader_define(const char *name, const char *define) const;

	Vulkan::ImageHandle scanout(VkImageLayout target_layout, const ScanoutOptions &options = {}, unsigned scale_factor = 1);
	void scanout_memory_range(unsigned &offset, unsigned &length) const;
	void set_shader_bank(const ShaderBank *bank);

	enum PerScanlineRegisterBits
	{
		// Currently supported bits.
		PER_SCANLINE_HSTART_BIT = 1 << 0,
		PER_SCANLINE_XSCALE_BIT = 1 << 1
	};
	using PerScanlineRegisterFlags = uint32_t;

	void begin_vi_register_per_scanline(PerScanlineRegisterFlags flags);
	void set_vi_register_for_scanline(PerScanlineRegisterBits reg, uint32_t value);
	void latch_vi_register_for_scanline(unsigned vi_line);
	void end_vi_register_per_scanline();

private:
	Vulkan::Device *device = nullptr;
	Renderer *renderer = nullptr;
	uint32_t vi_registers[unsigned(VIRegister::Count)] = {};

	struct PerScanlineRegisterState
	{
		uint32_t latched_state;
		uint32_t line_state[VI_V_END_MAX];
	};

	struct
	{
		PerScanlineRegisterState h_start;
		PerScanlineRegisterState x_scale;
		PerScanlineRegisterFlags flags = 0;
		unsigned line = 0;
		bool ended = false;
	} per_line_state;

	const Vulkan::Buffer *rdram = nullptr;
	const Vulkan::Buffer *hidden_rdram = nullptr;
	Vulkan::BufferHandle gamma_lut;
	Vulkan::BufferViewHandle gamma_lut_view;
	const ShaderBank *shader_bank = nullptr;

	void init_gamma_table();
	bool previous_frame_blank = false;
	bool debug_channel = false;
	int filter_debug_channel_x = -1;
	int filter_debug_channel_y = -1;

	void message(const std::string &tag, uint32_t code,
	             uint32_t x, uint32_t y, uint32_t z,
	             uint32_t num_words, const Vulkan::DebugChannelInterface::Word *words) override;

	// Frame state.
	uint32_t frame_count = 0;
	uint32_t last_valid_frame_count = 0;
	Vulkan::ImageHandle prev_scanout_image;
	VkImageLayout prev_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	bool prev_image_is_external = false;

	size_t rdram_offset = 0;
	size_t rdram_size = 0;
	bool timestamp = false;

	struct HorizontalInfo
	{
		int32_t h_start;
		int32_t h_start_clamp;
		int32_t h_end_clamp;
		int32_t x_start;
		int32_t x_add;
		int32_t y_start;
		int32_t y_add;
		int32_t y_base;
	};

	struct HorizontalInfoLines
	{
		HorizontalInfo lines[VI_MAX_OUTPUT_SCANLINES];
	};

	static void bind_horizontal_info_view(Vulkan::CommandBuffer &cmd, const HorizontalInfoLines &lines);

	struct Registers
	{
		int vi_width;
		int vi_offset;
		int v_current_line;
		bool is_pal;
		uint32_t status;

		int init_y_add;

		// Global scale pass scissor box.
		int h_start_clamp, h_res_clamp;
		int h_start, h_res;
		int v_start, v_res;

		// For AA stages.
		int max_x, max_y;
	};

	Registers decode_vi_registers(HorizontalInfoLines *lines) const;
	void clear_per_scanline_state();

	Vulkan::ImageHandle vram_fetch_stage(const Registers &registers,
	                                     unsigned scaling_factor) const;
	Vulkan::ImageHandle aa_fetch_stage(Vulkan::CommandBuffer &cmd,
	                                   Vulkan::Image &vram_image,
	                                   const Registers &registers,
	                                   unsigned scaling_factor) const;
	Vulkan::ImageHandle divot_stage(Vulkan::CommandBuffer &cmd,
	                                Vulkan::Image &aa_image,
	                                const Registers &registers,
	                                unsigned scaling_factor) const;
	Vulkan::ImageHandle scale_stage(Vulkan::CommandBuffer &cmd,
	                                Vulkan::Image &divot_image,
	                                Registers registers,
	                                const HorizontalInfoLines &lines,
	                                unsigned scaling_factor,
	                                bool degenerate,
	                                const ScanoutOptions &options,
	                                bool final_pass) const;
	Vulkan::ImageHandle downscale_stage(Vulkan::CommandBuffer &cmd,
	                                    Vulkan::Image &scale_image,
	                                    unsigned scaling_factor,
	                                    unsigned downscale_factor,
										const ScanoutOptions &options,
										bool final_pass) const;
	Vulkan::ImageHandle upscale_deinterlace(Vulkan::CommandBuffer &cmd,
	                                        Vulkan::Image &scale_image,
	                                        unsigned scaling_factor, bool field_select,
	                                        const ScanoutOptions &options) const;
	static bool need_fetch_bug_emulation(const Registers &reg, unsigned scaling_factor);
};
}
