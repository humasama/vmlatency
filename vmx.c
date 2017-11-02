/*
 * Copyright (c) 2017 Evgeny Yulyugin
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

#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <asm/io.h>

#include "vmx.h"
#include "asm-inlines.h"
#include "cpu-defs.h"

static inline int
allocate_vmpage(vmpage_t *p)
{
        p->page = alloc_page(GFP_KERNEL | __GFP_ZERO);
        if (!p->page)
                return -1;

        p->p = kmap(p->page);
        p->pa = page_to_phys(p->page);
        return 0;
}

static inline void
free_vmpage(vmpage_t *p)
{
        kunmap(p->page);
        __free_page(p->page);
        p->page = NULL;
        p->p = NULL;
        p->pa = 0;
}

int
vmlatency_printk(const char *fmt, ...)
{
        int ret;
        va_list va;
        va_start(va, fmt);
        ret = vprintk(fmt, va);
        va_end(va);
        return ret;
}

static inline int
allocate_memory(vm_monitor_t *vmm)
{
        int cnt = 0;
        if (allocate_vmpage(&vmm->vmxon_region) == 0) cnt++; else return -cnt;
        if (allocate_vmpage(&vmm->vmcs) == 0) cnt++; else return -cnt;
        if (allocate_vmpage(&vmm->io_bitmap_a) == 0) cnt++; else return -cnt;
        if (allocate_vmpage(&vmm->io_bitmap_b) == 0) cnt++; else return -cnt;
        if (allocate_vmpage(&vmm->msr_bitmap) == 0) cnt++; else return -cnt;

        return cnt;
}

static inline void
free_memory(vm_monitor_t *vmm, int cnt)
{
        if (cnt == 5) { free_vmpage(&vmm->msr_bitmap); cnt--; }
        if (cnt == 4) { free_vmpage(&vmm->io_bitmap_b); cnt--; }
        if (cnt == 3) { free_vmpage(&vmm->io_bitmap_a); cnt--; }
        if (cnt == 2) { free_vmpage(&vmm->vmxon_region); cnt--; }
        if (cnt == 1) { free_vmpage(&vmm->vmcs); cnt--; }
}

static inline bool
has_vmx(void)
{
        u32 ecx = __cpuid_ecx(1, 0);
        return ecx & CPUID_1_ECX_VMX;
}

bool
vmx_enabled()
{
        u64 feature_control;
        if (!has_vmx()) {
                vmlatency_printk("VMX is not supported\n");
                return false;
        }

        feature_control = __rdmsr(IA32_FEATURE_CONTROL);
        if (!(feature_control & FEATURE_CONTROL_LOCK_BIT) ||
            !(feature_control & FEATURE_CONTROL_VMX_OUTSIDE_SMX_ENABLE_BIT)) {
                vmlatency_printk("VMX is not enabled in BIOS\n");
                return false;
        }

        vmlatency_printk("VMX is supported by CPU\n");
        return true;
}

static inline u32
get_vmcs_revision_identifier(void)
{
        return 0x8fffffff & __rdmsr(IA32_VMX_BASIC);
}

static inline void
vmxon_setup_revision_id(vm_monitor_t *vmm)
{
        ((u32 *)vmm->vmxon_region.p)[0] = get_vmcs_revision_identifier();
}

static inline void
vmcs_setup_revision_id(vm_monitor_t *vmm)
{
        ((u32 *)vmm->vmcs.p)[0] = get_vmcs_revision_identifier();
}

static inline int
do_vmxon(vm_monitor_t *vmm)
{
        u64 old_cr4 = __get_cr4();
        vmm->old_vmxe = old_cr4 & CR4_VMXE;

        /* Set CR4.VMXE if necessary */
        if (!vmm->old_vmxe)
                __set_cr4(old_cr4 | CR4_VMXE);

        if (__vmxon(vmm->vmxon_region.pa) != 0) {
                vmlatency_printk("VMXON failed\n");
                return -1;
        }

        vmlatency_printk("VMXON succeeded\n");
        return 0;
}

static inline void
do_vmxoff(vm_monitor_t *vmm)
{
        if (__vmxoff() != 0)
                vmlatency_printk("VMXOFF failed\n");

        /* Clear CR4.VMXE if necessary */
        if (!vmm->old_vmxe)
                __set_cr4(__get_cr4() & ~CR4_VMXE);

        vmlatency_printk("VMXOFF succeeded\n");
}

