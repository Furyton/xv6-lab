// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

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

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;


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

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
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

  acquire(&bcache.lock);

  struct buf* LRU_buf = 0;

  int i, chc=-1;
  for (i = 0; i < BUCKET_LEN; i++) {
    // if(i != hc) acquire(&bcache.bucket[i].lock);

    for (b = bcache.bucket[i].head.next; b != &bcache.bucket[i].head; b = b->next) {
      if (b->refcnt == 0 && (!LRU_buf || b->time_stamp < LRU_buf->time_stamp)) {
        LRU_buf = b;
        chc = i;
      }
    }
    // if (i!=hc && !flag) release(&bcache.bucket[i].lock);
  }
  if (LRU_buf) {
    if (chc != hc) {
      acquire(&bcache.bucket[chc].lock);
      pop(LRU_buf);
      push(LRU_buf, hc);
      release(&bcache.bucket[chc].lock);
    }
    LRU_buf->dev = dev;
    LRU_buf->blockno = blockno;
    LRU_buf->valid = 0;
    LRU_buf->refcnt = 1;
    LRU_buf->time_stamp = ticks;
    release(&bcache.bucket[hc].lock);
    release(&bcache.lock);
    acquiresleep(&LRU_buf->lock);
    return LRU_buf;
  }

  // acquire(&bcache.lock);

  // struct buf* LRU_buf = 0;
  // uint shc = -1, chc; // source hashcode, current hashcode

  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   chc = HASH(b->blockno);
  //   if (LRU_buf && chc == shc) { // bucket has locked
  //     if (b->refcnt == 0 && b->time_stamp < LRU_buf->time_stamp) {
  //       LRU_buf = b;
  //     }
  //   } else if (LRU_buf) { // need to lock a different bucket
  //     if (chc != hc) acquire(&bcache.bucket[chc].lock);

  //     if (b->refcnt == 0 && b->time_stamp < LRU_buf->time_stamp) {
  //       LRU_buf = b;
  //       if (shc != hc) release(&bcache.bucket[shc].lock);
  //       shc = chc;
  //     } else {
  //       if (chc != hc) release(&bcache.bucket[chc].lock);
  //     }
  //   } else { // haven't found unused buff
  //     if (chc != hc) acquire(&bcache.bucket[chc].lock);
  //     if (b->refcnt == 0) {
  //       LRU_buf = b;
  //       shc = chc;
  //     }
  //   }
  // }
  // if (LRU_buf) {

  //   if (hc != shc) {
  //     pop(LRU_buf);
  //     push(LRU_buf, hc);
  //   }

  //   LRU_buf->dev = dev;
  //   LRU_buf->blockno = blockno;
  //   LRU_buf->valid = 0;
  //   LRU_buf->refcnt = 1;
  //   LRU_buf->time_stamp = ticks;
  //   if(shc != hc) release(&bcache.bucket[shc].lock);
  //   release(&bcache.bucket[hc].lock);
  //   release(&bcache.lock);
  //   acquiresleep(&LRU_buf->lock);
  //   return LRU_buf;
  // }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint hc = HASH(b->blockno);
  acquire(&bcache.bucket[hc].lock);
  // acquire(&bcache.lock);
  b->refcnt--;
  b->time_stamp = ticks;
  // release(&bcache.lock);
  release(&bcache.bucket[hc].lock);
}

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


