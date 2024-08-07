// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/rocket_accel.h>
#include <linux/gfp_types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "rocket_core.h"
#include "rocket_device.h"
#include "rocket_drv.h"
#include "rocket_job.h"
#include "rocket_registers.h"

#define JOB_TIMEOUT_MS 500

#define job_write(dev, reg, data) writel(data, dev->iomem + (reg))
#define job_read(dev, reg) readl(dev->iomem + (reg))

static struct rocket_job *
to_rocket_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct rocket_job, base);
}

struct rocket_fence {
	struct dma_fence base;
	struct drm_device *dev;
	/* rocket seqno for signaled() test */
	u64 seqno;
	int queue;
};

static inline struct rocket_fence *
to_rocket_fence(struct dma_fence *fence)
{
	return (struct rocket_fence *)fence;
}

static const char *rocket_fence_get_driver_name(struct dma_fence *fence)
{
	return "rocket";
}

static const char *rocket_fence_get_timeline_name(struct dma_fence *fence)
{
	return "rockchip-npu";
}

static const struct dma_fence_ops rocket_fence_ops = {
	.get_driver_name = rocket_fence_get_driver_name,
	.get_timeline_name = rocket_fence_get_timeline_name,
};

static struct dma_fence *rocket_fence_create(struct rocket_core *core)
{
	struct rocket_device *rdev = core->dev;
	struct rocket_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->dev = rdev->ddev;
	fence->seqno = ++core->emit_seqno;
	dma_fence_init(&fence->base, &rocket_fence_ops, &core->job_lock,
		       core->fence_context, fence->seqno);

	return &fence->base;
}

static int
rocket_copy_tasks(struct drm_device *dev,
		  struct drm_file *file_priv,
		  struct drm_rocket_job *job,
		  struct rocket_job *rjob)
{
	struct drm_rocket_task *tasks;
	int ret = 0;
	int i;

	rjob->task_count = job->task_count;

	if (!rjob->task_count)
		return 0;

	tasks = kvmalloc_array(rjob->task_count, sizeof(*tasks), GFP_KERNEL);
	if (!tasks) {
		ret = -ENOMEM;
		DRM_DEBUG("Failed to allocate incoming tasks\n");
		goto fail;
	}

	if (copy_from_user(tasks,
			   (void __user *)(uintptr_t)job->tasks,
			   rjob->task_count * sizeof(*tasks))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy incoming tasks\n");
		goto fail;
	}

	rjob->tasks = kvmalloc_array(job->task_count, sizeof(*rjob->tasks), GFP_KERNEL);
	if (!rjob->tasks) {
		DRM_DEBUG("Failed to allocate task array\n");
		ret = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < rjob->task_count; i++) {
		if (tasks[i].regcmd_count == 0) {
			ret = -EINVAL;
			goto fail;
		}
		rjob->tasks[i].regcmd = tasks[i].regcmd;
		rjob->tasks[i].regcmd_count = tasks[i].regcmd_count;
	}

fail:
	kvfree(tasks);
	return ret;
}

static void rocket_job_hw_submit(struct rocket_core *core, struct rocket_job *job)
{
	struct rocket_device *rdev = core->dev;
	struct rocket_task *task;
	bool task_pp_en = 1;
	bool task_count = 1;

	/* GO ! */

	/* Don't queue the job if a reset is in progress */
	if (!atomic_read(&core->reset.pending)) {

		task = &job->tasks[job->next_task_idx];
		job->next_task_idx++;   /* TODO: Do this only after a succesful run? */

		rocket_write(core, REG_PC_BASE_ADDRESS, 0x1);

		rocket_write(core, REG_CNA_S_POINTER, 0xe + 0x10000000 * core->index);
		rocket_write(core, REG_CORE_S_POINTER, 0xe + 0x10000000 * core->index);

		rocket_write(core, REG_PC_BASE_ADDRESS, task->regcmd);
		rocket_write(core, REG_PC_REGISTER_AMOUNTS, (task->regcmd_count + 1) / 2 - 1);

		rocket_write(core, REG_PC_INTERRUPT_MASK, PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1);
		rocket_write(core, REG_PC_INTERRUPT_CLEAR, PC_INTERRUPT_CLEAR_DPU_0 | PC_INTERRUPT_CLEAR_DPU_1);

		rocket_write(core, REG_PC_TASK_CON, ((0x6 | task_pp_en) << 12) | task_count);

		rocket_write(core, REG_PC_TASK_DMA_BASE_ADDR, 0x0);

		rocket_write(core, REG_PC_OPERATION_ENABLE, 0x1);

		dev_dbg(rdev->dev,
			"Submitted regcmd at 0x%llx to core %d",
			task->regcmd, core->index);
	}
}

