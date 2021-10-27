# mmap

要求实现 mmap 和 unmmap，能够将硬盘中的给定文件映射到内存中，并支持将修改写回。

## how to store and represent the mapped files?

首先要考虑被映射的文件要放到内存中的哪个位置，以及该如何表示这些映射。

在用户地址空间中，最高处的地址放置的是 `trapframe` 和一些保护页，之后是可以随意使用的一段堆栈地址空间，所以我们考虑将映射的文件以此放到这里。具体的，从高到低以此根据映射文件的大小分配一块内存。文件的数据地址由低到高放置，并且文件开始地址保持页对齐。为了实现它，我们需要记录当前能够分配的最大地址 `max_addr`。

记录映射需要记录内存中的开始和结束地址，文件的长度，文件指针，以及一些权限标志等。

```c
// in kernel/proc.h

struct vma {
  uint64 start;
  uint64 end;
  int length;
  int prot;
  int flags;
  struct file* file;
  int used;
};

```

最终我们需要在进程控制块中设立两个新的属性。

```c
// part of `struct proc` in kernel/proc.h

struct vma* vma[NVMA];       // Virtual memory area per process
uint64 max_addr;             // Maximum valid VMA address
```

除此之外，我们还需要能够为进程分配和回收 `vma`。

```c
// in kernel/file.c


struct {
  struct spinlock lock;
  struct vma vma[NVMA];
} vma_table;


void
vmainit(void)
{
  initlock(&vma_table.lock, "vma_table");
}

struct vma*
vmaalloc(void)
{
  struct vma* vma;
  acquire(&vma_table.lock);
  for(vma = vma_table.vma; vma < vma_table.vma + NVMA; vma++) {
    if (!vma->used) {
      vma->used = 1;
      release(&vma_table.lock);
      return vma;
    }
  }
  release(&vma_table.lock);
  return 0;
}

void
vmadealloc(struct vma* vma)
{
  acquire(&vma_table.lock);
  memset(vma, 0, sizeof(vma));
  release(&vma_table.lock);
}
```

### mmap

建立映射的系统调用实现相对简单，根据 `max_addr` 分配一块合适大小的内存空间，填写 `vma` 的信息。注意这里仅仅记录了映射，但并没有真正地将文件载入内存中，而是在 `trap` 阶段使用 lazy allocation 的机制。

```c
// in kernel/sysfile.c

uint64
sys_mmap(void)
{
  int length, prot, flags, fd;
  struct file *f;

  if (argint(1, &length) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0)
    return -1;

  if(!f->writable && (prot & PROT_WRITE) && (flags & MAP_SHARED)){
    return -1;
  }

  struct proc* p = myproc();
  uint64 starting_addr = PGROUNDDOWN(p->max_addr - length);

  struct vma *vma;

  if ((vma = vmaalloc()) == 0) 
    return -1;

  vma->file = filedup(f);
  vma->start = starting_addr;
  vma->end = p->max_addr;
  vma->prot = prot;
  vma->flags = flags;
  vma->length = length;

  for (int i = 0; i < NVMA; i++) {
    if (!p->vma[i]) {
      p->vma[i] = vma;
      break;
    }
  }

  p->max_addr = starting_addr;

  return starting_addr;
}
```

### lazy allocation

若发生页的读写错误时，我们首先在当前进程的所有映射中找寻匹配的一个映射，然后为其分配一个物理页，之后将一个页的文件内容写到内存中。当然，这里需要考虑文件本身的长度，以及文件 `offset` 的细节。


这里读文件的时候，默认起始的地址 `va` 是对齐的，之后计算出偏移和读取的长度，进行读操作。

```c
// in kernel/file.c

void
load_vma(struct vma* vma, uint64 va)
{
  begin_op();
  struct inode *ip = vma->file->ip;
  ilock(ip);
  int off = va - PGROUNDDOWN(vma->start);
  int n = PGSIZE;
  if (vma->length - off < n)
    n = vma->length - off;
  readi(ip, 1, va, off, n);
  iunlock(ip);
  end_op();
}
```

在 `trap.c` 中则只需查找对应的 `vma`，分配物理页，进行读取即可。

