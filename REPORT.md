# page tables

## print a page table

要求在 init 程序装载完成后(init 进程执行 `exec('/init')`)，按一定格式打印用户地址空间的所有有效页表项(PTE)。

```c
// in kernel/vm.c

void
vmprint_(pagetable_t pagetable, int level)
{
  for(int i = 0 ; i < 512; i++) {
    pte_t pte = pagetable[i];

    if (!(pte & PTE_V)) continue;

    for (int j = 0; j < level; j++) printf(".. ");

    printf("..%d: pte %p pa %p\n", i, pte, PTE2PA(pte));

    if (level == 2) continue;

    uint64 child = PTE2PA(pte);
    vmprint_((pagetable_t)child, level + 1);
  }
}

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);

  vmprint_(pagetable, 0);
}

```

由于 xv6 中采用三级页表，所以需要递归的遍历每级页目录。实现的思路与 `void freewalk(pagetable_t pagetable)` 类似，我们可以遍历 512 个页表项，按所在级数判断是否递归。

## A kernel page table per process

要求为每个进程设立单独的kernel pagetable，便于实现下一个子实验中系统调用里用户地址空间和内核地址空间内容的复制。

主要的实现思路是，在进程控制块中保存内核页表`kpagetable`，同时在创建进程、fork等中对它进行维护。在 scheduler 进行进程切换时，需要相应的设置对应的内核页表地址，没有进程运行时使用最初的全局内核页表。

核心问题在于如何维护这一个页表。

### create

内核页表创建时，需要分配并映射内核运行需要的栈空间，因为不同进程下的内核的栈的状态可能不同。为了简便和效率，我们让每个进程记录他的物理内核栈地址 `pkstack`，且不再改变。

```c
// in kernel/proc.h

struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 pkstack;              // Physic address of kernel stack <--
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  pagetable_t kpagetable;      // Kernel page table <--
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};

```

另外，为了统一全局内核页表和进程内核页表的创建过程，设置`pagetable_t new_kpagetable()`函数。

```c
// in kernel/vm.c

pagetable_t new_kpagetable()
{
  pagetable_t kern_pagetable = (pagetable_t) kalloc();
  if (kern_pagetable == 0)
    return 0;

  memset(kern_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap_(kern_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap_(kern_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap_(kern_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap_(kern_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap_(kern_pagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap_(kern_pagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap_(kern_pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  return kern_pagetable;
}
```

所以在分配进程时，我们需要同时分配内核页表，并且设置栈地址的映射。

```c
// part of allocproc() function in kernel/proc.c

// An empty user page table,
// and an kernel page table.
p->pagetable = proc_pagetable(p);

// printf("[allocproc]: before new_kpagetable\n");

p->kpagetable = new_kpagetable();

if(p->pagetable == 0 || p->kpagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
}

kvmmap_(p->kpagetable, p->kstack, p->pkstack, PGSIZE, PTE_R | PTE_W);
```

### free

清除内核页表时需要注意，每个内核页表目前实质上是全局内核页表的一个备份，所映射的物理地址也是相同的(除了栈地址)，而栈地址又没有必要在进程创建和清除时重复的分配，所以并不需要将它映射的物理地址清空，仅需将页表删除即可，但唯一一个相关的函数是 `void freewalk(pagetable_t pagetable)` 可以删除一个页表，但他的前置条件是对应的物理页必须清空，也就是页表项的 `PTE_V` 被置0。因此，我们需要改写一下，令它可以仅删除页表。

```c
// in kernel/vm.c

void
freewalk(pagetable_t pagetable, int need_panic)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child, need_panic);
      pagetable[i] = 0;
    } else if(need_panic && (pte & PTE_V)){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
```

其中 `need_panic` 可以选择是否必须要清空物理页。所以在清除内核页表的时候，仅需设置该参数为 0 即可。

```c
// in kernel/vm.c

void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable, 1);
}

// Free kernel page-table pages.
void
kvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 0);
  freewalk(pagetable, 0);
}
```

### switch

