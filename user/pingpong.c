#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
  // 创建两个管道
  int fds_parent[2];
  int fds_child[2];
  if (pipe(fds_parent) < 0) {
    printf("pipe error\n");
    exit(1);
  }

  if (pipe(fds_child) < 0) {
    printf("pipe error\n");
    exit(1);
  }

  // fork出子进程
  int pid = fork();
  if (pid < 0) {
    printf("fork fail\n");
    exit(1);
  } else if (pid == 0) {
    // 读取父进程的数据
    char c;
    read(fds_parent[0], &c, 1);

    printf("%d: received ping\n", getpid());

    // 向父进程发送数据
    write(fds_child[1], &c, 1);
    exit(0);
  } else {
    char c = 'a';
    // 向子进程发送数据
    write(fds_parent[1], &c, 1);

    // 读取子进程的数据
    read(fds_child[0], &c, 1);
    printf("%d: received pong\n", getpid());
    exit(0);
  }
}