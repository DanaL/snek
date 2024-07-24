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
#include <time.h>
#include <unistd.h>

#define VERSION "0.0.1"

#define INIT_SKEN_LEN 8

#define EMPTY 0
#define SNEK_HEAD 1
#define SNEK_BODY 2
#define MUSHROOM 3
#define SNEK_SNACK 4

#define NORTH 0
#define SOUTH 1
#define EAST 2
#define WEST 3

#define LEFT 0
#define RIGHT 1

#define MIN_WIN_HEIGHT 30
#define MIN_WIN_WIDTH 100

#define CTRL_KEY(k) ((k) & 0x1f)

#define GREEN 28
#define PURPLE 99
#define BLUE 33

#define ACCELERATION 500

// prototypes
void clear_screen(void);
void exit_raw_mode(void);
void hide_cursor(void);
char get_key(void);

// data structures for storing the snek and game state

struct game_state {
  uint32_t score;
  uint32_t acceleration;
  int *items;
  useconds_t speed;
};

struct pt {
  uint32_t row;
  uint32_t col;
  struct pt *next;
  struct pt *prev;
};

struct snek {
  struct pt *head;
  struct pt *tail;
  uint32_t dir;
};

struct snek *snek_init(void)
{
  struct snek *snek = malloc(sizeof(struct snek));
  snek->dir = EAST;

  // Create an initial snek that's roughly in the centre of the screen
  // and five segments long.
  uint32_t init_row = MIN_WIN_HEIGHT / 2;
  uint32_t init_col = MIN_WIN_WIDTH / 2 + 2;

  snek->head = malloc(sizeof(struct pt));
  snek->head->row = init_row;
  snek->head->col = init_col;
  snek->head->next = NULL;

  struct pt *p = snek->head;
  for (int j = 0; j < INIT_SKEN_LEN; j++) {
    struct pt *segment = malloc(sizeof(struct pt));
    segment->row = init_row;
    segment->col = p->col - 1;
    p->prev = segment;
    segment->next = p;
    segment->prev = NULL;
    p = segment;
  }

  snek->tail = p;

  return snek;
}

void items_init(struct game_state *gs, struct snek *snek)
{
  int count = 20;
  while (count > 0) {
    int row = rand() % (MIN_WIN_HEIGHT - 2) + 1;
    int col = rand() % (MIN_WIN_WIDTH - 2) + 1;

    if (row == snek->head->row && col == snek->head->col)
      continue;

    gs->items[row * MIN_WIN_WIDTH + col] = SNEK_SNACK;
    --count;
  }
}

void snek_destroy(struct snek *snek)
{
  struct pt *p = snek->head;
  while (p) {
    struct pt *seg = p;
    p = p->prev;
    free(seg);
  }

  free(snek);
}

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

  while (true) {
    char c = get_key();
    if (c == 'q') {
      exit(0);
    }
    else if (c == ' ') {
     break;
    }
  }
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

  // just in case, switch back to default fg colour
  write(STDOUT_FILENO, "\x1b[39m", 5);
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
  //raw.c_cc[VTIME] = 1;

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

char get_key(void)
{
  char c = '\0';
  if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
  
  if (c == CTRL_KEY('q')) {
    exit(0);
  }
  else if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      switch (seq[1]) {
        case 'A': return 'w';
        case 'B': return 's';
        case 'C': return 'd';
        case 'D': return 'a';
      }
    }
  }

  return c;
}

void invert(char *buf, size_t *pos)
{
  memcpy(&buf[*pos], "\x1b[47m", 5);
  *pos += 5;
}

void uninvert(char *buf, size_t *pos)
{
  memcpy(&buf[*pos], "\x1b[m", 3);
  *pos += 3;
}

void fg_colour(char *buf, size_t *pos, int colour)
{
  char s[15];
  sprintf(s, "\x1b[38;5;%dm", colour);

  memcpy(&buf[*pos], s, strlen(s));
  *pos += strlen(s);
}

char snek_head(uint32_t dir)
{
  switch (dir) {
    case NORTH:
      return '^';
    case SOUTH:
      return 'v';
    case EAST:
      return '>';
    default:
      return '<';
  }
}

