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

#ifndef _VM_H_
#define _VM_H_

/*
* VM system-related definitions.
*/

#include <machine/vm.h>
#include <addrspace.h>

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

#define N_FRAMES (uint32_t)(ram_getsize() / PAGE_SIZE)
#define N_ENTRIES 1024

#define PAGE_FREE 0

/* Used for page table level */
#define FIRST_LEVEL   1 /* */
#define SECOND_LEVEL  2 /* */

/* Used for returning region that an address lies in */
#define REG_UNUSED  0 /* */
#define REG_CODE    1 /* */
#define REG_DATA    2 /* */
#define REG_HEAP    3 /* */
#define REG_STACK   4 /* */
#define REG_KERNEL  5 /* */

/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

/*
 * Functions for page table:
 *    pt_init     - Given an address space, initialise first layer page table
 * 
 *    pt_create   - Create a page table: this function is only used for the creation of 2nd level tables, 
 *                  as first layer table is initialised with pt_init(struct addrspace *as)
 * 
 *    pt_destroy  - Free second layer page table
 * 
 *    pt_insert   - Create a frame and passed the address into the page_table 
 *                  incremented by 1st layer (pt1) and 2nd layer (pt2)
 * 
 *    pt_copy     - Create a copy of old page table (old_table) to a new page table (ret)
 */
int     pt_init(struct addrspace *as);
int     pt_create(paddr_t **page_table, uint32_t pt1);
void    pt_destroy(paddr_t *page_table);
int     pt_insert(paddr_t **page_table, uint32_t pt1, uint32_t pt2);
int     pt_copy(paddr_t **old_table, paddr_t ***new_table);


int     add_entries(struct addrspace *as, vaddr_t address);

#endif /* _VM_H_ */
