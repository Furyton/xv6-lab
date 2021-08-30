// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  int cpu_id;
  for (cpu_id = 0; cpu_id < NCPU; cpu_id ++) {
    initlock(&kmem[cpu_id].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  acquire(&kmem[0].lock);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    memset(p, 1, PGSIZE);

    struct run *r = (struct run*)p;

    r->next = kmem[0].freelist;
    kmem[0].freelist = r;
  }
  release(&kmem[0].lock);
    // kfree(p);
}

int get_cpu_id() {
  push_off();
  int id = cpuid();
  pop_off();
  return id;
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int cid = get_cpu_id();

  acquire(&kmem[cid].lock);
  r->next = kmem[cid].freelist;
  kmem[cid].freelist = r;
  release(&kmem[cid].lock);
}

struct run*
steal(int cur_cid) {
  int cid;
  struct run* r;
  for (cid = 0; cid < NCPU; cid ++) {
    if (cid == cur_cid) continue;

    acquire(&kmem[cid].lock);
    r = kmem[cid].freelist;
    if (r) {
      kmem[cid].freelist = r->next;
      r->next = 0;

      release(&kmem[cid].lock);
      return r;
    }

    release(&kmem[cid].lock);
  }
  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cid = get_cpu_id();

  acquire(&kmem[cid].lock);
  r = kmem[cid].freelist;
  if(r)
    kmem[cid].freelist = r->next;
  else {
    r = steal(cid);
  }
  release(&kmem[cid].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
