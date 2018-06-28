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

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <asm/io.h>

#include "api.h"

int
allocate_vmpage(vmpage_t *p)
{
        p->page = alloc_page(GFP_KERNEL | __GFP_ZERO);
        if (!p->page)
                return -1;

        p->p = kmap(p->page);
        p->pa = page_to_phys(p->page);
        return 0;
}

void
free_vmpage(vmpage_t *p)
{
        kunmap(p->page);
        __free_page(p->page);
        p->page = NULL;
        p->p = NULL;
        p->pa = 0;
}

int
vmlatency_printm(const char *fmt, ...)
{
        int ret;
        va_list va;
        va_start(va, fmt);
        ret = vprintk(fmt, va);
        va_end(va);
        return ret;
}

unsigned long
vmlatency_get_cpu(void)
{
        unsigned long irq_flags;
        get_cpu();
        local_irq_save(irq_flags);
        return irq_flags;
}

void
vmlatency_put_cpu(unsigned long irq_flags)
{
        local_irq_restore(irq_flags);
        put_cpu();
}