static int rocket_acquire_object_fences(struct drm_gem_object **bos,
					int bo_count,
					struct drm_sched_job *job,
					bool is_write)
{
	int i, ret;

	for (i = 0; i < bo_count; i++) {
		ret = dma_resv_reserve_fences(bos[i]->resv, 1);
		if (ret)
			return ret;

		ret = drm_sched_job_add_implicit_dependencies(job, bos[i],
							      is_write);
		if (ret)
			return ret;
	}

	return 0;
}

static void rocket_attach_object_fences(struct drm_gem_object **bos,
					  int bo_count,
					  struct dma_fence *fence)
{
	int i;

	for (i = 0; i < bo_count; i++)
		dma_resv_add_fence(bos[i]->resv, fence, DMA_RESV_USAGE_WRITE);
}

static int rocket_job_push(struct rocket_job *job)
{
	struct rocket_device *rdev = job->rdev;
	struct drm_gem_object **bos;
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;

	bos = kvmalloc_array(job->in_bo_count + job->out_bo_count, sizeof(void *), GFP_KERNEL);
	memcpy(bos, job->in_bos, job->in_bo_count * sizeof(void *));
	memcpy(&bos[job->in_bo_count], job->out_bos, job->out_bo_count * sizeof(void *));

	ret = drm_gem_lock_reservations(bos, job->in_bo_count + job->out_bo_count, &acquire_ctx);
	if (ret)
		goto err;

	mutex_lock(&rdev->sched_lock);
	drm_sched_job_arm(&job->base);

	job->inference_done_fence = dma_fence_get(&job->base.s_fence->finished);

	ret = rocket_acquire_object_fences(job->in_bos, job->in_bo_count, &job->base, false);
	if (ret) {
		mutex_unlock(&rdev->sched_lock);
		goto err_unlock;
	}

	ret = rocket_acquire_object_fences(job->out_bos, job->out_bo_count, &job->base, true);
	if (ret) {
		mutex_unlock(&rdev->sched_lock);
		goto err_unlock;
	}

	kref_get(&job->refcount); /* put by scheduler job completion */

	drm_sched_entity_push_job(&job->base);

	mutex_unlock(&rdev->sched_lock);

	rocket_attach_object_fences(job->out_bos, job->out_bo_count, job->inference_done_fence);

err_unlock:
	drm_gem_unlock_reservations(bos, job->in_bo_count + job->out_bo_count, &acquire_ctx);
err:
	kfree(bos);

	return ret;
}

static void rocket_job_cleanup(struct kref *ref)
{
	struct rocket_job *job = container_of(ref, struct rocket_job,
						refcount);
	unsigned int i;

	dma_fence_put(job->done_fence);
	dma_fence_put(job->inference_done_fence);

	if (job->in_bos) {
		for (i = 0; i < job->in_bo_count; i++)
			drm_gem_object_put(job->in_bos[i]);

		kvfree(job->in_bos);
	}

	if (job->out_bos) {
		for (i = 0; i < job->out_bo_count; i++)
			drm_gem_object_put(job->out_bos[i]);

		kvfree(job->out_bos);
	}

	kfree(job->tasks);

	kfree(job);
}

static void rocket_job_put(struct rocket_job *job)
{
	kref_put(&job->refcount, rocket_job_cleanup);
}

static void rocket_job_free(struct drm_sched_job *sched_job)
{
	struct rocket_job *job = to_rocket_job(sched_job);

	drm_sched_job_cleanup(sched_job);

	rocket_job_put(job);
}

static struct rocket_core *sched_to_core(struct rocket_device *rdev, struct drm_gpu_scheduler *sched)
{
	unsigned int core;

	for (core = 0; core < rdev->comp->num_cores; core++) {
		if (&rdev->cores[core].sched == sched)
			return &rdev->cores[core];
	}

	return NULL;
}

