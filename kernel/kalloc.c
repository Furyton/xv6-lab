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

#define PA2IDX(pa) (((uint64)pa) >> 12)

struct {
  struct spinlock lock;
  int count[PHYSTOP >> 12];
} refcnt;

void
rcinit()
{
  initlock(&refcnt.lock, "refcnt");
  acquire(&kmem.lock);
  memset(refcnt.count, 0, sizeof(refcnt.count));
  release(&kmem.lock);
}

void
safe_increase_rc(void* pa)
{
  acquire(&refcnt.lock);
  refcnt.count[PA2IDX(pa)]++;
  release(&refcnt.lock);
}

void
increase_rc(void *pa)
{
  refcnt.count[PA2IDX(pa)]++;
}

void
decrease_rc(void *pa)
{
  refcnt.count[PA2IDX(pa)]--;
}
int
get_rc(void *pa)
{
  int rc = refcnt.count[PA2IDX(pa)];
  return rc;
}

void
reset_rc(void* pa)
{
  refcnt.count[PA2IDX(pa)] = 0;
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

  acquire(&refcnt.lock);
  decrease_rc(pa);
  if (get_rc(pa) > 0) {
    release(&refcnt.lock);
    return;
  }
  reset_rc(pa);
  release(&refcnt.lock);
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

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
    acquire(&refcnt.lock);
    reset_rc((void*)r);
    increase_rc((void*)r);
    release(&refcnt.lock);
  }
  return (void*)r;
}
