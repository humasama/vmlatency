/*
 * Copyright (c) 2017 Evgenii Iuliugin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __ASM_INLINES_H__
#define __ASM_INLINES_H__

#include "types.h"
#include "cpu-defs.h"

#pragma pack(push, 1)
typedef struct {
        u16 limit;
        u64 base;
} descriptor_t;
#pragma pack(pop)

#if defined(__GNUC__) || defined(__INTEL_COMPILER)

#define SAVE_RFLAGS(rflags) \
        "pushfq;"           \
        "popq %0;"          \
        :"=r"(rflags)

static inline void
__cpuid_all(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
        __asm__ __volatile__(
                "cpuid"
                :"=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                :"a"(leaf), "c"(subleaf));
}

static inline u32
__cpuid_ecx(u32 leaf, u32 subleaf)
{
        u32 eax, ebx, ecx, edx;
        __cpuid_all(leaf, subleaf, &eax, &ebx, &ecx, &edx);
        return ecx;
}

static inline u64 __rdmsr(u32 msr_num)
{
        u32 eax, edx;
        __asm__ __volatile__(
                "rdmsr"
                :"=a"(eax), "=d"(edx)
                :"c"(msr_num));
        return ((u64)edx << 32) | eax;
}

static inline u64
__get_cr0(void)
{
        u64 cr0;
        __asm__ __volatile__(
                "movq %%cr0, %0"
                :"=r"(cr0):);
        return cr0;
}

static inline u64
__get_cr3(void)
{
        u64 cr3;
        __asm__ __volatile__(
                "movq %%cr3, %0"
                :"=r"(cr3):);
        return cr3;
}

static inline u64
__get_cr4(void)
{
        u64 cr4;
        __asm__ __volatile__(
                "movq %%cr4, %0"
                :"=r"(cr4):);
        return cr4;
}

static inline void
__set_cr4(u64 cr4)
{
        __asm__ __volatile__(
                "movq %0, %%cr4"
                ::"r"(cr4));
}

static inline u16
__get_es(void)
{
        u16 es;
        __asm__ __volatile__("movw %%es, %0" :"=r"(es));
        return es;
}

static inline u16
__get_cs(void)
{
        u16 cs;
        __asm__ __volatile__("movw %%cs, %0" :"=r"(cs));
        return cs;
}

static inline u16
__get_ss(void)
{
        u16 ss;
        __asm__ __volatile__("movw %%ss, %0" :"=r"(ss));
        return ss;
}

static inline u16
__get_ds(void)
{
        u16 ds;
        __asm__ __volatile__("movw %%ds, %0" :"=r"(ds));
        return ds;
}

static inline u16
__get_fs(void)
{
        u16 fs;
        __asm__ __volatile__("movw %%fs, %0" :"=r"(fs));
        return fs;
}

static inline u16
__get_gs(void)
{
        u16 gs;
        __asm__ __volatile__("movw %%gs, %0" :"=r"(gs));
        return gs;
}

static inline u32
__lar(u16 seg)
{
        u32 attrs;
        __asm__ __volatile__("lar %1, %0":"=r"(attrs):"r"((u32)seg));
        return attrs;
}

static inline u16
__lsl(u16 seg)
{
        u16 limit;
        __asm__ __volatile__("lsl %1, %0":"=r"(limit):"r"(seg));
        return limit;
}

static inline u16
__sldt(void)
{
        u16 ldtr;
        __asm__ __volatile__("sldt %0" :"=r"(ldtr));
        return ldtr;
}

static inline void
__get_gdt(descriptor_t *gdtr)
{
        __asm__ __volatile__("sgdt %0":"=m"(*gdtr));
}

static inline void
__set_gdt(descriptor_t *gdtr)
{
        __asm__ __volatile__("lgdt %0"::"m"(*gdtr));
}

static inline void
__get_idt(descriptor_t *idtr)
{
        __asm__ __volatile__("sidt %0":"=m"(*idtr));
}

static inline void
__set_idt(descriptor_t *idtr)
{
        __asm__ __volatile__("lidt %0"::"m"(*idtr));
}

static inline u16
__str(void)
{
        u16 tr;
        __asm__ __volatile__("str %0" :"=r"(tr));
        return tr;
}

static inline int
__vmxon(uintptr_t vmxon_region_pa)
{
        u64 rflags;
        __asm__ __volatile__(
                "vmxon %1;"
                SAVE_RFLAGS(rflags)
                :"m"(vmxon_region_pa));

        rflags &= RFLAGS_CF | RFLAGS_ZF;
        if (rflags == RFLAGS_CF)
                return -1;  /* VMXON failed */
        if (rflags == (RFLAGS_CF | RFLAGS_ZF))
                return 1;  /* VMX is already on */

        return 0;
}

