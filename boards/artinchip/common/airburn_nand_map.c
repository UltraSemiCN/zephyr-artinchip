/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_airburn_nand_map

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(airburn_nand, CONFIG_FLASH_LOG_LEVEL);

#define MAP_NODE DT_DRV_INST(0)
#define BLOCK_SIZE DT_PROP(MAP_NODE, erase_block_size)
#define FLASH_SIZE DT_PROP(MAP_NODE, size_bytes)
#define BLOCK_COUNT (FLASH_SIZE / BLOCK_SIZE)
#define INVALID_BLOCK UINT16_MAX

struct nand_region {
	uint32_t start;
	uint32_t size;
};
#define REGION(node) { DT_REG_ADDR(node), DT_REG_SIZE(node) },
static const struct nand_region regions[] = {
	DT_FOREACH_CHILD_STATUS_OKAY(DT_CHILD(MAP_NODE, partitions), REGION)
};
static const struct device *const raw = DEVICE_DT_GET(DT_PHANDLE(MAP_NODE, flash));
/* Each partition starts its own logical-to-physical sequence. */
static uint16_t block_map[BLOCK_COUNT];
static const struct flash_parameters parameters = {
	.write_block_size = DT_PROP(MAP_NODE, write_block_size),
	.erase_value = 0xff,
};
static const struct flash_pages_layout layout = {
	.pages_count = BLOCK_COUNT,
	.pages_size = BLOCK_SIZE,
};

static int mapped_read(const struct device *dev, off_t offset, void *buffer, size_t length)
{
	uint8_t *out = buffer;
	const struct nand_region *region = NULL;

	ARG_UNUSED(dev);
	if (offset < 0 || offset > FLASH_SIZE || length > FLASH_SIZE - offset) {
		return -EINVAL;
	}
	if (length == 0U) {
		return 0;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(regions); i++) {
		if (offset >= regions[i].start && offset < regions[i].start + regions[i].size) {
			region = &regions[i];
			break;
		}
	}
	if (region == NULL || length > region->start + region->size - offset) {
		return -EINVAL;
	}
	while (length > 0U) {
		uint32_t within = offset % BLOCK_SIZE;
		uint16_t physical = block_map[offset / BLOCK_SIZE];
		size_t count = MIN(length, BLOCK_SIZE - within);
		int ret;

		if (physical == INVALID_BLOCK) {
			return -ENOSPC;
		}
		ret = flash_read(raw, (off_t)physical * BLOCK_SIZE + within, out, count);
		if (ret != 0) {
			return ret;
		}
		offset += count;
		out += count;
		length -= count;
	}
	return 0;
}

static int mapped_write(const struct device *dev, off_t offset, const void *data, size_t length)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(offset);
	ARG_UNUSED(data);
	ARG_UNUSED(length);
	return -ENOTSUP;
}

static int mapped_erase(const struct device *dev, off_t offset, size_t length)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(offset);
	ARG_UNUSED(length);
	return -ENOTSUP;
}

static const struct flash_parameters *mapped_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);
	return &parameters;
}

static int mapped_size(const struct device *dev, uint64_t *size)
{
	ARG_UNUSED(dev);
	*size = FLASH_SIZE;
	return 0;
}

static void mapped_layout(const struct device *dev, const struct flash_pages_layout **out,
			  size_t *count)
{
	ARG_UNUSED(dev);
	*out = &layout;
	*count = 1U;
}

static int mapped_init(const struct device *dev)
{
	uint32_t bad_count = 0U;

	ARG_UNUSED(dev);
	if (!device_is_ready(raw)) {
		return -ENODEV;
	}
	memset(block_map, 0xff, sizeof(block_map));
	for (size_t i = 0U; i < ARRAY_SIZE(regions); i++) {
		uint32_t logical = regions[i].start / BLOCK_SIZE;
		uint32_t end = regions[i].start + regions[i].size;

		if (end > FLASH_SIZE || regions[i].start % BLOCK_SIZE != 0U ||
		    regions[i].size % BLOCK_SIZE != 0U) {
			return -EINVAL;
		}
		for (off_t physical = regions[i].start; physical < end; physical += BLOCK_SIZE) {
			enum flash_block_status status;
			int ret = flash_ex_op(raw, FLASH_EX_OP_IS_BAD_BLOCK,
					      (uintptr_t)&physical, &status);

			if (ret != 0) {
				return ret;
			}
			if (status == FLASH_BLOCK_BAD) {
				bad_count++;
				continue;
			}
			block_map[logical++] = physical / BLOCK_SIZE;
		}
	}
	LOG_DBG("AirBurn partition map ready, %u bad blocks skipped", bad_count);
	return 0;
}

static DEVICE_API(flash, mapped_api) = {
	.read = mapped_read,
	.write = mapped_write,
	.erase = mapped_erase,
	.get_parameters = mapped_parameters,
	.get_size = mapped_size,
	.page_layout = mapped_layout,
};

DEVICE_DT_INST_DEFINE(0, mapped_init, NULL, NULL, NULL, POST_KERNEL, 81, &mapped_api);
