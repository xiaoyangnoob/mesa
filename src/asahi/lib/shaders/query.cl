/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * Copyright 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "libagx.h"
#include "query.h"

static inline void
write_query_result(uintptr_t dst_addr, int32_t idx, bool is_64, uint64_t result)
{
   if (is_64) {
      global uint64_t *out = (global uint64_t *)dst_addr;
      out[idx] = result;
   } else {
      global uint32_t *out = (global uint32_t *)dst_addr;
      out[idx] = result;
   }
}

void
libagx_copy_query(constant struct libagx_copy_query_push *push, unsigned i)
{
   uint64_t dst = push->dst_addr + (((uint64_t)i) * push->dst_stride);
   uint32_t query = push->first_query + i;
   bool available = push->availability[query];

   if (available || push->partial) {
      /* For occlusion queries, results[] points to the device global heap. We
       * need to remap indices according to the query pool's allocation.
       */
      uint result_index = push->oq_index ? push->oq_index[query] : query;
      uint idx = result_index * push->reports_per_query;

      for (unsigned i = 0; i < push->reports_per_query; ++i) {
         write_query_result(dst, i, push->_64, push->results[idx + i]);
      }
   }

   if (push->with_availability) {
      write_query_result(dst, push->reports_per_query, push->_64, available);
   }
}

void
libagx_copy_xfb_counters(constant struct libagx_xfb_counter_copy *push)
{
   unsigned i = get_local_id(0);

   *(push->dest[i]) = push->src[i] ? *(push->src[i]) : 0;
}
