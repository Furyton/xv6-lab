# syscall

## system call tracing

要求使内核能够打印出想要查看的系统调用的调用情况，包括pid、系统调用名称以及对应的返回值。具体的，可以通过设置 mask 来选择期望查看的系统调用号。

```c
// in syscall.c

static char *sysname[] = {
[SYS_fork]    "fork",
[SYS_exit]    "exit",
[SYS_wait]    "wait",
[SYS_pipe]    "pipe",
[SYS_read]    "read",
[SYS_kill]    "kill",
[SYS_exec]    "exec",
[SYS_fstat]   "fstat",
[SYS_chdir]   "chdir",
[SYS_dup]     "dup",
[SYS_getpid]  "getpid",
[SYS_sbrk]    "sbrk",
[SYS_sleep]   "sleep",
[SYS_uptime]  "uptime",
[SYS_open]    "open",
[SYS_write]   "write",
[SYS_mknod]   "mknod",
[SYS_unlink]  "unlink",
[SYS_link]    "link",
[SYS_mkdir]   "mkdir",
[SYS_close]   "close",
[SYS_trace]   "trace",
[SYS_sysinfo] "sysinfo",
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    if(p->mask & (((uint64)1) << num)) {
      printf("%d: syscall %s -> %d\n", p->pid, sysname[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}

```

为了打印系统调用名称，我们需要一个字符串数组记录他们的名字，即 `sysname` 数组。

由于在 user.pl 里，系统调用号被放置在 a7 寄存器中，系统调用的返回值被放置在 a0 寄存器中，所以我们可以在 trapframe 中恢复获得相应的系统调用号和返回值。而mask值，我们可以存放在进程控制块中(struct proc)。当用户调用 trace 时，只需将 proc 中的 mask 设置为给定值即可。

相关代码详见 附录2.1

## Sysinfo

要求添加一个系统调用，用来获取当前系统的信息（内存空闲字节数和已使用的进程数）。