切换不同的内核页表只需设置 `satp` 寄存器。而设置完`satp`寄存器后，需要刷新 TLB。

```c
// part of function scheduler() in kernel/proc.c

if(p->state == RUNNABLE) {
  // Switch to chosen process.  It is the process's job
  // to release its lock and then reacquire it
  // before jumping back to us.
  p->state = RUNNING;
  c->proc = p;

  w_satp(MAKE_SATP(p->kpagetable));
  sfence_vma();

  swtch(&c->context, &p->context);

  kvminithart();
  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;

  found = 1;
}

```

## Simplify copyin/copyinstr

为了使内核能够直接访问到用户地址空间中的内容，我们需要使内核页表能够映射用户地址空间，同时要实现两个地址空间内容的同步。没有使用同一个地址空间的原因是，在用户地址的页表项会设置 `PTE_U`，而 risc-v 中禁止内核态直接访问这一页表项，因此我们需要做用户地址空间在内核页表中的一个备份或同步。

实现同步的方法是页表项的复制(同时修改 `PTE_U` 位)。其中，有关于是否释放或重新分配物理页表的代码都做了修改，能够选择是否释放或分配，因为内核页表的内容需要与用户页表一致，对应的物理页表应该是同一个，不能随意删除或新建(处理用户页表时一定已经处理过了)。

另外，`sync_pagetable` 函数也能够选择是以 append 方式还是覆盖的方式进行复制，可以提高效率，避免不必要的复制操作。

```c
// in kernel/vm.c

uint64
vmdealloc_(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int do_free)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, do_free);
  }

  return newsz;
}

int
vmcopy_(pagetable_t old, pagetable_t new, uint64 oldsz, uint64 newsz, int do_alloc)
{
  pte_t *pte, *newpte;
  uint64 pa, i;
  uint flags;
  char *mem;

  oldsz = oldsz ? PGROUNDUP(oldsz) : 0;
  for(i = oldsz; i < newsz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("vmcopy_: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("vmcopy_: page not present");
    pa = PTE2PA(*pte);
    
    if(!do_alloc) {
      flags = PTE_FLAGS(*pte & ~PTE_U); // remove PTE_U flag
      if((newpte = walk(new, i, 1)) == 0)
        panic("vmcopy_: new pte should exist");
      *newpte = PA2PTE(pa) | flags;
    } else {
      flags = PTE_FLAGS(*pte);
      if((mem = kalloc()) == 0)
        goto err;
      memmove(mem, (char*)pa, PGSIZE);
      if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
        kfree(mem);
        goto err;
      }
    }
  }

  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

int
sync_pagetable(pagetable_t upagetable, pagetable_t kpagetable, uint64 oldsz, uint64 newsz, int append)
{
  if (append) {
    if (oldsz > newsz) {
      return vmdealloc_(kpagetable, oldsz, newsz, 0);
    } else {
      return vmcopy_(upagetable, kpagetable, oldsz, newsz, 0);
    }
  } else {
    if(oldsz > 0)
      uvmunmap(kpagetable, 0, PGROUNDUP(oldsz)/PGSIZE, 0);
    return vmcopy_(upagetable, kpagetable, 0, newsz, 0);
  }
}
```

### where to sync pagetable?

主要用在 fork、exec、userinit 中，在分配完内核页表时，及时的同步页表信息。

### how to free the kernel pagetable?

释放内核页表时会遇到我们最初提到的问题，内核程序的部分不应当被释放，因此，我们只需删除用户页表项对应的部分即可，也无需释放对应的物理页表。释放时，我们只需指定用户进程的大小即可，使用我们修改过的 `freewalk` 函数进行页表的清除。

```c
// in kernel/vm.c

void
kvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 0);
  freewalk(pagetable, 0);
}

```

### attention

在一些小的函数中需要注意使用哪一个内核页表。如 `kvmpa` 函数，调用 `walk` 时，需要传入当前进程的内核页表。

```c
// in kernel/vm.c

uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(myproc()->kpagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}
```

更多相关代码，详见 附录 3.1
