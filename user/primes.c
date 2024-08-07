#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include <stdbool.h>

// 流水线输出（通过递归实现）
void Recursion(int *fds_left) {
  int fds_child[2];
  if (pipe(fds_child) < 0) {
    printf("pipe error\n");
    exit(1);
  }

  int temp;
  int prime = 0;
  bool is_first = true;
  bool is_fork = false;

  // 从左边的进程读取
  while (read(fds_left[0], &temp, 4) != 0) {
    if (is_first) {
      prime = temp;
      printf("prime %d\n", prime);
      is_first = false;
    } else {
      // 向右边的进程传递数据
      if (temp % prime != 0) {
        write(fds_child[1], &temp, 4);
        if (is_fork == false) {
          int child_pid = fork();
          if (child_pid < 0) {
            printf("fork fail\n");
            exit(1);
          } else if (child_pid == 0) {
            // 关闭右边进程的写端
            close(fds_child[1]);
            Recursion(fds_child);
            exit(0);
          } else {
            // 关闭左边进程的读端
            close(fds_child[0]);
            is_fork = true;
          }
        }
      }
    }
  }
  close(fds_left[0]);
  close(fds_child[1]);
  wait(0);
}

int main() {
  int first_prime = 2;
  int fds_main[2];

  // 创建管道
  if (pipe(fds_main) < 0) {
    printf("pipe error\n");
    exit(1);
  }

  printf("prime %d\n", first_prime);
  int pid = fork();

  if (pid < 0) {
    printf("fork error\n");
    exit(1);
  } else if (pid == 0) {
    // 关闭右边进程的写端
    close(fds_main[1]);
    Recursion(fds_main);
    exit(0);
  } else {
    // 关闭左边进程的读端
    close(fds_main[0]);
    for (int i = 3; i <= 35; i++) {
      if (i % first_prime != 0) {
        // 向子进程写入
        write(fds_main[1], &i, 4);
      }
    }
    // 关闭不需要使用的管道
    close(fds_main[1]);
  }
  wait(0);
  exit(0);
}