static struct dma_fence *rocket_job_run(struct drm_sched_job *sched_job)
{
	struct rocket_job *job = to_rocket_job(sched_job);
	struct rocket_device *rdev = job->rdev;
	struct rocket_core *core = sched_to_core(rdev, sched_job->sched);
	struct dma_fence *fence = NULL;
	int ret;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	/* Nothing to execute: can happen if the job has finished while
	 * we were resetting the GPU.
	 */
	if (job->next_task_idx == job->task_count)
		return NULL;

	fence = rocket_fence_create(core);
	if (IS_ERR(fence))
		return fence;

	if (job->done_fence)
		dma_fence_put(job->done_fence);
	job->done_fence = dma_fence_get(fence);

	ret = pm_runtime_get_sync(rdev->dev);
	if (ret < 0)
		return fence;

	spin_lock(&core->job_lock);

	core->in_flight_job = job;
	rocket_job_hw_submit(core, job);

	spin_unlock(&core->job_lock);

	return fence;
}

static void rocket_job_handle_done(struct rocket_core *core,
				   struct rocket_job *job)
{
	if (job->next_task_idx < job->task_count) {
		rocket_job_hw_submit(core, job);
		return;
	}

	core->in_flight_job = NULL;
	dma_fence_signal_locked(job->done_fence);
	pm_runtime_put_autosuspend(core->dev->dev);
}

static void rocket_job_handle_irq(struct rocket_core *core)
{
	uint32_t status, raw_status;

	pm_runtime_mark_last_busy(core->dev->dev);

	status = rocket_read(core, REG_PC_INTERRUPT_STATUS);
	raw_status = rocket_read(core, REG_PC_INTERRUPT_RAW_STATUS);

	rocket_write(core, REG_PC_OPERATION_ENABLE, 0x0);
	rocket_write(core, REG_PC_INTERRUPT_CLEAR, 0x1ffff);

	spin_lock(&core->job_lock);

	if (core->in_flight_job)
		rocket_job_handle_done(core, core->in_flight_job);

	spin_unlock(&core->job_lock);
}

static void
rocket_reset(struct rocket_core *core, struct drm_sched_job *bad)
{
	struct rocket_device *rdev = core->dev;
	bool cookie;

	if (!atomic_read(&core->reset.pending))
		return;

	/* Stop the scheduler.
	 *
	 * FIXME: We temporarily get out of the dma_fence_signalling section
	 * because the cleanup path generate lockdep splats when taking locks
	 * to release job resources. We should rework the code to follow this
	 * pattern:
	 *
	 *	try_lock
	 *	if (locked)
	 *		release
	 *	else
	 *		schedule_work_to_release_later
	 */
	drm_sched_stop(&core->sched, bad);

	cookie = dma_fence_begin_signalling();

	if (bad)
		drm_sched_increase_karma(bad);

	/* Mask job interrupts and synchronize to make sure we won't be
	 * interrupted during our reset.
	 */
	rocket_write(core, REG_PC_INTERRUPT_MASK, 0x0);
	synchronize_irq(core->irq);

	/* Handle the remaining interrupts before we reset. */
	rocket_job_handle_irq(core);

	/* Remaining interrupts have been handled, but we might still have
	 * stuck jobs. Let's make sure the PM counters stay balanced by
	 * manually calling pm_runtime_put_noidle() and
	 * rocket_devfreq_record_idle() for each stuck job.
	 * Let's also make sure the cycle counting register's refcnt is
	 * kept balanced to prevent it from running forever
	 */
	spin_lock(&core->job_lock);
	if (core->in_flight_job)
		pm_runtime_put_noidle(rdev->dev);

	core->in_flight_job = NULL;
	spin_unlock(&core->job_lock);

	/* Proceed with reset now. */
	pm_runtime_force_suspend(rdev->dev);
	pm_runtime_force_resume(rdev->dev);

	/* GPU has been reset, we can clear the reset pending bit. */
	atomic_set(&core->reset.pending, 0);

	/* Now resubmit jobs that were previously queued but didn't have a
	 * chance to finish.
	 * FIXME: We temporarily get out of the DMA fence signalling section
	 * while resubmitting jobs because the job submission logic will
	 * allocate memory with the GFP_KERNEL flag which can trigger memory
	 * reclaim and exposes a lock ordering issue.
	 */
	dma_fence_end_signalling(cookie);
	drm_sched_resubmit_jobs(&core->sched);
	cookie = dma_fence_begin_signalling();

	/* Restart the scheduler */
	drm_sched_start(&core->sched, true);

	dma_fence_end_signalling(cookie);
}

