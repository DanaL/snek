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

#define INIT_SKEN_LEN 8

#define EMPTY 0
#define SNEK_HEAD 1
#define SNEK_BODY 2
#define MUSHROOM 3
#define SNEK_SNACK 4
#define WALL 5

#define NORTH 0
#define SOUTH 1
#define EAST 2
#define WEST 3

#define LEFT 0
#define RIGHT 1

#define MIN_WIN_HEIGHT 30
#define MIN_WIN_WIDTH 100

#define GREEN 28
#define PURPLE 99
#define BLUE 33
#define WHITE 15

#define ACCELERATION 500

#define POISON_DURATION 5

char mushroom[] = { 0xe2, 0x99, 0xa3 };

// data structures for storing the snek and game state

struct message {
  uint32_t row;
  char *msg;
  int colour;
};

struct game_state {
  uint32_t score;
  uint32_t acceleration;
  int *items;
  useconds_t speed;
  useconds_t saved_speed;
  bool paused;
  time_t snacks_refreshed;
  time_t mushrooms_refreshed;
  bool poisoned;
  time_t poisoned_time;
  uint32_t last_wall_attempt;
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

// prototypes
void clear_screen(void);
void exit_raw_mode(void);
void hide_cursor(void);
char get_key(void);
void render(struct snek *, struct game_state *, struct message *, size_t, uint32_t);

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

void add_item(struct game_state *gs, struct snek *snek, int item)
{
  // we'll only try so many times to find a place for the time
  for (int j = 0; j < 100; j++) {
    int row = rand() % (MIN_WIN_HEIGHT - 2) + 1;
    int col = rand() % (MIN_WIN_WIDTH - 2) + 1;

    if (row == snek->head->row && col == snek->head->col)
      continue;

    int i = row * MIN_WIN_WIDTH + col;
    if (gs->items[i] != EMPTY)
      continue;

    gs->items[i] = item;
    return;
  }
}

void add_snacks(struct game_state *gs, struct snek *snek, int count)
{
  while (count > 0) {
    add_item(gs, snek, SNEK_SNACK);
    --count;
  }
}

void add_mushrooms(struct game_state *gs, struct snek *snek, int count)
{
  while (count > 0) {
    add_item(gs, snek, MUSHROOM);
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
  struct message *messages = malloc(4 * sizeof(struct message));
  messages[0].msg = "~~ SNEK! 1.0.0 ~~";
  messages[0].row = MIN_WIN_HEIGHT / 3;
  messages[0].colour = WHITE;
  messages[1].msg = "Eat snek snacks! Grow!";
  messages[1].row = (MIN_WIN_HEIGHT / 3) + 2;
  messages[1].colour = WHITE;
  messages[2].msg = "Avoid an ouroboros situation!";
  messages[2].row = (MIN_WIN_HEIGHT / 3) + 3;
  messages[2].colour = WHITE;
  messages[3].msg = "press space to begin...";
  messages[3].row = (MIN_WIN_HEIGHT / 3) + 5;
  messages[3].colour = WHITE;
  
  struct game_state gs = { .score = 0, .items = NULL };

  render(NULL, &gs, messages, 4, 0);
  free(messages);
  
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

  int cols = ws.ws_col;
  int rows = ws.ws_row;
  
  if (cols < MIN_WIN_WIDTH || rows < MIN_WIN_HEIGHT)
    return false;
  
  return true;
}

// terminal i/o stuff

char get_key(void)
{
  char c = '\0';
  if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) 
    die("read");
  
  if (c == '\x1b') {
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

void render(struct snek *snek, struct game_state *gs, struct message *messages, size_t msg_count, uint32_t high_score)
{
  int snek_colour = GREEN;

  // build table of items on screen
  int *table = calloc(sizeof(int), MIN_WIN_HEIGHT * MIN_WIN_WIDTH);
  if (gs->items) {
    for (int j = 0; j < MIN_WIN_HEIGHT * MIN_WIN_WIDTH; j++) {
      if (gs->items[j] != EMPTY)
        table[j] = gs->items[j];
    }

    if (gs->poisoned)
      snek_colour = PURPLE;
  }

  
  if (snek) {
    struct pt *p = snek->head;
    int i = i = p->row * MIN_WIN_WIDTH + p->col;
    table[i] = SNEK_HEAD;
    p = p->prev;
    while (p) {
      int i = p->row * MIN_WIN_WIDTH + p->col;
      table[i] = SNEK_BODY;
      p = p->prev;
    }
  }

  clear_screen();
  char buffer[MIN_WIN_HEIGHT * MIN_WIN_WIDTH * 2];
  size_t pos = 0;

  // draw top bar with score
  invert(buffer, &pos);

  memset(&buffer[pos], ' ', 5);
  pos += 5;

  uninvert(buffer, &pos);

  char score[25];
  sprintf(score, " Score: %d ", gs->score);
  size_t score_len = strlen(score);
  memcpy(&buffer[pos], score, score_len);
  pos += score_len;

  invert(buffer, &pos);

  sprintf(score, " High score: %d ", high_score);
  size_t high_score_len = strlen(score);

  int padding = (MIN_WIN_WIDTH - high_score_len - 5) - (score_len + 5);
  memset(&buffer[pos], ' ', padding);
  pos += padding;

  uninvert(buffer, &pos);

  memcpy(&buffer[pos], score, high_score_len);
  pos += high_score_len;

  invert(buffer, &pos);

  memset(&buffer[pos], ' ', 5);
  pos += 5;

  uninvert(buffer, &pos);

  buffer[pos++] = '\r';
  buffer[pos++] = '\n';
  
  for (size_t r = 1; r < MIN_WIN_HEIGHT - 1; r++) {
    invert(buffer, &pos);
    buffer[pos++] = ' ';
    uninvert(buffer, &pos);
    
    // Wish C had built-in hash tables but I'm not going to bother
    // implementing one just for this...
    struct message *message = NULL;
    size_t msg_col = 0, msg_len = 0;
    for (size_t j = 0; j < msg_count; j++) {
      if (messages[j].row == r) {
        message = &messages[j];
        msg_len = strlen(message->msg);
        msg_col = (MIN_WIN_WIDTH - 2 - msg_len) / 2;
      }
    }

    for (size_t c = 1; c < MIN_WIN_WIDTH - 1; c++) {
      if (message && c >= msg_col && c < msg_col + msg_len) {
        fg_colour(buffer, &pos, message->colour);
        buffer[pos++] = message->msg[c - msg_col];
        continue;
      }
      
      int i = r * MIN_WIN_WIDTH + c;
      switch (table[i]) {
        case EMPTY:
          buffer[pos++] = ' ';
          break;
        case SNEK_BODY:
          fg_colour(buffer, &pos, snek_colour);
          buffer[pos++] = '#';
          break;
        case SNEK_HEAD:
          fg_colour(buffer, &pos, snek_colour);
          buffer[pos++] = snek_head(snek->dir);
          break;
        case SNEK_SNACK:
          fg_colour(buffer, &pos, BLUE);
          buffer[pos++] = 'o';
          break;
        case WALL:
          invert(buffer, &pos);
          buffer[pos++] = ' ';
          uninvert(buffer, &pos);
          break;
        case MUSHROOM:
          fg_colour(buffer, &pos, PURPLE);
          memcpy(&buffer[pos], "\xe2\x99\xa3", 3);
          pos += 3;
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

void try_to_add_barrier(struct snek *snek, struct game_state *gs) 
{
  size_t walls[3];

  // try up to 3 times to add a barrier
  for (int j = 0; j < 3; j++) {
    int row = rand() % (MIN_WIN_HEIGHT - 2) + 1;
    int col = rand() % (MIN_WIN_WIDTH - 2) + 1;
    int i = row * MIN_WIN_WIDTH + col;

    int shape = rand() % 2;
    switch (shape) {
      case 0:
        walls[0] = i - 1;
        walls[1] = i;
        walls[2] = i + 1;
        break;
      case 1:
        walls[0] = i - MIN_WIN_WIDTH;
        walls[1] = i;
        walls[2] = i + MIN_WIN_WIDTH;
        break;
    }
    
    bool valid = true;
    struct pt *seg = snek->head;
    while (seg) {
      int i = seg->row * MIN_WIN_WIDTH + seg->col;
      for (int k = 0; k < 3; k++) {
        if (walls[k] == i) {
          valid = false;
          goto draw_wall;
        }
      }
      seg = seg->prev;
    }
draw_wall:
    if (valid) {
      for (int k = 0; k < 3; k++) {
        gs->items[walls[k]] = WALL;
      }

      return;
    }
  }
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

  if (gs->poisoned && time(NULL) - gs->poisoned_time >= POISON_DURATION) {
    gs->poisoned = false;
    gs->speed = gs->saved_speed;
    gs->saved_speed = 0;
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
    gs->speed -= 1000;
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
  else if (gs->items[i] == MUSHROOM) {
    gs->score += 75;
    if (gs->saved_speed == 0) {
      gs->saved_speed = gs->speed;
    }
    gs->speed /= 2;
    gs->items[i] = EMPTY;
    gs->poisoned = true;
    gs->poisoned_time = time(NULL);
  }
  else if (gs->items[i] == WALL) {
    return true;
  }

  // check if the snek has hit any part of its body
  struct pt *seg = snek->head->prev;
  while (seg) {
    if (seg->row == snek->head->row && seg->col == snek->head->col) {
      return true;
    }

    seg = seg->prev;
  }

  // should we try to add a barrier?
  if (gs->score >= 500 && gs->score - gs->last_wall_attempt >= 100) {
    try_to_add_barrier(snek, gs);
    gs->last_wall_attempt = gs->score;
  }

  return false;
}

bool in_bounds(struct snek *snek) 
{
  if (snek->head->row == 0 || snek->head->col == 0)
    return false;

  if (snek->head->row >= MIN_WIN_HEIGHT - 1 || snek->head->col >= MIN_WIN_WIDTH - 1)
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
  enter_raw_mode();
  hide_cursor();
  
  uint32_t high_score = 0;
  struct snek *snek = NULL;

  title_screen();

	bool playing = true;
	do {
    struct game_state gs = { .score = 0, .items = NULL, .speed = 100000,
                                .paused = false, .poisoned = false,
                                .last_wall_attempt = 0, .saved_speed = 0 };
		free(snek);
  	snek = snek_init();
		free(gs.items);
		gs.items = calloc(sizeof(int), MIN_WIN_HEIGHT * MIN_WIN_WIDTH);
		add_snacks(&gs, snek, 20);

		bool game_over = false;
		gs.snacks_refreshed = time(NULL);
		gs.mushrooms_refreshed = time(NULL);

    try_to_add_barrier(snek, &gs);

		// main game loop	
		while (true) {
			char c = get_key();    
			if (c == 'w') 
				snek->dir = NORTH;
			else if (c == 'a')
				snek->dir = WEST;
			else if (c == 's')
				snek->dir = SOUTH;
			else if (c == 'd')
				snek->dir = EAST;
			else if (c == ' ')
				gs.paused = !gs.paused;
		
			if (!gs.paused) {
				game_over = update(snek, &gs);

				if (!in_bounds(snek))
					game_over = true;

				if (game_over) {
          bool new_high_score = false;
          if (gs.score > high_score) {
            new_high_score = true;
            high_score = gs.score;
          }
 
          size_t num_msgs = new_high_score ? 3 : 2;
          struct message *msg = malloc(num_msgs * sizeof(struct message));
          int i = 0, row = MIN_WIN_HEIGHT / 3;
          msg[i].msg = "Oh noes! Game over :(";
          msg[i].colour = PURPLE;
          msg[i].row = row;

          if (new_high_score) {
            ++i;
            row += 2;
            msg[i].msg = "A new high score!!";
            msg[i].colour = BLUE;
            msg[i].row = row;
          }

          ++i;
          row += 2;
          msg[i].msg = "Press space to play again or q to quit";
          msg[i].colour = WHITE;
          msg[i].row = row;
          render(snek, &gs, msg, num_msgs, high_score);
          free(msg);
					break;
				}

				if (time(NULL) - gs.snacks_refreshed >= 10) {
					add_snacks(&gs, snek, 5);
					gs.snacks_refreshed = time(NULL);
				}

        if(gs.score > 200 && time(NULL) - gs.mushrooms_refreshed >= 15) {
          add_mushrooms(&gs, snek, 2);
          gs.mushrooms_refreshed = time(NULL);
        }

				render(snek, &gs, NULL, 0, high_score);
			}

  		usleep(gs.speed);
  	}

		while (true) {
			char c = get_key();
			if (c == 'q') {
				playing = false;
        free(gs.items);
        free(snek);
        clear_screen();
				break;
			}
			else if (c == ' ') {
				break;
			}	
		}
	}
	while (playing);
}