static inline int
do_vmptrld(vm_monitor_t *vmm)
{
        if (__vmptrld(vmm->vmcs.pa) != 0){
                vmlatency_printk("VMPTRLD failed\n");
                return -1;
        }

        return 0;
}

static inline int
do_vmclear(vm_monitor_t *vmm)
{
        if (__vmclear(vmm->vmcs.pa) != 0){
                vmlatency_printk("VMCLEAR failed\n");
                return -1;
        }

        return 0;
}

/* Initialize guest state to match host state */
static inline void
initialize_vmcs(vm_monitor_t *vmm)
{
        u16 val16;

        /* Segment registers */
        __asm__ __volatile__("movw %%es, %0" :"=r"(val16));
        __vmwrite(VMCS_HOST_ES, val16);
        __vmwrite(VMCS_GUEST_ES, val16);
        __vmwrite(VMCS_GUEST_ES_BASE, 0);
        __vmwrite(VMCS_GUEST_ES_LIMIT, 0xffffffff);
        //__vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, );

        __asm__ __volatile__("movw %%cs, %0" :"=r"(val16));
        __vmwrite(VMCS_HOST_CS, val16);
        __vmwrite(VMCS_GUEST_CS, val16);
        __vmwrite(VMCS_GUEST_CS_BASE, 0);
        __vmwrite(VMCS_GUEST_CS_LIMIT, 0xffffffff);
        __vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, __get_segment_ar(val16));

        __asm__ __volatile__("movw %%ss, %0" :"=r"(val16));
        __vmwrite(VMCS_HOST_SS, val16);
        __vmwrite(VMCS_GUEST_SS, val16);
        __vmwrite(VMCS_GUEST_SS_BASE, 0);
        __vmwrite(VMCS_GUEST_SS_LIMIT, 0xffffffff);
        __vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, __get_segment_ar(val16));

        __asm__ __volatile__("movw %%ds, %0" :"=r"(val16));
        __vmwrite(VMCS_HOST_DS, val16);
        __vmwrite(VMCS_GUEST_DS, val16);
        __vmwrite(VMCS_GUEST_DS_BASE, 0);
        __vmwrite(VMCS_GUEST_DS_LIMIT, 0xffffffff);
        //__vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, );

        __asm__ __volatile__("movw %%fs, %0" :"=r"(val16));
        __vmwrite(VMCS_HOST_FS, val16);
        __vmwrite(VMCS_GUEST_FS, val16);
        u64 fs_base = __rdmsr(IA32_FS_BASE);
        __vmwrite(VMCS_GUEST_FS_BASE, fs_base);
        __vmwrite(VMCS_HOST_FS_BASE, fs_base);
        __vmwrite(VMCS_GUEST_FS_LIMIT, 0xffffffff);
        //__vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, );

        __asm__ __volatile__("movw %%gs, %0" :"=r"(val16));
        __vmwrite(VMCS_HOST_GS, val16);
        __vmwrite(VMCS_GUEST_GS, val16);
        u64 gs_base = __rdmsr(IA32_GS_BASE);
        __vmwrite(VMCS_GUEST_GS_BASE, gs_base);
        __vmwrite(VMCS_HOST_GS_BASE, gs_base);
        __vmwrite(VMCS_GUEST_GS_LIMIT, 0xffffffff);
        //__vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, );

        __asm__ __volatile__("sldt %0" :"=r"(val16));
        __vmwrite(VMCS_GUEST_LDTR, val16);
        __vmwrite(VMCS_GUEST_LDTR_BASE, 0);
        __vmwrite(VMCS_GUEST_LDTR_LIMIT, 0xffffffff);
        //__vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, );

        u64 gdtr;
        __asm__ __volatile__("sgdt %0":"=m"(gdtr));
        u64 gdtr_limit = gdtr & 0xffff;
        u64 gdtr_base = gdtr >> 16;
        /* Make GDTR.base canonical */
        if (gdtr & 0x8000000000000000ull)
                gdtr_base |= 0xffff000000000000ull;
        __vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr_limit);
        __vmwrite(VMCS_GUEST_GDTR_BASE, gdtr_base);
        __vmwrite(VMCS_HOST_GDTR_BASE, gdtr_base);

        u64 idtr;
        __asm__ __volatile__("sidt %0":"=m"(idtr));
        u64 idtr_limit = idtr & 0xffff;
        u64 idtr_base = idtr >> 16;
        /* Make IDTR.base canonical */
        if (idtr & 0x8000000000000000ull)
                idtr_base |= 0xffff000000000000ull;
        __vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr_limit);
        __vmwrite(VMCS_GUEST_IDTR_BASE, idtr_base);
        __vmwrite(VMCS_HOST_IDTR_BASE, idtr_base);

        u16 tr, tr_limit;
        __asm__ __volatile__("str %0" :"=r"(tr));
        __asm__ __volatile__("lsl %1, %0":"=r"(tr_limit):"r"(tr));
        __vmwrite(VMCS_GUEST_TR, tr);
        __vmwrite(VMCS_HOST_TR, tr);
        __vmwrite(VMCS_GUEST_TR_LIMIT, tr_limit);
        __vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, __get_segment_ar(val16));
        /* Extracting TR.base for GDT */
        u64 trdesc_lo = ((u64*)(gdtr_base + tr))[0];
        u64 trbase = ((trdesc_lo >> 16) & 0xffffff)
                   | (((trdesc_lo >> 56) & 0xff) << 24);
        u64 trdesc_hi = ((u64*)(gdtr_base + tr))[1];
        trbase |= trdesc_hi << 32;
        __vmwrite(VMCS_GUEST_TR_BASE, trbase);
        __vmwrite(VMCS_HOST_TR_BASE, trbase);

        /* 64-bit control fields */
        __vmwrite(VMCS_IO_BITMAP_A_ADDR, vmm->io_bitmap_a.pa);
        __vmwrite(VMCS_IO_BITMAP_B_ADDR, vmm->io_bitmap_b.pa);
        __vmwrite(VMCS_EXEC_VMCS_PTR, 0);
        __vmwrite(VMCS_TSC_OFFSET, 0);

        /* 64-bit guest state */
        __vmwrite(VMCS_VMCS_LINK_PTR, 0xffffffffffffffffull);
        __vmwrite(VMCS_GUEST_IA32_DEBUGCTL, 0);

        /* 32-bit control fields */
        __vmwrite(VMCS_PIN_BASED_VM_CTLS, vmm->pinbased_allowed0 &
                                          vmm->pinbased_allowed1);
        /* Secondary controls are not activated */
        __vmwrite(VMCS_PROC_BASED_VM_CTLS, vmm->procbased_allowed0 &
                                           vmm->procbased_allowed1);
        __vmwrite(VMCS_EXCEPTION_BITMAP, 0xffffffff);
        __vmwrite(VMCS_PF_ECODE_MASK, 0);
        __vmwrite(VMCS_PF_ECODE_MATCH, 0);
        __vmwrite(VMCS_CR3_TARGET_CNT, 0);
        __vmwrite(VMCS_VMEXIT_CTLS, (vmm->exit_ctls_allowed0 &
                                     vmm->exit_ctls_allowed1) |
                                    VMCS_VMEXIT_CTL_HOST_ADDR_SPACE_SIZE);
        __vmwrite(VMCS_VMEXIT_MSR_STORE_CNT, 0);
        __vmwrite(VMCS_VMEXIT_MSR_LOAD_CNT, 0);
        __vmwrite(VMCS_VMENTRY_CTLS, vmm->entry_ctls_allowed0 &
                                     vmm->entry_ctls_allowed1);
        __vmwrite(VMCS_VMENTRY_MSR_LOAD_CNT, 0);
        __vmwrite(VMCS_VMENTRY_INT_INFO, 0);
        __vmwrite(VMCS_VMENTRY_ECODE, 0);
        __vmwrite(VMCS_VMENTRY_INSTR_LEN, 0);

        __vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);
        __vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);
        __vmwrite(VMCS_GUEST_SMBASE, 0);

        /* 32-bit guest state*/
        u32 ia32_sysenter_cs = __rdmsr(IA32_SYSENTER_CS);
        __vmwrite(VMCS_GUEST_IA32_SYSENTER_CS, ia32_sysenter_cs);
        __vmwrite(VMCS_HOST_IA32_SYSENTER_CS, ia32_sysenter_cs);

        /* Control registers */
        u32 cr0 = __get_cr0();
        __vmwrite(VMCS_GUEST_CR0, cr0);
        __vmwrite(VMCS_HOST_CR0, cr0);
        __vmwrite(VMCS_CR0_GUEST_HOST_MASK, 0);
        __vmwrite(VMCS_CR0_READ_SHADOW, 0);

        u32 cr4 = __get_cr4();
        __vmwrite(VMCS_GUEST_CR4, cr4);
        __vmwrite(VMCS_HOST_CR4, cr4);
        __vmwrite(VMCS_CR4_GUEST_HOST_MASK, 0);
        __vmwrite(VMCS_CR4_READ_SHADOW, 0);

        u32 cr3 = __get_cr3();
        __vmwrite(VMCS_GUEST_CR3, cr3);
        __vmwrite(VMCS_HOST_CR3, cr3);
        __vmwrite(VMCS_CR3_TARGET_VALUE_0, 0);
        __vmwrite(VMCS_CR3_TARGET_VALUE_1, 0);
        __vmwrite(VMCS_CR3_TARGET_VALUE_2, 0);
        __vmwrite(VMCS_CR3_TARGET_VALUE_3, 0);

        /* Natural-width guest/host state */
        __vmwrite(VMCS_GUEST_DR7, 0x400); /* Initial value */

        u64 rsp;
        __asm__ __volatile__("mov %%rsp, %0":"=r"(rsp));
        __vmwrite(VMCS_GUEST_RSP, rsp);
        __vmwrite(VMCS_HOST_RSP, rsp);

        u64 rflags;
        __asm__ __volatile__(SAVE_RFLAGS(rflags));
        __vmwrite(VMCS_GUEST_RFLAGS, rflags);

        __vmwrite(VMCS_GUEST_PENDING_DBG_EXCEPTION, 0);

        u64 ia32_sysenter_esp = __rdmsr(IA32_SYSENTER_ESP);
        __vmwrite(VMCS_GUEST_IA32_SYSENTER_ESP, ia32_sysenter_esp);
        __vmwrite(VMCS_HOST_IA32_SYSENTER_ESP, ia32_sysenter_esp);

        u64 ia32_sysenter_eip = __rdmsr(IA32_SYSENTER_EIP);
        __vmwrite(VMCS_GUEST_IA32_SYSENTER_EIP, ia32_sysenter_esp);
        __vmwrite(VMCS_HOST_IA32_SYSENTER_EIP, ia32_sysenter_eip);
}

