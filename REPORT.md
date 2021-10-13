# trap

## backtrace

要求实现 backtrace 函数，能够在系统出错时，输出一系列调用函数的地址。

在栈帧中，返回地址和栈基地址的相对位置是固定的，因此，我们可以很容易的根据栈指针的位置获得调用函数的地址和上一个栈帧的位置，分别是`fp-8` 和 `fp-16`。并据此进行递归查找，并输出。

```c
// in kernel/printf.c

void
backtrace(void)
{
  uint64 fp = r_fp();
  uint64 limit_down = PGROUNDDOWN(fp), limit_up = PGROUNDUP(fp);
  printf("backtrace:\n");

  while(fp >= limit_down && fp < limit_up) {
    printf("%p\n", *((uint64*)(fp - 8)));
    fp = *((uint64*)(fp - 16));
  }
}
```

## alarm

要求能够实现 alarm 功能，即用户可以设定一段时间后运行给定的函数。主要思路是，在进程控制块中设定一个计数器，表明剩余的 ticks，之后修改 usertrap，使他能够对每一个时钟中断进行处理，更新剩余 ticks，若到时间，设置返回地址为给定函数的地址，并重置计数器。

### record

进程控制块中需要记录很多必要的信息。

```c
// part of struct proc in kernel/proc.h

int ticks;                   // Ticks
int r_ticks;                 // Remained ticks before next handler call
uint64 alarm_handler;     // Alarm handler
int handling;
struct trapframe *resume_trapframe;
```

- ticks：用户设置的滴答数。
- r_ticks：当前剩余的滴答数。
- alarm_handler：用户设置的需要定时调用的函数。
- handling：表示当前是否正在执行 `handler` 函数，类似于锁，防止`handler`运行时间过长，ticks过短，发生冲突。
- resume_trapframe：记录 `handler` 运行结束后需要返回的 context。

这里面，`resume_trapframe` 是需要在进程分配和释放中需要管理的，与原本的 `trapframe` 处理方式基本相同。

```c

// part of function allocproc() in kernel/proc.c

// Allocate a trapframe page.
if((p->trapframe = (struct trapframe *)kalloc()) == 0 || (p->resume_trapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
}

// part of function freeproc() in kernel/proc.c

// free resume trapframe page
if(p->resume_trapframe)
    kfree((void*)p->resume_trapframe);
```

### register alarm handler and remove

alarm 的系统调用实质上就是将用户传递的滴答数和`handler`函数设置好，返回时将 `trapframe` 恢复的一个过程。

其中要注意对 handling 变量的更新。

```c
// in kernel/sysproc.c

uint64
sys_sigalarm(void)
{
  int ticks, handler;

  if(argint(0, &ticks) < 0 || argint(1, &handler) < 0) {
    return -1;
  }

  struct proc* p = myproc();
  p->ticks = p->r_ticks = ticks;
  p->alarm_handler = (uint64)handler;
  p->handling = 0;

  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc* p = myproc();

  if (p->handling) {
    *(p->trapframe) = *(p->resume_trapframe);
    p->handling = 0;
  }

  return 0;
}
```

### dealing alarm in usertrap()

关键的部分在于处理时钟中断。当发生时钟中断后，首先更新 `r_ticks`，若发现它到了 0 后开始执行以下 (若 `r_ticks-- = 1`，那么此时 `r_ticks` 已经为 0)。

- 重置 `r_ticks`
- 保存 `trapframe`
- 设置返回地址为 `alarm_handler`
- 更新 `handling`

```c
// part of function usertrap() in kernel/trap.c

// give up the CPU if this is a timer interrupt.
if(which_dev == 2) {
    if (p->r_ticks-- == 1) {
        p->r_ticks = p->ticks;
        if (!p->handling) {
            *(p->resume_trapframe) = *(p->trapframe);
            p->trapframe->epc = p->alarm_handler;
            p->handling = 1;
        }
    }

    yield();
}
```

更多相关代码 详见 4.1
