#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "x86.h"
#include "proc.h"
#include "buf.h"
#define PTE_S           0x200   // Present in swap space



struct swap_slot slotsList[NSLOTS];
void
initswaplist(){
  for(int i = 0; i<NSLOTS; i++){
    slotsList[i].is_free = 1;
  }
}

void
printSlots(){
  for(int i = 0; i<NSLOTS; i++){
    cprintf("Slot %d: is_free: %d, page_perm: %d, pid: %d\n", i, slotsList[i].is_free, slotsList[i].page_perm, slotsList[i].pid);
  }
}

struct vicpage
findvictimpage(){
    // if there is a page that is not accessed, return that page
    // if all pages are accessed, convert 10% of the pages to not accessed

    struct proc *p = findvictimproc();
    int count = 0;
    struct vicpage vp;
    for(int i = 0; i < p->sz; i+=PGSIZE){
        pte_t* pte = walkpgdir(p->pgdir, (void*)i, 0);
        if(pte == 0){
            panic("findvictimpage: pte is null");
        }
        if(*pte & PTE_P){
            if(!(*pte & PTE_A)){
                if (p->rss > 0){
                    p->rss -= PGSIZE;
                }
                vp.page = (char*)P2V(PTE_ADDR(*pte));
                vp.pte = pte;
                vp.pid = p->pid;
                return vp;
            }else{
                count++;
            }
        }
    }
    int num_of_pages_to_convert = (count+9)/10;
    for(int i = 0; i < p->sz; i+=PGSIZE){
        pte_t* pte = walkpgdir(p->pgdir, (void*)i, 0);
        if(pte == 0){
            panic("findvictimpage: pte is null");
        }
        if(*pte & PTE_P){
            if(*pte & PTE_A){
                *pte = *pte ^ PTE_A;
                num_of_pages_to_convert--;
                if(num_of_pages_to_convert == 0){
                    if(p->rss > 0){
                        p->rss -= PGSIZE;
                    }
                    vp.page = (char*)P2V(PTE_ADDR(*pte));
                    vp.pte = pte;
                    vp.pid = p->pid;
                    return vp;
                }
            }
        }
    }
    return vp;
}

uint
getFreeSwapSlot(){
    for(uint i = 0; i<NSLOTS; i++){
        if(slotsList[i].is_free){
            return 2 + i*SLOT_SIZE;
        }
    }
    return 0;
}

char*
swapout(){
    struct vicpage vp = findvictimpage();
    uint slot = getFreeSwapSlot();
    if(slot == 0){
        panic("No free swap slot available");
    }
    for (int i = 0; i < SLOT_SIZE; i++){
        struct buf* b = bread(ROOTDEV, slot+i);
        // cprintf("vp.page: %x\n", vp.page+i*BSIZE);
        memmove(b->data, vp.page+i*BSIZE,BSIZE);
        bwrite(b);
        brelse(b);
    }    
    *vp.pte = PTE_FLAGS(*vp.pte) | (slot<<12);
    *vp.pte = *vp.pte ^ PTE_P;
    *vp.pte = *vp.pte ^ PTE_S;
    slotsList[(slot-2)/8].is_free = 0;
    slotsList[(slot-2)/8].page_perm = PTE_FLAGS(*vp.pte);
    slotsList[(slot-2)/8].pid = vp.pid;
    return vp.page;
}

char *
swapin(uint slot){
    char* page = kalloc();
    if(page == 0){
        panic("swapin: kalloc failed");
    }
    
    for(int i = 0; i < SLOT_SIZE; i++){
        struct buf* b = bread(ROOTDEV, slot+i);
        memmove(page+i*BSIZE, b->data, BSIZE);
        brelse(b);
    }
    return page;
}

void
freeSwapSlot(uint slot){
    slotsList[(slot-2)/8].is_free = 1;
}

void 
cleanSwap(struct proc* p){
    for(int i = 0; i<p->sz;i+=PGSIZE){
        pte_t* pte = walkpgdir(p->pgdir, (void*)i, 0);
        if(pte == 0){
            panic("cleanSwap: pte is null");
        }
        if(*pte & PTE_S){
            uint slot = *pte >> 12;
            freeSwapSlot(slot);
        }
    }
}

void 
pagefault_handler(){
    uint va = rcr2();
    struct proc *p = myproc();
    pte_t *pte = walkpgdir(p->pgdir, (void*)va, 0);

    if(pte == 0){
        panic("pagefault_handler: pte is nulfdc");
    }
    if(*pte & PTE_P){
        panic("pagefault_handler: page is already present");
    }
    if((*pte & PTE_S)){
        uint slot = *pte >> 12;
        char* page = swapin(slot);
        uint permissions = slotsList[(slot-2)/8].page_perm;
        slotsList[(slot-2)/8].is_free = 1;
        *pte = PTE_ADDR(V2P(page)) | PTE_FLAGS(permissions);
        *pte = *pte ^ PTE_P;
        *pte = *pte ^ PTE_S;
        *pte = *pte | PTE_A;
        p->rss += PGSIZE;
    }
    else{
        panic("pagefault_handler: page is not present in swap");
    }
}