// snek - a Snake clone
// Written in 2024 by Dana Larose <ywg.dana@gmail.com>
//
// To the extent possible under law, the author has dedicated all copyright
// and related and neighboring rights to this software to the public domain
// worldwide. This software is distributed without any warranty.
//
// You should have received a copy of the CC0 Public Domain Dedication along 
// with this software. If not, 
// see <http://creativecommons.org/publicdomain/zero/1.0/>.

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define VERSION "0.0.1"

#define MIN_WIN_HEIGHT 30
#define MIN_WIN_WIDTH 100

#define CTRL_KEY(k) ((k) & 0x1f)

// prototypes
void clear_screen(void);
void hide_cursor(void);

// Configure the terminal for raw input/output, turn off key echoing, etc.

// I learned how to do the raw terminal i/o stuff from the neat
// Build Your Own Text Editor tutorial over at 
// https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html

struct termios orig_termios;

void title_screen(void)
{
  clear_screen();

  char *border = malloc(MIN_WIN_WIDTH + 1);
  memset(border, ' ', MIN_WIN_WIDTH);
  border[MIN_WIN_WIDTH] = '\0';

  char *blank = malloc(MIN_WIN_WIDTH - 1);
  memset(blank, ' ', MIN_WIN_WIDTH - 2);
  blank[MIN_WIN_WIDTH - 2] = '\0';

  hide_cursor();

  write(STDOUT_FILENO, "\x1b[47m", 5);
  write(STDOUT_FILENO, border, MIN_WIN_WIDTH);
  write(STDOUT_FILENO, "\x1b[m", 3);
  write(STDOUT_FILENO, "\r\n", 2);

  for (int r = 1; r < MIN_WIN_HEIGHT - 1 ; r++) {
    write(STDOUT_FILENO, "\x1b[47m", 5);
    write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, "\x1b[m", 3);

    if (r == MIN_WIN_HEIGHT / 3) {
      char title[80];
      sprintf(title, "~~ SNEK! %s ~~", VERSION);
      size_t len = strlen(title);
      size_t padding = (MIN_WIN_WIDTH - 2 - len) / 2;
      for (size_t j = 0; j < padding; j++)
         write(STDOUT_FILENO, " ", 1);
    
      write(STDOUT_FILENO, title, len);

      for (size_t j = padding + len + 1; j < MIN_WIN_WIDTH - 1; j++)
        write(STDOUT_FILENO, " ", 1);
    }
    else if (r == MIN_WIN_HEIGHT / 3 + 2) {
      char *msg = "press space to begin";
      size_t len = strlen(msg);
      size_t padding = (MIN_WIN_WIDTH - 2 - len) / 2;
      for (size_t j = 0; j < padding; j++)
         write(STDOUT_FILENO, " ", 1);

      write(STDOUT_FILENO, msg, len);

      for (size_t j = padding + len + 1; j < MIN_WIN_WIDTH - 1; j++)
        write(STDOUT_FILENO, " ", 1);
    }
    else {
      write(STDOUT_FILENO, blank, MIN_WIN_WIDTH - 2);
    }

    write(STDOUT_FILENO, "\x1b[47m", 5);
    write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, "\x1b[m", 3);

    write(STDOUT_FILENO, "\r\n", 2);
  }

  write(STDOUT_FILENO, "\x1b[47m", 5);
  write(STDOUT_FILENO, border, MIN_WIN_WIDTH);
  write(STDOUT_FILENO, "\x1b[m", 3);
  write(STDOUT_FILENO, "\r\n", 2);

  free(border);
  free(blank);
}

void die(const char *s)
{
  clear_screen();
  perror(s);
  exit(1);
}

void clear_screen(void)
{
  // clear the screen
  write(STDOUT_FILENO, "\x1b[2J", 4);

  // move cursor to to top left
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void hide_cursor(void)
{
  write(STDOUT_FILENO, "\x1b[?25l", 6);
}

void show_cursor(void)
{
  write(STDOUT_FILENO, "\x1b[?25h", 6);
}

void exit_raw_mode(void) 
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
  show_cursor();
}

void enter_raw_mode(void)
{
  // Stash the termios settings we had upon loading snek so
  // we can restore them when we exit
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgeattr");

  atexit(exit_raw_mode);

  // Turn off echoing keys and canonical mode (so we can read
  // input byte-by-byte instead of line-by-line)
  // Flipping ISIG turns off SIGINT (ctrl-c) and SIGSTP (ctrl-z)
  // for the program. Flipping IXON turns off ctrl-s and ctrl-q
  // Flipping IEXTEN turns off ctrl-v. OPOST turns off output
  // processing
  struct termios raw = orig_termios;
  raw.c_iflag &= ~(IXON);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_oflag &= ~(OPOST);

  // Set a timeout on read() so that we can have an actual game loop
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

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

// terminal i/o stuff

char update(void)
{
  char c = '\0';
  if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
  
  if (c == CTRL_KEY('q')) 
    exit(0);

  return c;
}

void render(void)
{
  clear_screen();
}

int main(void)
{
  if (!valid_window_size())
  {
    printf("Please open snek in a terminal that's at least %dx%d\n",
          MIN_WIN_HEIGHT, MIN_WIN_WIDTH);
    
    return 1;
  }

  enter_raw_mode();

  title_screen();

  // main game loop
  while (true) {
   char c = update();
   if (c == ' ') {
    exit_raw_mode();
    printf("new game!\n");
    break;
   }

   //render();
  }
}