void
print_vmx_info()
{
        vmlatency_printk("VMCS revision identifier: %#lx\n",
                         get_vmcs_revision_identifier());

        u64 ia32_vmx_basic = __rdmsr(IA32_VMX_BASIC);
        vmlatency_printk("IA32_VMX_BASIC (%#x): %#llx\n", IA32_VMX_BASIC,
                         ia32_vmx_basic);
        bool has_true_ctls = ia32_vmx_basic & __BIT(55);

        u64 ia32_vmx_pinbased_ctls = __rdmsr(IA32_VMX_PINBASED_CTLS);
        vmlatency_printk("IA32_VMX_PINBASED_CTLS (%#x): %#llx\n",
                         IA32_VMX_PINBASED_CTLS, ia32_vmx_pinbased_ctls);

        if (has_true_ctls) {
                u64 ia32_vmx_true_pinbased_ctls =
                        __rdmsr(IA32_VMX_TRUE_PINBASED_CTLS);
                vmlatency_printk("IA32_VMX_TRUE_PINBASED_CTLS (%#x): %#llx\n",
                                 IA32_VMX_TRUE_PINBASED_CTLS,
                                 ia32_vmx_true_pinbased_ctls);
        }

        u64 ia32_vmx_procbased_ctls = __rdmsr(IA32_VMX_PROCBASED_CTLS);
        vmlatency_printk("IA32_VMX_PROCBASED_CTLS (%#x): %#llx\n",
                         IA32_VMX_PROCBASED_CTLS, ia32_vmx_procbased_ctls);

        if (has_true_ctls) {
                u64 ia32_vmx_true_procbased_ctls =
                        __rdmsr(IA32_VMX_TRUE_PROCBASED_CTLS);
                vmlatency_printk("IA32_VMX_TRUE_PROCBASED_CTLS (%#x): %#llx\n",
                                 IA32_VMX_TRUE_PROCBASED_CTLS,
                                 ia32_vmx_true_procbased_ctls);
        }
}