void render(struct snek *snek, struct game_state *gs)
{
  // build table of things on screen
  int *table = calloc(sizeof(int), MIN_WIN_HEIGHT * MIN_WIN_WIDTH);
  for (int j = 0; j < MIN_WIN_HEIGHT * MIN_WIN_WIDTH; j++) {
    if (gs->items[j] == SNEK_SNACK)
      table[j] = SNEK_SNACK;
  }

  struct pt *p = snek->head;
  int i = i = p->row * MIN_WIN_WIDTH + p->col;
  table[i] = SNEK_HEAD;
  p = p->prev;
  while (p) {
    int i = p->row * MIN_WIN_WIDTH + p->col;
    table[i] = SNEK_BODY;
    p = p->prev;
  }

  clear_screen();
  char buffer[MIN_WIN_HEIGHT * MIN_WIN_WIDTH * 2];
  size_t pos = 0;

  // draw top bar with score
  invert(buffer, &pos);

  memset(&buffer[pos], ' ', 5);
  pos += 5;

  uninvert(buffer, &pos);

  char score[20];
  sprintf(score, " Score: %d ", gs->score);
  size_t score_len = strlen(score);
  memcpy(&buffer[pos], score, score_len);
  pos += score_len;

  invert(buffer, &pos);

  memset(&buffer[pos], ' ', MIN_WIN_WIDTH - score_len - 5);
  pos +=  MIN_WIN_WIDTH - score_len - 5;

  uninvert(buffer, &pos);

  buffer[pos++] = '\r';
  buffer[pos++] = '\n';
  
  for (size_t r = 1; r < MIN_WIN_HEIGHT - 1; r++) {
    invert(buffer, &pos);
    buffer[pos++] = ' ';
    uninvert(buffer, &pos);
    
    for (size_t c = 1; c < MIN_WIN_WIDTH - 1; c++) {
      int i = r * MIN_WIN_WIDTH + c;
      switch (table[i]) {
        case EMPTY:
          buffer[pos++] = ' ';
          break;
        case SNEK_BODY:
          fg_colour(buffer, &pos, GREEN);
          buffer[pos++] = '#';
          break;
        case SNEK_HEAD:
          fg_colour(buffer, &pos, GREEN);
          buffer[pos++] = snek_head(snek->dir);
          break;
        case SNEK_SNACK:
          fg_colour(buffer, &pos, BLUE);
          buffer[pos++] = 'o';
          break;
      }
    }

    invert(buffer, &pos);
    buffer[pos++] = ' ';
    uninvert(buffer, &pos);

    buffer[pos++] = '\r';
    buffer[pos++] = '\n';
  }

  invert(buffer, &pos);
  memset(&buffer[pos], ' ', MIN_WIN_WIDTH);
  pos += MIN_WIN_WIDTH;
  uninvert(buffer, &pos);
  buffer[pos++] = '\r';
  buffer[pos++] = '\n';
  
  write(STDOUT_FILENO, buffer, pos);

  free(table);
}

bool update(struct snek *snek, struct game_state *gs)
{
  int dr = 0, dc = 0;
  switch (snek->dir) {
    case NORTH:
      dr = -1;
      break;
    case SOUTH:
      dr = 1;
      break;
    case EAST:
      dc = 1;
      break;
    case WEST:
      dc = -1;
      break;
  }

  struct pt *n = malloc(sizeof(struct pt));
  n->row = snek->head->row + dr;
  n->col = snek->head->col + dc;
  n->next = NULL;
  n->prev = snek->head;
  snek->head->next = n;
  snek->head = n;

  struct pt *t = snek->tail;
  snek->tail = snek->tail->next;
  snek->tail->prev = NULL;
  free(t);

  size_t i = snek->head->row * MIN_WIN_WIDTH + snek->head->col;
  if (gs->items[i] == SNEK_SNACK) {
    gs->score += 10;
    gs->speed -= 100;
    gs->items[i] = EMPTY;

    // grow the snek by three segments
    for (int j = 0; j < 3; j++) {
      struct pt *new_seg = malloc(sizeof(struct pt));
      new_seg->row = snek->tail->row;
      new_seg->col = snek->tail->col;
      new_seg->prev = NULL;
      new_seg->next = snek->tail;
      snek->tail->prev = new_seg;
      snek->tail = new_seg;
    }
  }

  // check if the snek has hit any part of its body
  struct pt *seg = snek->head->prev;
  while (seg) {
    if (seg->row == snek->head->row && seg->col == snek->head->col) {
      return false;
    }

    seg = seg->prev;
  }
  return true;
}

bool in_bounds(struct snek *snek) 
{
  if (snek->head->row == 0 || snek->head->col == 0)
    return false;

  if (snek->head->row == MIN_WIN_HEIGHT || snek->head->col == MIN_WIN_WIDTH)
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

  srand(time(NULL));

  uint32_t high_score = 0;
  struct game_state gs = { .score = 0, .items = NULL, .speed = 100000 };
  struct snek *snek = snek_init();

  enter_raw_mode();
  hide_cursor();

  title_screen();

  // main game loop
  free(gs.items);
  gs.items = calloc(sizeof(int), MIN_WIN_HEIGHT * MIN_WIN_WIDTH);
  items_init(&gs, snek);

  while (true) {
    char c = get_key();
    
    if (c == 'q')
      break;
    else if (c == 'w') 
      snek->dir = NORTH;
    else if (c == 'a')
      snek->dir = WEST;
    else if (c == 's')
      snek->dir = SOUTH;
    else if (c == 'd')
      snek->dir = EAST;
    
    if (!update(snek, &gs))
      break;
      
    render(snek, &gs);

    if (!in_bounds(snek))
      break;

    usleep(gs.speed);
  }

  free(gs.items);
}