/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_GEM_H__
#define __ROCKET_GEM_H__

#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>

struct rocket_gem_object {
	struct drm_gem_shmem_object base;

	struct mutex mutex;
	size_t size;
	u32 offset;
	u32 last_cpu_prep_op;
};

struct drm_gem_object *rocket_gem_create_object(struct drm_device *dev, size_t size);

int rocket_ioctl_create_bo(struct drm_device *dev, void *data, struct drm_file *file);

int rocket_ioctl_prep_bo(struct drm_device *dev, void *data, struct drm_file *file);

int rocket_ioctl_fini_bo(struct drm_device *dev, void *data, struct drm_file *file);

static inline
struct  rocket_gem_object *to_rocket_bo(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct rocket_gem_object, base);
}

#endif
