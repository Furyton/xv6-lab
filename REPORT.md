# cow

要求实现 Copy On Write 功能，在对用户地址空间进行复制操作时，均采用 COW 机制。

## copy

为了实现 COW，我们首先需要在页表项中设置一个 flag `PTE_C` 用来标识该页是否是 COW 页。

```c
// in kernel/riscv.h

#define PTE_C (1L << 8)  // 1 -> cow page
```

继而，我们可以将所有关于用户页表复制的函数，修改为只设置 `PTE_C` 和清除 `PTE_W`。

```c
// in kernel/vm.c

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 i;

  for(i = 0; i < sz; i += PGSIZE) {
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");

    *pte |= PTE_C; // add PTE_C flag
    *pte &= ~PTE_W; // remove PTE_W flag

    pte_t *newpte = walk(new, i, 1);
    *newpte = *pte;

    safe_increase_rc((void*)PTE2PA(*pte)); // explained later
  }
  return 0;
}
```

## free

为了由于一个物理页可能有很多 COW 页映射到它，所以我们不能随意的释放物理页表。我们可以设立一个引用数组`count[]`，记录每一个物理页对应的映射的个数。注意，我们需要在 `kalloc` 中维护这个数组，并且需要一个锁机制避免并发时出现冲突。

```c
// in kernel/kalloc.c

struct {
  struct spinlock lock;
  int count[PHYSTOP >> 12];
} refcnt;

```

在有锁和无锁的环境中，可以用不同的函数在改变 `count`。

```c
// in kernel/kalloc.c

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
```

当进行释放时，可以首先减少引用数，之后检查是否为 0，继而决定是否需要释放。分配时则需要先重置引用数，之后增 1.

```c
// in kernel/kalloc.c

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

```

增加的情况则只会出现在 `uvmcopy` 中，当进行 COW 时，我们需要增加对这一物理页的引用数。

## handle page fault

真正执行复制的过程发生在 page fault 时。当发生页的写错误时，我们要检查它是否时 COW 页，若是则进行页的复制，清除掉 `PTE_C` 标志，加上 `PTE_W` 标志，同时释放原先的物理页(释放中先执行引用数修改操作)。

```c
// in kernel/trap.c

int
page_fault_handler(pagetable_t pagetable, uint64 va)
{
  uint64 pa;
  pte_t *pte;
  uint flags;

  if (va >= MAXVA)
    return -1;

  va = PGROUNDDOWN(va);
  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return -1;

  pa = PTE2PA(*pte);

  flags = PTE_FLAGS(*pte);
  // cow page
  if (flags & PTE_C)
  {
    char *mem = kalloc();
    if (mem == 0) return -1;

    memmove(mem, (char*)pa, PGSIZE);
    kfree((void*)pa);
    flags = (flags & ~PTE_C) | PTE_W;
    *pte = PA2PTE((uint64)mem) | flags;
    return 0;
  }
  else return 0;
}

```

利用页错误处理函数，我们可以轻松的处理15号page fault，`page_fault_handler` 返回 -1 表示出现越界、未映射等其他错误。

```c
// part of function usertrap() in kernel/trap.c

// page fault
if (r_scause() == 15)
{
    uint64 va = r_stval();

    if (page_fault_handler(p->pagetable, va) != 0)
        p->killed = 1;
}
else
{
    p->killed = 1;
}
```