```c
// part of function usertrap() in kernel/trap.c

// r_scause() == 15 || r_scause() == 13

char *mem;
pagetable_t pagetable = p->pagetable;

uint64 va = PGROUNDDOWN(r_stval()), original_va = r_stval();

struct vma * vma = 0;

for(int i = 0; i < NVMA; i++) {
    if (p->vma[i] && (original_va <= p->vma[i]->start + p->vma[i]->length && original_va >= p->vma[i]->start)) {
        vma = p->vma[i];
        break;
    }
}

if (!vma) {
    printf("can't find correspond mapping\n");
    goto bad;
}

mem = kalloc();
if(mem == 0) {
    printf("usertrap: allocate new page failed\n");
    goto bad;
} else {
    memset(mem, 0, PGSIZE);

    int flag = PTE_U;
    if (vma->prot & PROT_WRITE) flag |= PTE_W;
    if (vma->prot & PROT_READ)  flag |= PTE_R;

    if(mappages(pagetable, va, PGSIZE, (uint64)mem, flag) != 0) {
        kfree(mem);
        printf("usertrap: map page failed\n");
        goto bad;
    }

    load_vma(vma, va);
}
```

### unmap

注意用户 unmap 的长度可能小于文件本身的长度，即用户可能只释放了文件的一部分，所以我们需要维护文件的起始地址以及长度。由于题目中释放的地址一定是起始地址，所以在内存中的文件一定是连续的，为映射的维护提供了便利。

另外需要注意，若映射被设置了 `MAP_SHARED` 属性，我们需要将内存中的数据写回(如果被分配了物理页)。

```c
// in kernel/sysfile.c

uint64
sys_munmap(void)
{
  uint64 addr;
  int length;

  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0) 
    return -1;

  struct proc *p = myproc();

  struct vma * vma = 0;
  int index=-1;
  for (int i = 0; i < NVMA; i++) {
    if (p->vma[i] && (addr < p->vma[i]->end && addr >= p->vma[i]->start)) {
      vma = p->vma[i];
      index = i;
      break;
    }
  }

  if (!vma) return -1;

  if (walkaddr(p->pagetable, addr)) {
    if (vma->flags & MAP_SHARED) {
      filewrite(vma->file, addr, length);
    }

    uvmunmap(p->pagetable, PGROUNDDOWN(addr), PGROUNDUP(length + (addr - PGROUNDDOWN(addr))) / PGSIZE, 1);
  }

  vma->length -= length;

  if (addr == vma->start) {
    vma->start += length;
  }

  if (vma->length == 0) {
    fileclose(vma->file);
    vmadealloc(vma);
    p->vma[index] = 0;
  }

  return 0;
}
```

### maintain the `vma` in fork() and exit()

fork 和 exit 的时候也需要维护映射的文件。

fork 中，父进程建立的所有映射都要复制到子进程中，文件的引用数也要增加。

```c
// part of function fork() in kernel/proc.c

for (int i = 0; i < NVMA; i++) {
    struct vma *new_vma = 0, *vma = p->vma[i];
    if (vma) {
        if ((new_vma = vmaalloc()) == 0)
        panic("fork: no enough vma");
        new_vma->file = filedup(vma->file);
        new_vma->start = vma->start;
        new_vma->end = vma->end;
        new_vma->prot = vma->prot;
        new_vma->flags = vma->flags;
        new_vma->length = vma->length;
    }
    np->vma[i] = new_vma;
}

np->max_addr = p->max_addr;

```

exit 中则要考虑映射文件是否要写回。

```c
// part of function exit() in kernel/proc.c

for (int i = 0; i < NVMA; i++) {
    if (p->vma[i]) {
        uint64 start;
        if ((start = walkaddr(p->pagetable, p->vma[i]->start))) {
            struct vma *vp = p->vma[i];
            if (vp->flags & MAP_SHARED) {
                filewrite(vp->file, vp->start, vp->length);
            }
            uvmunmap(p->pagetable, PGROUNDDOWN(vp->start), (vp->end - PGROUNDDOWN(vp->start)) / PGSIZE, 1);
        }
        fileclose(p->vma[i]->file);
        vmadealloc(p->vma[i]);
        p->vma[i] = 0;
    }
}

p->max_addr = MAXVMA;
```

---

更多相关代码，详见附录10.1