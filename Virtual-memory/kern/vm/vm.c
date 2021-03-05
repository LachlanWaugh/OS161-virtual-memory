
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <current.h>
#include <proc.h>
#include <spl.h>
#include <mips/tlb.h>
#include <elf.h>


/**
 * Given a address space, initialise first layer page table
 */
int pt_init(struct addrspace *as) {
    as->page_table = kmalloc(PAGE_SIZE);
    if (as->page_table == NULL) return ENOMEM;
    bzero((void*) as->page_table, PAGE_SIZE);
    for (int i=0;i<N_ENTRIES;i++) as->page_table[i] = NULL;
    return 0;
}


/*
* Create a page table: this function is only used for the creation of 2nd level tables,
* as first layer table is initialised with pt_init(struct addrspace *as)
*/
int pt_create(paddr_t **page_table, uint32_t pt1) {
    page_table[pt1] = kmalloc(PAGE_SIZE);
    if (page_table[pt1] == NULL) return ENOMEM;
    bzero((void*) page_table[pt1], PAGE_SIZE);
    return 0;
}

/*
 * Free second layer page table
 */
void pt_destroy(paddr_t *page_table) {
    if (page_table == NULL) {
        return;
    }

    /* Free the allocated pages */
    for (int i = 0; i < N_ENTRIES; i++) {
        if (page_table[i] != 0) {
            free_kpages(PADDR_TO_KVADDR(page_table[i] & PAGE_FRAME));
        }
    }

    kfree(page_table);
}

/*
* Create a frame and passed the address into the page_table
* incremented by 1st layer (pt1) and 2nd layer (pt2)
*/
int pt_insert(paddr_t **page_table, uint32_t pt1, uint32_t pt2) {
    /*  */
    vaddr_t page = alloc_kpages(1);
    if (page == 0) {
        return ENOMEM;
    }
    /* Initialise frame to 0 */
    bzero((void *) page, PAGE_SIZE);

    paddr_t frame = KVADDR_TO_PADDR(page);

    page_table[pt1][pt2] = frame & PAGE_FRAME;

    return 0;
}

/*
 * Create a copy of old page table (old_table) to a new page table (ret)
 */
int pt_copy(paddr_t **old_table, paddr_t ***ret) {

    paddr_t **new_table;
    int error;

    if (old_table == NULL) {
        return 0;
    }

    /* Create the new first-level table */
    new_table = kmalloc(PAGE_SIZE);
    if (new_table == NULL) {
        return ENOMEM;
    }
    bzero((void*) new_table, PAGE_SIZE);

    /* Loop through each second-level table and copy it across */
    for (int i = 0; i < N_ENTRIES; i++) {
        if (old_table[i] == NULL) continue;

        /* Create the second level table */
        error = pt_create(new_table, i);
        if (error) {
            kfree(new_table);
            return error;
        }

        /* Copy each entry from the original second level table */
        for (int j = 0; j < N_ENTRIES; j++) {
            if (old_table[i][j] == 0) continue;
            vaddr_t old_frame = PADDR_TO_KVADDR(old_table[i][j]);
            vaddr_t new_frame = alloc_kpages(1);
            if (new_frame == 0) {
                kfree(new_table);
                return ENOMEM;
            }
            /* Zero out new frame */
            bzero((void *)new_frame, PAGE_SIZE);
            memmove((void *)new_frame, (void *)old_frame, PAGE_SIZE);

            new_table[i][j] = KVADDR_TO_PADDR(new_frame) & PAGE_FRAME;
        }
    }

    *ret = new_table;

    return 0;
}

/*
*
*/
int add_entries(struct addrspace *as, vaddr_t address) {
    paddr_t **page_table = as->page_table;

    /* Bits (22..31) provide the index into the first page table, bits (12..21)
    provide the index into the second */
    int p1 = (address >> 22), p2 = ((address << 10) >> 22);

    /* If a mapping already exists, nothing to do */
    if (page_table[p1][p2] != PAGE_FREE) {
        /* Maybe check that it's valid? */
        return 0;
    }

    /* Check the permissions for the region */
    int perm = region_permissions(as, address);
    if (perm == -1) {
        return EFAULT;
    }
    /* Check if the page is writable for dirty bit */
    // int dirty = (perm & PF_W) ? TLBLO_DIRTY : 0;

    // int spl = splhigh();
    // pt_insert(&page_table[p1][p2], NULL, dirty);
    // splx(spl);

    // spl = splhigh();
    // tlb_insert(page_table[p1][p2], address);
    // splx(spl);

    return 0;
}

/*
*
*/
void vm_bootstrap(void) {
    /* Initialise any global components of your VM sub-system here.
     *
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
    */
}

int vm_fault(int faulttype, vaddr_t faultaddress) {
    run_number++;
    // kprintf("---------run number: %d---------\n", run_number);
    int error;

    /* Check the memory references */
    if (curproc == NULL || faultaddress == 0) {
        return EFAULT;
    }

    /* Check the address space is valid */
    struct addrspace *as = proc_getas();
    if (as == NULL || as->regions == NULL) {
        return EFAULT;
    }

    /* Check what region the fault occurred in, (making sure it's valid) */
    // int segment = region_type(as, faultaddress);
    // if (segment == REG_KERNEL || segment == REG_UNUSED) {
        // return EFAULT;
    // }

    /* If faultaddress is not in userstack and doesn't belong to one of the regions
     * Return bad memory reference */
    if (region_find(as, faultaddress) == NULL){
        uint32_t stack_end = USERSTACK - 16 * PAGE_SIZE;
        if (!(USERSTACK > faultaddress && faultaddress > stack_end)) return EFAULT;
    }

    /*  */
    switch(faulttype) {
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        case VM_FAULT_READONLY:
            return EFAULT;
        default:
            return EINVAL;
    }
    faultaddress = faultaddress & PAGE_FRAME;
    paddr_t paddr = KVADDR_TO_PADDR(faultaddress);

    uint32_t pt1 = (paddr >> 22) & 0x3FF;
    uint32_t pt2 = ((paddr << 10) >> 22) & 0x3FF;

    /* If page table not initialised*/
    if (as->page_table == NULL){
        error = pt_init(as);
        if (error) return error;
    }

    /* If 1st level entry not initialised*/
    if (as->page_table[pt1] == NULL){
        error = pt_create(as->page_table, pt1);
        if (error) return error;
    }
    /* If 2nd level entry not initialised*/
    if (as->page_table[pt1][pt2] == 0){
        error = pt_insert(as->page_table, pt1, pt2);
        if (error) return error;
    }

    /* Loads to TLB */
    int spl = splhigh();

    uint32_t ehi, elo;

    ehi = faultaddress;
    elo = as->page_table[pt1][pt2] | TLBLO_DIRTY | TLBLO_VALID;

    tlb_random(ehi, elo);
    splx(spl);

    return 0;
}

/*
* SMP-specific functions.  Unused in our UNSW configuration.
*/
void vm_tlbshootdown(const struct tlbshootdown *ts) {
      (void)ts;
      panic("vm tried to do tlb shootdown?!\n");
}
