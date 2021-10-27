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

## Symbolic links

要求实现一个简化版的软链接。用户可以选择建立一个软连接或者打开一个软链接的文件。

### create

用户创建软链接的目标地址可能也是一个软链接文件，所以需要去沿着链接找到真正的被链接文件。

查找的过程中可能遇到环，这里简单的设立一个阈值，迭代次数超过阈值则认为失败。

```c
// in kernel/sysfile.c

int
follow_link_path(int threshold, char* symlink_tpath)
{
  struct inode *tip;  // target inode pointer
  
  while(threshold --) {
    if ((tip = namei(symlink_tpath)) == 0) {
      return 1;
    }

    ilock(tip);
    if (tip->type == T_SYMLINK) {
      strncpy(symlink_tpath, tip->target, MAXPATH);
      iunlockput(tip);
    } else {
      iunlockput(tip);
      break;
    }
  }

  if (threshold <= 0) return -1;
  return 0;
}
```

所以建立的过程即，首先找到真正的目标链接文件，之后新建类型为 `T_SYMLINK` 的文件。

```c
// in kernel/sysfile.c

uint64
sys_symlink(void)
{
  char path[MAXPATH], target[MAXPATH];

  struct inode *ip;

  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;
  
  int threshold = 10;
  
  begin_op();
  
  if (follow_link_path(threshold, target) < 0){
    end_op();
    return -1;
  }

  if ((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
    end_op();
    return -1;
  }

  strncpy(ip->target, target, MAXPATH);

  iunlockput(ip);
  end_op();

  return 0;
}
```

### open

打开文件时用户可指定 `O_NOFOLLOW` 参数，即打开软链接文件本身还是对应的目标文件。查找目标文件只需调用 `follow_link_path()` 函数即可。

```c
// part of function sys_open() in kernel/sysfile.c

if (!(omode & O_NOFOLLOW) && ip->type == T_SYMLINK) {
  strncpy(target, ip->target, MAXPATH);
  int threshold = 10;
  if (follow_link_path(threshold, target) != 0) {
    iunlockput(ip);
    end_op();
    return -1;
  } else {
    iunlockput(ip);
    if((ip = namei(target)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
  }
}
```

---

更多相关代码，详见 附录9.1