static inline void
__vmxoff(void)
{
        __asm__ __volatile__("vmxoff");
}

static inline int
__vmptrld(uintptr_t vmcs_pa)
{
        u64 rflags;
        __asm__ __volatile__(
                "vmptrld %1;"
                SAVE_RFLAGS(rflags)
                :"m"(vmcs_pa));
        if (rflags & (RFLAGS_CF | RFLAGS_ZF))
                return -1;
        return 0;
}

static inline int
__vmclear(uintptr_t vmcs_pa)
{
        u64 rflags;
        __asm__ __volatile__(
                "vmclear %1;"
                SAVE_RFLAGS(rflags)
                :"m"(vmcs_pa));
        if (rflags & (RFLAGS_CF | RFLAGS_ZF))
                return -1;
        return 0;
}

static inline void
__vmwrite(u64 field, u64 value)
{
        __asm__ __volatile__(
                "vmwrite %1, %0;"
                ::"r"(field), "m"(value));
}

static inline u64
__vmread(u64 field)
{
        u64 ret;
        __asm__ __volatile__(
                "vmread %1, %0"
                :"=r"(ret): "r"(field));
        return ret;
}

static inline u64
__get_tsc(void)
{
        u32 eax, edx;
        __asm__ __volatile__(
                "rdtsc"
                :"=a"(eax), "=d"(edx));
        return ((u64)edx << 32) | eax;
}

static inline u64
__get_rflags(void)
{
        u64 rflags;
        __asm__ __volatile__(SAVE_RFLAGS(rflags));
        return rflags;
}

#else  /* !__GNUC__ */

extern void __cpuid_all(u32 l, u32 subl, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
extern u32 __cpuid_ecx(u32 leaf, u32 subleaf);

extern u64 __rdmsr(u32 msr_num);

extern u64 __get_cr0(void);
extern u64 __get_cr3(void);
extern u64 __get_cr4(void);
extern void __set_cr4(u64 cr4);

extern u16 __get_es(void);
extern u16 __get_cs(void);
extern u16 __get_ss(void);
extern u16 __get_ds(void);
extern u16 __get_fs(void);
extern u16 __get_gs(void);

extern u32 __lar(u16 seg);
extern u16 __lsl(u16 seg);

extern u16 __sldt(void);
extern void __get_gdt(descriptor_t *gdt);
extern void __set_gdt(descriptor_t *gdt);
extern void __get_idt(descriptor_t *idt);
extern void __set_idt(descriptor_t *idt);
extern u16 __str(void);

extern int __vmxon(uintptr_t vmxon_region_pa);
extern void __vmxoff(void);
extern int __vmptrld(uintptr_t vmcs_pa);
extern int __vmclear(uintptr_t vmcs_pa);
extern void __vmwrite(u64 field, u64 value);
extern u64 __vmread(u64 field);

extern u64 __get_tsc(void);

extern u64 __get_rflags(void);

#endif /* !__GNUC__ */

extern int do_vmlaunch(void);
extern int do_vmresume(void);

#endif /* __ASM_INLINES_H__ */
