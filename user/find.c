#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *target;

char*
fmtname(char *path)
{
  static char buf_name[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memset(buf_name, 0, sizeof(buf_name));
  memmove(buf_name, p, strlen(p));

  return buf_name;
}

void check_file(char *path) {
    if (strcmp(fmtname(path), target) == 0) {
        printf("%s\n",path);
    }
}

char buf[512];

void explore_dir(char *path) {
 
    char *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);

        return;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (st.type == T_FILE) {
        fprintf(2, "find: %s not a dir\n", path);
        return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");

        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        if (stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }

        if (st.type == T_FILE) {
            check_file(buf);
        } else if (st.type == T_DIR) {
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;

            explore_dir(buf);
        }
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(2, "too few arg\n");
        exit(-1);
    }

    target = argv[2];

    explore_dir(argv[1]);

    exit(0);
}