#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

const int kMaxArgLen = 255;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("xargs need a argument\n");
    exit(1);
  }
  
  // 保存参数的数组
  char *args[MAXARG];
  char c;
  char prev_c = '\0';
  char curr_arg[kMaxArgLen];

  int arg_cnt = 0;
  int cnt = 0;

  // 保存xargs传入的参数
  for (int i = 1; i < argc; i++) {
    args[arg_cnt++] = argv[i]; 
  }


  while (read(0, &c, 1) != 0) {
    if ((c == 'n' && prev_c == '\\') || c == '\n') {
      if (c == 'n' && prev_c == '\\') {
        curr_arg[cnt - 1] = '\0';
      }
      args[arg_cnt] = curr_arg;
      int pid = fork();
      if (pid < 0) {
        printf("fork fail\n");
        exit(1);
      } else if (pid == 0) {
        exec(argv[1], args);
      }
      wait(0);

      // 重置curr_arg数组
      memset(curr_arg, 0, sizeof(curr_arg));
      cnt = 0;
    } else {
      curr_arg[cnt++] = c;
      prev_c = c;
    }
  }
  exit(0);
}