static void
cache_vmx_capabilities(vm_monitor_t *vmm)
{
        vmm->ia32_vmx_basic = __rdmsr(IA32_VMX_BASIC);
        vmm->vmcs_revision_id = vmm->ia32_vmx_basic & 0x8fffffff;
        vmm->has_true_ctls = vmm->ia32_vmx_basic & __BIT(55);

        vmm->ia32_vmx_pinbased_ctls = __rdmsr(IA32_VMX_PINBASED_CTLS);
        vmm->ia32_vmx_procbased_ctls = __rdmsr(IA32_VMX_PROCBASED_CTLS);
        vmm->ia32_vmx_exit_ctls = __rdmsr(IA32_VMX_EXIT_CTLS);
        vmm->ia32_vmx_entry_ctls = __rdmsr(IA32_VMX_ENTRY_CTLS);

        if (vmm->has_true_ctls) {
                vmm->ia32_vmx_true_pinbased_ctls =
                        __rdmsr(IA32_VMX_TRUE_PINBASED_CTLS);
                vmm->ia32_vmx_true_procbased_ctls =
                        __rdmsr(IA32_VMX_PROCBASED_CTLS);
                vmm->ia32_vmx_true_exit_ctls = __rdmsr(IA32_VMX_TRUE_EXIT_CTLS);
                vmm->ia32_vmx_true_entry_ctls =
                        __rdmsr(IA32_VMX_TRUE_ENTRY_CTLS);
        }

        vmm->pinbased_allowed0 = 0xffffffff &
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_pinbased_ctls
                                    : vmm->ia32_vmx_pinbased_ctls);
        vmm->pinbased_allowed1 =
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_pinbased_ctls
                                    : vmm->ia32_vmx_pinbased_ctls) >> 32;

        vmm->procbased_allowed0 = 0xffffffff &
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_procbased_ctls
                                    : vmm->ia32_vmx_procbased_ctls);
        vmm->procbased_allowed1 =
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_procbased_ctls
                                    : vmm->ia32_vmx_procbased_ctls) >> 32;

        vmm->exit_ctls_allowed0 = 0xffffffff &
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_exit_ctls
                                    : vmm->ia32_vmx_exit_ctls);
        vmm->exit_ctls_allowed1 =
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_exit_ctls
                                    : vmm->ia32_vmx_exit_ctls) >> 32;

        vmm->entry_ctls_allowed0 = 0xffffffff &
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_entry_ctls
                                    : vmm->ia32_vmx_entry_ctls);
        vmm->entry_ctls_allowed1 =
                (vmm->has_true_ctls ? vmm->ia32_vmx_true_entry_ctls
                                    : vmm->ia32_vmx_entry_ctls) >> 32;
}

