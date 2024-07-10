/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dev/intel_device_info.h"
#include "intel_nir.h"
#include "isl/isl.h"
#include "nir_builder.h"

static bool
intel_nir_blockify_uniform_loads_instr(nir_builder *b,
                                       nir_instr *instr,
                                       void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   const struct intel_device_info *devinfo = cb_data;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
      /* BDW PRMs, Volume 7: 3D-Media-GPGPU: OWord Block ReadWrite:
       *
       *    "The surface base address must be OWord-aligned."
       *
       * We can't make that guarantee with SSBOs where the alignment is
       * 4bytes.
       */
      if (devinfo->ver < 9)
         return false;

      if (nir_src_is_divergent(intrin->src[1]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we can only do block loads of at least 4dwords (1
       * oword).
       */
      if (!devinfo->has_lsc && intrin->def.num_components < 4)
         return false;

      intrin->intrinsic =
         intrin->intrinsic == nir_intrinsic_load_ubo ?
         nir_intrinsic_load_ubo_uniform_block_intel :
         nir_intrinsic_load_ssbo_uniform_block_intel;
      return true;

   case nir_intrinsic_load_shared:
      /* Block loads on shared memory are not supported before Icelake. */
      if (devinfo->ver < 11)
         return false;

      if (nir_src_is_divergent(intrin->src[0]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we have to use OWord Block Load messages (the one
       * that requires OWord aligned offsets, too).
       */
      if (!devinfo->has_lsc &&
          (intrin->def.num_components < 4 ||
           nir_intrinsic_align(intrin) < 16))
         return false;

      intrin->intrinsic = nir_intrinsic_load_shared_uniform_block_intel;
      return true;

   case nir_intrinsic_load_global_constant:
      if (nir_src_is_divergent(intrin->src[0]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we can only do block loads of at least 4dwords (1
       * oword).
       */
      if (!devinfo->has_lsc && intrin->def.num_components < 4)
         return false;

      intrin->intrinsic = nir_intrinsic_load_global_constant_uniform_block_intel;
      return true;

   case nir_intrinsic_load_global_const_block_intel:
      /* Only deal with the simple predication true case */
      if (!nir_src_is_const(intrin->src[1]) ||
          nir_src_as_uint(intrin->src[1]) == 0)
         return false;

      if (nir_src_is_divergent(intrin->src[0]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we can only do block loads of at least 4dwords (1
       * oword).
       */
      if (!devinfo->has_lsc && intrin->def.num_components < 4)
         return false;

      b->cursor = nir_before_instr(&intrin->instr);
      nir_def *def =
         nir_load_global_constant_uniform_block_intel(
            b,
            intrin->def.num_components,
            intrin->def.bit_size,
            intrin->src[0].ssa,
            .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER,
            .align_mul = 4,
            .align_offset = 4);

      nir_def_rewrite_uses(&intrin->def, def);
      nir_instr_remove(&intrin->instr);
      return true;

   default:
      return false;
   }
}

bool
intel_nir_blockify_uniform_loads(nir_shader *shader,
                                 const struct intel_device_info *devinfo)
{
   return nir_shader_instructions_pass(shader,
                                       intel_nir_blockify_uniform_loads_instr,
                                       nir_metadata_control_flow |
                                       nir_metadata_live_defs,
                                       (void *) devinfo);
}