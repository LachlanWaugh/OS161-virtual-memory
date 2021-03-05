#ifndef U_TLB_H
#define U_TLB_H

/* */
void tlb_flush(void);

/* */
int tlb_insert(uint32_t page_table_entry, uint32_t address);

#endif /* U_TLB_H */
