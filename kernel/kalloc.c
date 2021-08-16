// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int ref[PHYSTOP >> 12];
} refcnt;

void
rcinit()
{
  initlock(&refcnt.lock, "refcnt");
  acquire(&refcnt.lock);
  memset(refcnt.ref, 0, sizeof(refcnt.ref));
  release(&refcnt.lock);
}

void
increase_rc(void *pa)
{
  acquire(&refcnt.lock);
  refcnt.ref[((uint64)pa) >> 12]++;
  release(&refcnt.lock);
}

void
decrease_rc(void *pa)
{
  acquire(&refcnt.lock);
  refcnt.ref[((uint64)pa) >> 12]--;
  release(&refcnt.lock);
}
int
get_rc(void *pa)
{
  acquire(&refcnt.lock);
  int rc = refcnt.ref[((uint64)pa) >> 12];
  release(&refcnt.lock);
  return rc;
}

void
reset_rc(void *pa)
{
  acquire(&refcnt.lock);
  refcnt.ref[((uint64)pa) >> 12] = 0;
  release(&refcnt.lock);
}

void
increase_rc_by_pte(pte_t* pte)
{
  increase_rc((void*)PTE2PA(*pte));
}

void
kinit()
{
  rcinit();
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  decrease_rc(pa);
  if (get_rc(pa) > 0)
    return;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  reset_rc(pa);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    reset_rc((void*)r);
    increase_rc((void*)r);
  }
  return (void*)r;
}