static enum drm_gpu_sched_stat rocket_job_timedout(struct drm_sched_job *sched_job)
{
	struct rocket_job *job = to_rocket_job(sched_job);
	struct rocket_device *rdev = job->rdev;
	struct rocket_core *core = sched_to_core(rdev, sched_job->sched);

	/*
	 * If the GPU managed to complete this jobs fence, the timeout is
	 * spurious. Bail out.
	 */
	if (dma_fence_is_signaled(job->done_fence))
		return DRM_GPU_SCHED_STAT_NOMINAL;

	/*
	 * rocket IRQ handler may take a long time to process an interrupt
	 * if there is another IRQ handler hogging the processing.
	 * For example, the HDMI encoder driver might be stuck in the IRQ
	 * handler for a significant time in a case of bad cable connection.
	 * In order to catch such cases and not report spurious rocket
	 * job timeouts, synchronize the IRQ handler and re-check the fence
	 * status.
	 */
	synchronize_irq(core->irq);

	if (dma_fence_is_signaled(job->done_fence)) {
		dev_warn(rdev->dev, "unexpectedly high interrupt latency\n");
		return DRM_GPU_SCHED_STAT_NOMINAL;
	}

	dev_err(rdev->dev, "gpu sched timeout");

	atomic_set(&core->reset.pending, 1);
	rocket_reset(core, sched_job);

	return DRM_GPU_SCHED_STAT_NOMINAL;
}

static void rocket_reset_work(struct work_struct *work)
{
	struct rocket_core *core;

	core = container_of(work, struct rocket_core, reset.work);
	rocket_reset(core, NULL);
}

static const struct drm_sched_backend_ops rocket_sched_ops = {
	.run_job = rocket_job_run,
	.timedout_job = rocket_job_timedout,
	.free_job = rocket_job_free
};

static irqreturn_t rocket_job_irq_handler_thread(int irq, void *data)
{
	struct rocket_core *core = data;

	rocket_job_handle_irq(core);

	return IRQ_HANDLED;
}

static irqreturn_t rocket_job_irq_handler(int irq, void *data)
{
	struct rocket_core *core = data;
	uint32_t raw_status = rocket_read(core, REG_PC_INTERRUPT_RAW_STATUS);

	WARN_ON(raw_status & PC_INTERRUPT_RAW_STATUS_DMA_READ_ERROR);
	WARN_ON(raw_status & PC_INTERRUPT_RAW_STATUS_DMA_READ_ERROR);

	if (!(raw_status & PC_INTERRUPT_RAW_STATUS_DPU_0 ||
	      raw_status & PC_INTERRUPT_RAW_STATUS_DPU_1))
		return IRQ_NONE;

	rocket_write(core, REG_PC_INTERRUPT_MASK, 0x0);

	return IRQ_WAKE_THREAD;
}

int rocket_job_init(struct rocket_core *core)
{
	struct rocket_device *rdev = core->dev;
	int ret;

	INIT_WORK(&core->reset.work, rocket_reset_work);
	spin_lock_init(&core->job_lock);

	core->irq = platform_get_irq_byname(to_platform_device(rdev->dev),
					    rdev->comp->irq_names[core->index]);
	if (core->irq < 0)
		return core->irq;

	ret = devm_request_threaded_irq(rdev->dev, core->irq,
					rocket_job_irq_handler,
					rocket_job_irq_handler_thread,
					IRQF_SHARED, KBUILD_MODNAME "-job",
					core);
	if (ret) {
		dev_err(rdev->dev, "failed to request job irq");
		return ret;
	}

	core->reset.wq = alloc_ordered_workqueue("rocket-reset-%d", 0, core->index);
	if (!core->reset.wq)
		return -ENOMEM;

	core->fence_context = dma_fence_context_alloc(1);

	ret = drm_sched_init(&core->sched,
				&rocket_sched_ops, NULL,
				DRM_SCHED_PRIORITY_COUNT,
				1, 0,
				msecs_to_jiffies(JOB_TIMEOUT_MS),
				core->reset.wq,
				NULL, "rocket", rdev->dev);
	if (ret) {
		dev_err(rdev->dev, "Failed to create scheduler: %d.", ret);
		goto err_sched;
	}

	return 0;

err_sched:
	drm_sched_fini(&core->sched);

	destroy_workqueue(core->reset.wq);
	return ret;
}

