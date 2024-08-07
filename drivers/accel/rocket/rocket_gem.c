// SPDX-License-Identifier: GPL-2.0
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_device.h>
#include <drm/drm_utils.h>
#include <drm/rocket_accel.h>
#include <linux/dma-mapping.h>

#include "rocket_gem.h"

/**
 * rocket_gem_create_object - Implementation of driver->gem_create_object.
 * @dev: DRM device
 * @size: Size in bytes of the memory the object will reference
 *
 * This lets the GEM helpers allocate object structs for us, and keep
 * our BO stats correct.
 */
struct drm_gem_object *rocket_gem_create_object(struct drm_device *dev, size_t size)
{
	struct rocket_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	return &obj->base.base;
}

int rocket_ioctl_create_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_create_bo *args = data;
	struct drm_gem_shmem_object *shmem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_object *gem_obj;
	struct sg_table *sgt;
	int ret;

	shmem_obj = drm_gem_shmem_create(dev, args->size);
	if (IS_ERR(shmem_obj))
		return PTR_ERR(shmem_obj);

	gem_obj = &shmem_obj->base;
	rkt_obj = to_rocket_bo(gem_obj);

	rkt_obj->size = args->size;
	rkt_obj->offset = 0;
	mutex_init(&rkt_obj->mutex);

	ret = drm_gem_handle_create(file, gem_obj, &args->handle);
	drm_gem_object_put(gem_obj);
	if (ret)
		goto err;

	sgt = drm_gem_shmem_get_pages_sgt(shmem_obj);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err;
	}

	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	args->dma_address = sg_dma_address(shmem_obj->sgt->sgl);

	return 0;

err:
	drm_gem_shmem_object_free(gem_obj);

	return ret;
}

static inline enum dma_data_direction rocket_op_to_dma_dir(u32 op)
{
	if (op & ROCKET_PREP_READ)
		return DMA_FROM_DEVICE;
	else if (op & ROCKET_PREP_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_BIDIRECTIONAL;
}

int rocket_ioctl_prep_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_prep_bo *args = data;
	unsigned long timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);
	struct drm_gem_object *gem_obj;
	struct drm_gem_shmem_object *shmem_obj;
	bool write = !!(args->op & ROCKET_PREP_WRITE);
	long ret = 0;

	if (args->op & ~(ROCKET_PREP_READ | ROCKET_PREP_WRITE))
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	ret = dma_resv_wait_timeout(gem_obj->resv, dma_resv_usage_rw(write),
				    true, timeout);
	if (!ret)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	shmem_obj = &to_rocket_bo(gem_obj)->base;

	dma_sync_sgtable_for_cpu(dev->dev, shmem_obj->sgt, rocket_op_to_dma_dir(args->op));
	to_rocket_bo(gem_obj)->last_cpu_prep_op = args->op;

	drm_gem_object_put(gem_obj);

	return ret;
}

int rocket_ioctl_fini_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_fini_bo *args = data;
	struct drm_gem_object *gem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_shmem_object *shmem_obj;

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	rkt_obj = to_rocket_bo(gem_obj);
	shmem_obj = &rkt_obj->base;

	WARN_ON(rkt_obj->last_cpu_prep_op == 0);

	dma_sync_sgtable_for_device(dev->dev, shmem_obj->sgt,
				    rocket_op_to_dma_dir(rkt_obj->last_cpu_prep_op));
	rkt_obj->last_cpu_prep_op = 0;

	drm_gem_object_put(gem_obj);

	return 0;
}
