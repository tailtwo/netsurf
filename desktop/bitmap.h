/*
 * Copyright 2022 Michael Drake <tlsa@nesturf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Internal core bitmap interface.
 */

#ifndef _NETSURF_DESKTOP_BITMAP_H_
#define _NETSURF_DESKTOP_BITMAP_H_

#include <nsutils/endian.h>

#include "netsurf/bitmap.h"

/** The client bitmap format. */
extern bitmap_fmt_t bitmap_fmt;

/** Pixel format: colour component order. */
struct bitmap_colour_layout {
	uint8_t r; /**< Byte offset within pixel to red component. */
	uint8_t g; /**< Byte offset within pixel to green component. */
	uint8_t b; /**< Byte offset within pixel to blue component. */
	uint8_t a; /**< Byte offset within pixel to alpha component. */
};

/**
 * Get the colour layout for the given bitmap format.
 *
 * \param[in] fmt  Pixel format to get channel layout for,
 * \return channel layout structure.
 */
static inline struct bitmap_colour_layout bitmap__get_colour_layout(
		const bitmap_fmt_t *fmt)
{
	switch (fmt->layout) {
	default:
		/* Fall through. */
	case BITMAP_LAYOUT_R8G8B8A8:
		return (struct bitmap_colour_layout) {
			.r = 0,
			.g = 1,
			.b = 2,
			.a = 3,
		};

	case BITMAP_LAYOUT_B8G8R8A8:
		return (struct bitmap_colour_layout) {
			.b = 0,
			.g = 1,
			.r = 2,
			.a = 3,
		};

	case BITMAP_LAYOUT_A8R8G8B8:
		return (struct bitmap_colour_layout) {
			.a = 0,
			.r = 1,
			.g = 2,
			.b = 3,
		};

	case BITMAP_LAYOUT_A8B8G8R8:
		return (struct bitmap_colour_layout) {
			.a = 0,
			.b = 1,
			.g = 2,
			.r = 3,
		};
	}
}

/**
 * Sanitise bitmap pixel component layout.
 *
 * Map endian-dependant layouts to byte-wise layout for the host.
 *
 * \param[in]  layout  Layout to convert.
 * \return sanitised layout.
 */
static inline enum bitmap_layout bitmap_sanitise_bitmap_layout(
		enum bitmap_layout layout)
{
	bool le = endian_host_is_le();

	switch (layout) {
	case BITMAP_LAYOUT_RGBA8888:
		layout = (le) ? BITMAP_LAYOUT_A8B8G8R8
		              : BITMAP_LAYOUT_R8G8B8A8;
		break;
	case BITMAP_LAYOUT_BGRA8888:
		layout = (le) ? BITMAP_LAYOUT_A8R8G8B8
		              : BITMAP_LAYOUT_B8G8R8A8;
		break;
	case BITMAP_LAYOUT_ARGB8888:
		layout = (le) ? BITMAP_LAYOUT_B8G8R8A8
		              : BITMAP_LAYOUT_A8R8G8B8;
		break;
	case BITMAP_LAYOUT_ABGR8888:
		layout = (le) ? BITMAP_LAYOUT_R8G8B8A8
		              : BITMAP_LAYOUT_A8B8G8R8;
		break;
	default:
		break;
	}

	return layout;
}

/**
 * Convert bitmap from one format to another.
 *
 * Note that both formats should be sanitised.
 *
 * \param[in]  bitmap  The bitmap to convert.
 * \param[in]  from    The current bitmap format specifier.
 * \param[in]  to      The bitmap format to convert to.
 */
void bitmap_format_convert(void *bitmap,
		const bitmap_fmt_t *from,
		const bitmap_fmt_t *to);

/**
 * Convert a bitmap to the client bitmap format.
 *
 * \param[in]  bitmap       The bitmap to convert.
 * \param[in]  current_fmt  The current bitmap format specifier.
 */
static inline void bitmap_format_to_client(
		void *bitmap,
		const bitmap_fmt_t *current_fmt)
{
	bitmap_fmt_t from = *current_fmt;

	from.layout = bitmap_sanitise_bitmap_layout(from.layout);
	if (from.layout != bitmap_fmt.layout) {
		bitmap_format_convert(bitmap, &from, &bitmap_fmt);
	}
}

/**
 * Convert a bitmap to the client bitmap format.
 *
 * \param[in]  bitmap      The bitmap to convert.
 * \param[in]  target_fmt  The target bitmap format specifier.
 */
static inline void bitmap_format_from_client(
		void *bitmap,
		const bitmap_fmt_t *target_fmt)
{
	bitmap_fmt_t to = *target_fmt;

	to.layout = bitmap_sanitise_bitmap_layout(to.layout);
	if (to.layout != bitmap_fmt.layout) {
		bitmap_format_convert(bitmap, &bitmap_fmt, &to);
	}
}

#endif