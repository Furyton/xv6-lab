# lock

## Memory allocator

要求修改 `kalloc.c` 中的实现，使得为每个 CPU 设立单独的 `freelist`，以此来提高效率。

每个 CPU 的 `freelist` 应当是动态维护的，当它为空时，需要从其他 CPU 的 `freelist` 中去 `偷' 取一个空闲页，而这一操作会引发冲突，因此需充分利用锁机制。

### init

最开始初始化每个空闲列表的锁，然后默认将所有的空闲页挂在 0 号 CPU 上。

```c
// in kernel/kalloc.c

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
}
```

## kfree

页的释放过程只需在适当的位置加上锁即可。

```c
// in kernel/kalloc.c

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int cid = get_cpu_id();

  acquire(&kmem[cid].lock);
  r->next = kmem[cid].freelist;
  kmem[cid].freelist = r;
  release(&kmem[cid].lock);
}
```

## kalloc

分配空闲页的过程中，若发生空闲页不足，需要从其他 CPU 中获得空闲页是，可能会发生较多的冲突，因此需要设置更多的检测。

若当前的 CPU 为 `cur_cid` 时，若 `cid` 号 CPU 中有空闲页时，我们考虑将他取出，在这一过程，我们始终获取着 `cid` 空闲列表的锁。

```c
// in kernel/kalloc.c

struct run*
steal(int cur_cid) {
  int cid;
  struct run* r;
  for (cid = 0; cid < NCPU; cid ++) {
    if (cid == cur_cid) continue;
    if (!kmem[cid].freelist) continue;
    
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

```

## Buffer cache

磁盘 buffer 的管理是由一个链表完成，为了提高效率，要求用多个链表共同完成 buffer 的管理，为了解决冲突，需要锁来完成。

### multi-links based on hash code

使用哈希值来将所有的块号映射到一个较小的范围内，相同哈希值的块缓存放置在同一个链表中，同时，每个链表需要一个锁。

这里取 HASE_BASE 为 13，0 号桶用来放置初始时刻所有的缓存块。

```c
// in kernel/bio.c

#define HASH_BASE 13
#define BUCKET_LEN (HASH_BASE + 1)

#define HASH(x) (((x) % (HASH_BASE)) + ((x)?1:0))

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket bucket[BUCKET_LEN];

} bcache;
```

相应的要实现对链表的简单操作，这里的操作默认执行前已经获得锁。

```c
// in kernel/bio.c

void
push(struct buf* buf, uint hc) {
  buf->next = bcache.bucket[hc].head.next;
  buf->prev = &bcache.bucket[hc].head;

  bcache.bucket[hc].head.next->prev = buf;
  bcache.bucket[hc].head.next = buf;
}

void
pop(struct buf* buf) {
  buf->next->prev = buf->prev;
  buf->prev->next = buf->next;
}
```

### init

初始时刻，我们将所有的 bucket 初始化，并将所有的块 buffer 放到 0 号 bucket 中。

```c
// in kernel/bio.c

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  int bid;
  for (bid = 0; bid < BUCKET_LEN; bid ++) {
    initlock(&bcache.bucket[bid].lock, "bcache_");
    bcache.bucket[bid].head.prev = &bcache.bucket[bid].head;
    bcache.bucket[bid].head.next = &bcache.bucket[bid].head;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->time_stamp = ticks;
    push(b, 0);
  }
}
```

### bget

最复杂的操作时获取给定块号的块 buffer，若找不到，则根据 LRU 选择空闲的 buffer。

首先看简单的情形。根据块号 `blockno`，我们可以获取它的哈希值，继而找到对应的 bucket，若能在链表中找到匹配的 buffer，则释放锁，加入到等待队列，并返回这个块 buffer。

注意，若没有找到，当前的锁没有被释放，因此我们不能在后面的操作里重复获取锁。

```c
// kernel/bio.c

struct buf *b;

uint hc = HASH(blockno); // hashcode

acquire(&bcache.bucket[hc].lock);
for (b = bcache.bucket[hc].head.next; b != &bcache.bucket[hc].head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        b->time_stamp = ticks;
        release(&bcache.bucket[hc].lock);
        acquiresleep(&b->lock);
        return b;
    }
}
```

若没有找到，则按一定顺序取遍历所有的 bucket，直到找到空闲的 buffer。这里是近似的 LRU 算法，考虑的是某一个桶内的最久未用 buffer。发现有新的空闲 buffer 的时间戳更小时，我们会放弃先前找过的 buffer，释放对应的锁(注：若上一个找到的哈希值等于 `hc`，则不进行锁操作)，但在某一个 bucket 中找到了一个空闲时间戳最小 buffer 后，立即跳出循环。

```c
// in kernel/bio.c

struct buf* LRU_buf = 0;

int i, chc=-1, flag = 0;
for (i = hc; i < BUCKET_LEN + hc; i++) {
    int hc_ = i % BUCKET_LEN;
    if(hc_ != hc) acquire(&bcache.bucket[hc_].lock);
    flag = 0;
    for (b = bcache.bucket[hc_].head.next; b != &bcache.bucket[hc_].head; b = b->next) {
        if (b->refcnt == 0 && (!LRU_buf || b->time_stamp < LRU_buf->time_stamp)) {
        LRU_buf = b;
        if (chc != -1 && chc != hc && chc != hc_) 
            release(&bcache.bucket[chc].lock);
        chc = hc_;
        flag = 1;
        }
    }
    if (hc_!=hc && !flag) release(&bcache.bucket[hc_].lock);
    else if (flag) goto succ;
}

```

若成功找到空闲 buffer，首先需要判断是否需要将这一个 buffer 换到正确的 bucket 中，之后设置对应的块信息，释放锁并返回。

### brelse

释放的过程较为简单，我们只需修改它的引用数量即可，同时更新 `time_stamp`

```c
// in kernel/bio.c

void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint hc = HASH(b->blockno);
  acquire(&bcache.bucket[hc].lock);
  b->refcnt--;
  b->time_stamp = ticks;
  release(&bcache.bucket[hc].lock);
}
```

### other oper

其他关于块 buffer 的操作均需要锁来维护

```c
// in kernel/bio.c

void
bpin(struct buf *b) {
  acquire(&bcache.bucket[HASH(b->blockno)].lock);
  b->refcnt++;
  release(&bcache.bucket[HASH(b->blockno)].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.bucket[HASH(b->blockno)].lock);
  b->refcnt--;
  release(&bcache.bucket[HASH(b->blockno)].lock);
}

```

---

更多相关代码，详见附录 9.1