void rocket_job_fini(struct rocket_core *core)
{
	drm_sched_fini(&core->sched);

	cancel_work_sync(&core->reset.work);
	destroy_workqueue(core->reset.wq);
}

int rocket_job_open(struct rocket_file_priv *rocket_priv)
{
	struct rocket_device *rdev = rocket_priv->rdev;
	struct drm_gpu_scheduler **scheds = kmalloc_array(rdev->comp->num_cores, sizeof(scheds), GFP_KERNEL);
	unsigned int core;
	int ret;

	for (core = 0; core < rdev->comp->num_cores; core++)
		scheds[core] = &rdev->cores[core].sched;

	ret = drm_sched_entity_init(&rocket_priv->sched_entity,
				    DRM_SCHED_PRIORITY_NORMAL,
				    scheds,
				    3, NULL);
	if (WARN_ON(ret))
		return ret;

	return 0;
}

void rocket_job_close(struct rocket_file_priv *rocket_priv)
{
	struct drm_sched_entity *entity = &rocket_priv->sched_entity;

	kfree(entity->sched_list);
	drm_sched_entity_destroy(entity);
}

int rocket_job_is_idle(struct rocket_device *rdev)
{
	unsigned int core;

	for (core = 0; core < rdev->comp->num_cores; core++) {
		/* If there are any jobs in any HW queue, we're not idle */
		if (atomic_read(&rdev->cores[core].sched.credit_count))
			return false;
	}

	return true;
}

static int rocket_ioctl_submit_job(struct drm_device *dev, struct drm_file *file, struct drm_rocket_job *job)
{
	struct rocket_device *rdev = dev->dev_private;
	struct rocket_file_priv *file_priv = file->driver_priv;
	struct rocket_job *rjob = NULL;
	int ret = 0;

	if (job->task_count == 0)
		return -EINVAL;

	rjob = kzalloc(sizeof(*rjob), GFP_KERNEL);
	if (!rjob)
		return -ENOMEM;

	kref_init(&rjob->refcount);

	rjob->rdev = rdev;

	ret = drm_sched_job_init(&rjob->base,
				 &file_priv->sched_entity,
				 1, NULL);
	if (ret)
		goto out_put_job;

	ret = rocket_copy_tasks(dev, file, job, rjob);
	if (ret)
		goto out_cleanup_job;

	ret = drm_gem_objects_lookup(file,
				     (void __user *)(uintptr_t)job->in_bo_handles,
				     job->in_bo_handle_count, &rjob->in_bos);
	if (ret)
		goto out_cleanup_job;

	rjob->in_bo_count = job->in_bo_handle_count;

	ret = drm_gem_objects_lookup(file,
				     (void __user *)(uintptr_t)job->out_bo_handles,
				     job->out_bo_handle_count, &rjob->out_bos);
	if (ret)
		goto out_cleanup_job;

	rjob->out_bo_count = job->out_bo_handle_count;

	ret = rocket_job_push(rjob);
	if (ret)
		goto out_cleanup_job;

out_cleanup_job:
	if (ret)
		drm_sched_job_cleanup(&rjob->base);
out_put_job:
	rocket_job_put(rjob);

	return ret;
}

int rocket_ioctl_submit(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_submit *args = data;
	struct drm_rocket_job *jobs;
	int ret = 0;
	unsigned int i = 0;

	jobs = kvmalloc_array(args->job_count, sizeof(*jobs), GFP_KERNEL);
	if (!jobs) {
		DRM_DEBUG("Failed to allocate incoming job array\n");
		return -ENOMEM;
	}

	if (copy_from_user(jobs,
			   (void __user *)(uintptr_t)args->jobs,
			   args->job_count * sizeof(*jobs))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy incoming job array\n");
		goto exit;
	}

	for (i = 0; i < args->job_count; i++)
		rocket_ioctl_submit_job(dev, file, &jobs[i]);

exit:
	kfree(jobs);

	return ret;
}
