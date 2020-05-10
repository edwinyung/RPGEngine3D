/*************************************************************************/
/*  image_loader_webp.cpp                                                */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "image_loader_webp.h"

#include "core/io/marshalls.h"
#include "core/os/os.h"
#include "core/print_string.h"

#include <stdlib.h>
#include <webp/decode.h>
#include <webp/encode.h>

static Vector<uint8_t> _webp_lossy_pack(const Ref<Image> &p_image, float p_quality) {

	ERR_FAIL_COND_V(p_image.is_null() || p_image->empty(), Vector<uint8_t>());

	Ref<Image> img = p_image->duplicate();
	if (img->detect_alpha())
		img->convert(Image::FORMAT_RGBA8);
	else
		img->convert(Image::FORMAT_RGB8);

	Size2 s(img->get_width(), img->get_height());
	Vector<uint8_t> data = img->get_data();
	const uint8_t *r = data.ptr();

	uint8_t *dst_buff = nullptr;
	size_t dst_size = 0;
	if (img->get_format() == Image::FORMAT_RGB8) {

		dst_size = WebPEncodeRGB(r, s.width, s.height, 3 * s.width, CLAMP(p_quality * 100.0, 0, 100.0), &dst_buff);
	} else {
		dst_size = WebPEncodeRGBA(r, s.width, s.height, 4 * s.width, CLAMP(p_quality * 100.0, 0, 100.0), &dst_buff);
	}

	ERR_FAIL_COND_V(dst_size == 0, Vector<uint8_t>());
	Vector<uint8_t> dst;
	dst.resize(4 + dst_size);
	uint8_t *w = dst.ptrw();
	w[0] = 'W';
	w[1] = 'E';
	w[2] = 'B';
	w[3] = 'P';
	copymem(&w[4], dst_buff, dst_size);
	free(dst_buff);

	return dst;
}

static Ref<Image> _webp_lossy_unpack(const Vector<uint8_t> &p_buffer) {

	int size = p_buffer.size() - 4;
	ERR_FAIL_COND_V(size <= 0, Ref<Image>());
	const uint8_t *r = p_buffer.ptr();

	ERR_FAIL_COND_V(r[0] != 'W' || r[1] != 'E' || r[2] != 'B' || r[3] != 'P', Ref<Image>());
	WebPBitstreamFeatures features;
	if (WebPGetFeatures(&r[4], size, &features) != VP8_STATUS_OK) {
		ERR_FAIL_V_MSG(Ref<Image>(), "Error unpacking WEBP image.");
	}

	/*
	print_line("width: "+itos(features.width));
	print_line("height: "+itos(features.height));
	print_line("alpha: "+itos(features.has_alpha));
	*/

	Vector<uint8_t> dst_image;
	int datasize = features.width * features.height * (features.has_alpha ? 4 : 3);
	dst_image.resize(datasize);

	uint8_t *dst_w = dst_image.ptrw();

	bool errdec = false;
	if (features.has_alpha) {
		errdec = WebPDecodeRGBAInto(&r[4], size, dst_w, datasize, 4 * features.width) == nullptr;
	} else {
		errdec = WebPDecodeRGBInto(&r[4], size, dst_w, datasize, 3 * features.width) == nullptr;
	}

	ERR_FAIL_COND_V_MSG(errdec, Ref<Image>(), "Failed decoding WebP image.");

	Ref<Image> img = memnew(Image(features.width, features.height, 0, features.has_alpha ? Image::FORMAT_RGBA8 : Image::FORMAT_RGB8, dst_image));
	return img;
}

Error webp_load_image_from_buffer(Image *p_image, const uint8_t *p_buffer, int p_buffer_len) {

	ERR_FAIL_NULL_V(p_image, ERR_INVALID_PARAMETER);

	WebPBitstreamFeatures features;
	if (WebPGetFeatures(p_buffer, p_buffer_len, &features) != VP8_STATUS_OK) {
		ERR_FAIL_V(ERR_FILE_CORRUPT);
	}

	Vector<uint8_t> dst_image;
	int datasize = features.width * features.height * (features.has_alpha ? 4 : 3);
	dst_image.resize(datasize);
	uint8_t *dst_w = dst_image.ptrw();

	bool errdec = false;
	if (features.has_alpha) {
		errdec = WebPDecodeRGBAInto(p_buffer, p_buffer_len, dst_w, datasize, 4 * features.width) == nullptr;
	} else {
		errdec = WebPDecodeRGBInto(p_buffer, p_buffer_len, dst_w, datasize, 3 * features.width) == nullptr;
	}

	ERR_FAIL_COND_V_MSG(errdec, ERR_FILE_CORRUPT, "Failed decoding WebP image.");

	p_image->create(features.width, features.height, 0, features.has_alpha ? Image::FORMAT_RGBA8 : Image::FORMAT_RGB8, dst_image);

	return OK;
}

static Ref<Image> _webp_mem_loader_func(const uint8_t *p_png, int p_size) {

	Ref<Image> img;
	img.instance();
	Error err = webp_load_image_from_buffer(img.ptr(), p_png, p_size);
	ERR_FAIL_COND_V(err, Ref<Image>());
	return img;
}

Error ImageLoaderWEBP::load_image(Ref<Image> p_image, FileAccess *f, bool p_force_linear, float p_scale) {

	Vector<uint8_t> src_image;
	int src_image_len = f->get_len();
	ERR_FAIL_COND_V(src_image_len == 0, ERR_FILE_CORRUPT);
	src_image.resize(src_image_len);

	uint8_t *w = src_image.ptrw();

	f->get_buffer(&w[0], src_image_len);

	f->close();

	Error err = webp_load_image_from_buffer(p_image.ptr(), w, src_image_len);

	return err;
}

void ImageLoaderWEBP::get_recognized_extensions(List<String> *p_extensions) const {

	p_extensions->push_back("webp");
}

ImageLoaderWEBP::ImageLoaderWEBP() {
	Image::_webp_mem_loader_func = _webp_mem_loader_func;
	Image::lossy_packer = _webp_lossy_pack;
	Image::lossy_unpacker = _webp_lossy_unpack;
}