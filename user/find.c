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

// struct list{
//     char *name;
//     struct list *next;
// };

void explore_dir(char *path) {
    // printf("I am exploring %s\n", path);
    
    char *p;
    int fd;
    struct dirent de;
    struct stat st;

    // struct list *head = 0;

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

    // printf("%s: step 1\n", path);
    
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    // printf("%s: step 2, %s\n", path, buf);
    
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

            // printf("I am going to explore %s\n", buf);
            explore_dir(buf);
            // char *name = (char*) malloc(strlen(de.name) + 1);
            // strcpy(name, de.name);

            // struct list *newNode = (struct list*) malloc(sizeof(struct list));

            // newNode->name = name;
            // if (head) {
            //     newNode->next = head;
            //     head = newNode;
            // } else {
            //     head = newNode;
            //     head->next = 0;
            // }
            // // newNode->next = head.next;
            // // head.next = newNode;
        }
    }
    close(fd);

    // struct list *cur = head;

    // while(cur) {
    //     memmove(p, cur->name, DIRSIZ);
    //     p[DIRSIZ] = 0;
    //     explore_dir(buf);
    //     cur = cur->next;
    // }

    // cur = head;
    // while(cur) {
    //     struct list *tmp = cur->next;
    //     free(cur);
    //     cur = tmp;
    // }
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