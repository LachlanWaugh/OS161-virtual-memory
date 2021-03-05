/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <elf.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
*/

/*
*
*/
struct addrspace *as_create(void) {
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    bzero((void*) as, sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }

    /*
    * It says that the page table should be allocated by vm_fault, I wasn't sure
    * if this included the first-level table, but in case it is I moved it there
    */
    as->regions    = NULL;
    as->page_table = NULL;

    return as;
}

/*
*
*/
void as_destroy(struct addrspace *as) {
    /* Free all the regions from the address space */
    for (region_t *next, *curr = as->regions; curr != NULL; curr = next) {
        next = curr->next;
        kfree(curr);
    }

    /* Free the page table (if it was allocated) */
    if (as->page_table != NULL) {
        /* Free all the pages in the page table */
        for (int i = 0; i < N_ENTRIES; i++) {
            pt_destroy(as->page_table[i]);
        }
        kfree(as->page_table);
    }

    kfree(as);
}

/*
*
*/
int as_copy(struct addrspace *old, struct addrspace **ret) {
    struct addrspace *new_as = as_create();
    if (new_as == NULL) {
        return ENOMEM;
      }

    /* Copy all regions to the new address space */
    for (region_t *region = old->regions; region; region = region->next) {
        int perm = region->perm;
        int result = as_define_region(new_as, region->start, region->size,
                                      perm & PF_R, perm & PF_W, perm & PF_X);
        if (result) {
            as_destroy(new_as);
            return result;
        }
    }

    /* Copy the page table to the new address space */
    pt_init(new_as);
    int result = pt_copy(old->page_table, &new_as->page_table);
    if (result) {
        as_destroy(new_as);
        return result;
    }

    *ret = new_as;

    return 0;
}

/*
*
*/
void as_activate(void) {
    struct addrspace *as = proc_getas();
    if (as == NULL) {
        /* Kernel thread without an address space;
         * leave the prior address space in place. */
        return;
    }

    tlb_flush();
}

/*
*
*/
void as_deactivate(void) {
    tlb_flush();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
*/
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                         int readable, int writeable, int executable) {
    /* Bad memory reference */
    if (as == NULL) {
        return EFAULT;
    }

    if (vaddr + memsize >= USERSTACK) return ENOMEM;

    /* I copied the region alignment from badvm.c */

    /* Align the region. First, the base... */
    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    /* ...and now the length. */
    memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

    int perm = readable | writeable | executable;

    /* Store the region into the address space */
    int result = region_insert(as, vaddr, memsize, perm);
    if (result) {
        return result;
    }

    return 0;
}

/*
* as_prepare_load: store the old region permissions and set the current
* permissions to read/write
*/
int as_prepare_load(struct addrspace *as) {
    if (as == NULL) {
        return EFAULT;
    }

    for (region_t *region = as->regions; region != NULL; region = region->next)
    {
        region->old_perm = region->perm;
        region->perm     = PF_R | PF_W ;
    }

    return 0;
}

/*
* as_complete_load: set the region permissions back to the previous ones
*/
int as_complete_load(struct addrspace *as) {
    if (as == NULL) {
        return EFAULT;
    }

    for (region_t *region = as->regions; region != NULL; region = region->next)
    {
        region->perm = region->old_perm;
    }

    return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {
    /* Allocate the stack region as Readable/Writable */
    // int result = as_define_region(as, USERSTACK, 16 * PAGE_SIZE, 0x4, 0x2, 0x0);
    // if (result) {
        // return result;
    // }
    (void)as;
    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    return 0;
}

/*
* Inserts a region into the correct place in a given address space
*/
int region_insert(struct addrspace *as, vaddr_t start, size_t size, uint32_t permissions) {
    region_t *new_region, *curr_region, *temp_region;
    new_region = kmalloc(sizeof(region_t));
    bzero((void*) new_region, sizeof(region_t));
    if (new_region == NULL) {
        return ENOMEM;
    }

    /* Initialize the new region */
    new_region->perm = permissions;
    new_region->old_perm = permissions;
    new_region->start = start;
    new_region->size = size;
    new_region->next = NULL;

    /* Add this new region to the address spaces' list */
    curr_region = temp_region = as->regions;
    if (curr_region == NULL) {
        as->regions = new_region;
    } else {
        /* Insert the new region into the address spaces' list, making sure it
         * is inserted before any higher addresses */
        while (curr_region != NULL && curr_region->start < new_region->start) {
            temp_region = curr_region;
            curr_region = curr_region->next;
        }

        temp_region->next = new_region;
        new_region->next = curr_region;
    }

    return 0;
}

/**
 * Find the region that a address resides in
 * Return NULL if region not found
 */
region_t *region_find(struct addrspace *as, vaddr_t address) {
    region_t *region;
    vaddr_t region_start, region_end;

    /* Check all of the regions in the address space for this address */
    for (region = as->regions; region != NULL; region = region->next) {
        /* the stack grows down, so first check if the region is the stack */
        if (region->start == USERSTACK) {
            region_start = region->start - region->size - 1; /* off-by-one error */
            region_end = region->start;
        /* The other regions grow up, so they're handled the opposite way */
        } else {
            region_start = region->start;
            region_end = region->start + region->size - 1;
        }

        /* If the address is located in this region, return it's name */
        if (address >= region_start && address <= region_end) {
            return region;
        }
    }
    /* Region not found */
    return NULL;
}

/*
* Find the memory region that a given address resides in, returns REG_UNUSED
* if the address is not in a region.
*/
int region_type(struct addrspace *as, vaddr_t address) {
    /* If the address is above the stack, it's obviously the kernel */
    if (address >= USERSTACK) {
        return REG_KERNEL;
    }

    region_t *region = region_find(as, address);
    if (region == NULL) {
        return REG_UNUSED;
    }
    else if (region->start == USERSTACK) {
        return REG_STACK;
    }
    /* TODO: implement checks for REG_HEAP and REG_DATA */
    else {
        return REG_CODE;
    }
}

/*
*
*/
int region_permissions(struct addrspace *as, vaddr_t address) {
    region_t *region = region_find(as, address);
    if (region == NULL) {
        return -1;
    }

    return region->perm;
}

/*
* Implementation copied from dumbvm.c
*/
void tlb_flush(void) {

    int spl = splhigh();
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}

/*
 *
*/
int tlb_insert(paddr_t page_table_entry, vaddr_t address) {
    /* entryhi is the page number */
    uint32_t entryhi = address & PAGE_FRAME;

    /* entrylo is the frame number, valid bit and dirty bit */
    uint32_t entrylo = page_table_entry;

    /* */
    int spl = splhigh();
    tlb_random(entryhi, entrylo);
    splx(spl);

    return 0;
}
