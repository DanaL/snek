#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MIN_WIN_HEIGHT 30
#define MIN_WIN_WIDTH 100

bool valid_window_size()
{
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return false;    
  }

  int  cols = ws.ws_col;
  int rows = ws.ws_row;
  
  if (cols < MIN_WIN_WIDTH || rows < MIN_WIN_HEIGHT)
    return false;
  
  return true;
}

int main(void)
{
  if (!valid_window_size())
  {
    printf("Please open snek in a terminal that's at least %dx%d\n",
          MIN_WIN_HEIGHT, MIN_WIN_WIDTH);
    
    return 1;
  }

  printf("snek!!\n");
}