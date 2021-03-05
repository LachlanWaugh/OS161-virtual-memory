#include <mips/tlb.h>
#include <tlb.h>
#include <spl.h>

void tlb_flush(void) {
    /*
    * Implementation copied from dumbvm.c
    */

    int spl = splhigh();

    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    splx(spl);
}

/*
 *
*/
int tlb_insert(uint32_t page_table_entry, uint32_t address) {
    /* entryhi is the page number */
    uint32_t entryhi = address & PAGE_FRAME;
    int x = 1 / 0;
    /* entrylo is the frame number, valid bit and dirty bit */
    uint32_t entrylo = page_table_entry;

    /* */
    int spl = splhigh();
    tlb_random(entryhi, entrylo);
    splx(spl);

    return 0;
}
