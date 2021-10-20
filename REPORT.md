# lazy

trap.c
vm.c
sysproc.c

要求修改 `sbrk()` 系统调用，实现 lazy on allocation 机制。当进程调用 `sbrk()` 以分配更多内存空间时，仅仅更新进程控制块中的进程大小的信息，当并不真正地进行分配操作。真正的进程分配操作发生在 `usertrap()` 当中，即发生了页错误的时候。

## remove the allocation in `sbrk()` system call

首先应该做的是修改`sbrk()`，当 `n` 大于 0 时，表示用户希望增加进程的内存空间，这时我们应只修改 `proc->sz`。

```c
// in sysproc.c

uint64
sys_sbrk(void)
{
  int n;
  struct proc *proc = myproc();

  if(argint(0, &n) < 0)
    return -1;

  uint64 oldsz = proc->sz;

  if (n > 0)  proc->sz += n;
  else growproc(n);

  return oldsz;
}
```

## modify usertrap() to handle lazy allocation

当发生页错误时，若是读或写错误(`scause`等于15或13)，则开始考虑进行分配。

首先需要检查地址是否合法，即是否在用户请求的进程大小范围内以及是否超过了用户进程的最大限制。

```c
// part of function usertrap() in kernel/trap.c

if (va >= p->sz || va <= PGROUNDDOWN(p->trapframe->sp)) {
  printf("usertrap: illegal address\n");
  goto bad;
}
```

接下来进行页的分配和映射。

```c
// part of function usertrap() in kernel/trap.c

mem = kalloc();
if(mem == 0) {
  printf("usertrap: allocate new page failed\n");
  goto bad;
} else {
  if(mappages(pagetable, va, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0) {
    kfree(mem);
    printf("usertrap: map page failed\n");
    goto bad;
  }
}
```

## modify other places where it uses the lazy pages

在所有的关于内存读写的操作都需要通过 `walkaddr` 查找对应的物理页。若发现当前页没有进行分配的时候，即进行lazy allocation

```c
// part of function 'uint64 walkaddr(pagetable_t pagetable, uint64 va)' 
// in kernel/vm.c


pte = walk(pagetable, va, 0);

if(pte == 0 || (*pte & PTE_V) == 0)
{
  struct proc *p = myproc();
  if (va >= p->sz || va < PGROUNDUP(p->trapframe->sp))
    return 0;
  char *mem = kalloc();
  
  if((pte = walk(p->pagetable, PGROUNDDOWN(va), 1)) == 0)
    return 0;
  *pte = PA2PTE(mem) | PTE_W|PTE_X|PTE_R|PTE_U | PTE_V;
  return (uint64)mem;
}
```

更多相关代码，详见 附录5.1