static inline void
handle_early_exit(void)
{
        vmlatency_printk("VM instruciton error: %#lx\n",
                         __vmread(VMCS_VM_INSTRUCTION_ERROR));
}

void
measure_vmlatency()
{
        vm_monitor_t vmm = {0};
        cache_vmx_capabilities(&vmm);

        int cnt = allocate_memory(&vmm);
        if (cnt <= 0)
                goto out1;

        vmxon_setup_revision_id(&vmm);
        vmcs_setup_revision_id(&vmm);

        /* Disable interrupts */
        local_irq_disable();

        if (do_vmxon(&vmm) != 0)
                goto out2;

        if (do_vmptrld(&vmm) != 0)
                goto out3;

        initialize_vmcs(&vmm);

        __vmwrite(VMCS_HOST_RIP, (u64)&&handle_vmexit);
        __vmwrite(VMCS_GUEST_RIP, (u64)&&guest_code);

        if (__vmlaunch() != 0) {
                vmlatency_printk("VMLAUNCH failed\n");
                handle_early_exit();
                goto out4;
        }

guest_code:
        __asm__ __volatile__("cpuid"); // Will cause VM exit

handle_vmexit:
        vmlatency_printk("VM exit handled\n");

out4:
        do_vmclear(&vmm);
out3:
        do_vmxoff(&vmm);
out2:
        /* Enable interrupts */
        local_irq_enable();
out1:
        free_memory(&vmm, cnt);
}
