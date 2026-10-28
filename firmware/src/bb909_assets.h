/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "drum909.h"
#include <stddef.h>
/* Owned 909 PCM pack v1, bounded to the existing 240 KiB region geometry.
 * This defines an image format, not authority to replace device contents.
 * Parse before audio; on success each kit shares these immutable PCM buffers.
 */
int bb909_assets_parse(const void *image, size_t bytes, drum909_samples_t *out);
