#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 递归遍历文件夹和子文件夹
void RecursiveFind(const char *path, const char *find_name) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }


  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
    printf("find: path too long\n");
    close(fd);
    return;
  }

  // 方便打印最终路径
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';

  // 遍历目录
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if (stat(buf, &st) < 0) {
      printf("find: cannot stat %s\n", buf);
      continue;
    }

    // 如果是目录，递归遍历
    if (st.type == T_DIR) {
      if (!strcmp(".", de.name) || !strcmp("..", de.name)) {
        continue;
      }
      RecursiveFind(buf, find_name);
    }

    // 如果是文件，则与需要查找的名称比较，相同则输出
    if (st.type == T_FILE) {
      if (!strcmp(find_name, de.name)) {
        printf("%s\n", buf);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(2, "find need 2 argument\n");
    exit(1);
  }

  char *path = argv[1];
  char *find_name = argv[2];
  
  RecursiveFind(path, find_name);

  exit(0);
}