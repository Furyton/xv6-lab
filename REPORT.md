# thread

实现一个用户级的线程切换。

## context

与内核中的进程切换类似，这里需要保存线程的上下文，也就是各种寄存器的值。

```c
// in user/uthread.c

struct context{
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

类似的，可以实现上下文的切换。

```x86asm
/* in user/uthread_switch.S */

sd ra, 0(a0)
sd sp, 8(a0)
sd s0, 16(a0)
sd s1, 24(a0)
sd s2, 32(a0)
sd s3, 40(a0)
sd s4, 48(a0)
sd s5, 56(a0)
sd s6, 64(a0)
sd s7, 72(a0)
sd s8, 80(a0)
sd s9, 88(a0)
sd s10, 96(a0)
sd s11, 104(a0)

ld ra, 0(a1)
ld sp, 8(a1)
ld s0, 16(a1)
ld s1, 24(a1)
ld s2, 32(a1)
ld s3, 40(a1)
ld s4, 48(a1)
ld s5, 56(a1)
ld s6, 64(a1)
ld s7, 72(a1)
ld s8, 80(a1)
ld s9, 88(a1)
ld s10, 96(a1)
ld s11, 104(a1)

ret    /* return to ra */

```

所以，当我们想要从当前线程切换到下一个线程的时候，可以调用这一函数实现上下文的切换。如

```c
// part of function thread_schedule() in user/uthread.c

thread_switch((uint64)&t->ctx, (uint64)&next_thread->ctx);
```

## initialize context

对于上下文的初始化，我们需要赋予它一个初始的返回地址，也就是切换到这一线程后要执行的指令地址，还有栈地址。由于`struct thread`中定义了一段栈空间，我们据此来赋值 `sp`，但要注意栈地址的增长方向是由大到小，所以应将 `thread->stack + STACK_SIZE` 赋给 `sp`。

```c
// in user/uthread.c

void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  memset(&t->ctx, 0, sizeof(t->ctx));
  t->ctx.ra = (uint64)func;
  t->ctx.sp = (uint64)(t->stack + STACK_SIZE);
}
```

---

更多相关代码，详见 8.1