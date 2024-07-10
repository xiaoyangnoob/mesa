/*
 * Copyright © 2016 Red Hat
 * SPDX-License-Identifier: MIT
 *
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_wsi.h"

#include "vk_util.h"
#include "wsi_common_drm.h"
#include "drm-uapi/drm_fourcc.h"

#include "tu_device.h"

static void
kgsl_get_info(VkPhysicalDevice _pdevice,
                       VkDeviceMemory _memory,
                       int *fd,
                       uint32_t *offset)
{
   VK_FROM_HANDLE(tu_physical_device, pdevice, _pdevice);
   VK_FROM_HANDLE(tu_device_memory, memory, _memory);
   *fd = pdevice->local_fd;
   *offset = memory->bo->gem_handle << 12;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
tu_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(tu_physical_device, pdevice, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(&pdevice->instance->vk, pName);
}

static bool
tu_wsi_can_present_on_device(VkPhysicalDevice physicalDevice, int fd)
{
   return true;
}

VkResult
tu_wsi_init(struct tu_physical_device *physical_device)
{
   VkResult result;

   const struct wsi_device_options options = { .sw_device = false };
   result = wsi_device_init(&physical_device->wsi_device,
                            tu_physical_device_to_handle(physical_device),
                            tu_wsi_proc_addr,
                            &physical_device->instance->vk.alloc,
                            physical_device->master_fd,
                            &physical_device->instance->dri_options,
                            &options);
   if (result != VK_SUCCESS)
      return result;

   if (strcmp(physical_device->instance->knl->name, "kgsl") == 0) {
      physical_device->wsi_device.kgsl_get_info = kgsl_get_info;
      physical_device->wsi_device.is_tu_kgsl = true;
   }

   physical_device->wsi_device.supports_modifiers = true;
   physical_device->wsi_device.can_present_on_device =
      tu_wsi_can_present_on_device;

   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
tu_wsi_finish(struct tu_physical_device *physical_device)
{
   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->vk.alloc);
}
