#ifndef BC250_DESCRIPTOR_BINDING_H
#define BC250_DESCRIPTOR_BINDING_H
/* Shared by the vkd3d interface map, reflection and descriptor writer.
 * VS and PS have independent D3D register namespaces. Set is always zero. */
#define BC250_DESCRIPTOR_STAGE_STRIDE 256u
#define BC250_DESCRIPTOR_CB_BASE 0u
#define BC250_DESCRIPTOR_CB_COUNT 14u
#define BC250_DESCRIPTOR_SRV_BASE 32u
#define BC250_DESCRIPTOR_SRV_COUNT 128u
#define BC250_DESCRIPTOR_SAMPLER_BASE 160u
#define BC250_DESCRIPTOR_SAMPLER_COUNT 16u
#define BC250_DESCRIPTOR_STAGE_COUNT 2u
#define BC250_DESCRIPTOR_MAX_BINDINGS 316u
#endif
