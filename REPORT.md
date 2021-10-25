# fs

## Large files

要求为文件设置二级索引，用来放置更大的文件。处理的思路与一级索引类似，只是需要递归的去做两遍。

这里将最后一个一级索引块修改为二级索引块。

### bmap

根据给定的相对块号，若发现它处于二级索引块的范围内时，则需要开始进行二级索引。

首先求出相对于二级索引的一级块号(`bn`)，及块内的地址(`subaddr`)，仿照一级索引块的写法，获得 bn 块的内容。接下来用同样的方法获得二级索引块的内容，从而获得正确的地址。

```c
// part of function bmap() in kernel/fs.c

bn -= NINDIRECT;

if(bn < NDINDIRECT){
    subaddr = bn % NINDIRECT;
    bn /= NINDIRECT;

    if((addr = ip->addrs[NDIRECT + 1]) == 0)
        ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if ((addr = a[bn]) == 0) {
        a[bn] = addr = balloc(ip->dev);
        log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if ((addr = a[subaddr]) == 0) {
        a[subaddr] = addr = balloc(ip->dev);
        log_write(bp);
    }
    brelse(bp);

    return addr;
}
```

### free

为了便于实现，将一级索引的清除进行了封装，便于二级索引的实现。

`free_indirect_block()` 用来清除一个一级索引块内的所有内容。

```c
// in kernel/fs.c

void
free_indirect_block(uint dev, uint addr)
{
  struct buf *bp;
  int i;
  uint *a;

  bp = bread(dev, addr);
  a = (uint*)bp->data;
  for (i = 0; i < NINDIRECT; i++) {
    if (a[i])
      bfree(dev, a[i]);
  }
  brelse(bp);
  bfree(dev, addr);
}
```

二级索引块的清除可以用相同的方式进行清除，只是对块的清除 `bfree` 改为了 `free_indirect_block()`

```c

void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    free_indirect_block(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  if(ip->addrs[NDIRECT + 1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;

    for (j = 0; j < NINDIRECT; j++) {
        if (a[j])
          free_indirect_block(ip->dev, a[j]);
        // a[j] = 0;
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```