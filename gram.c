/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define GRAM_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey
{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	PAGE_UP,
	PAGE_DOWN
};

/*** data ***/

struct editorConfig
{
	int cx, cy;
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
}

void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	// tcgetattr(STDIN_FILENO, &raw);
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	// iflag input flags
	// oflag output flag
	// lflag local flags
	// Echo for cleaning the thing you write in terminal
	// icanon for disable canoniacal mode i.e to be able to read byte to byte else than line to line
	// isig to catch input signals like ctrl c and ctrl z
	// iexten for making ctrl v i.e charachter gets submitted after next character
	// ixon for catching input transmission signals i.e ctrl c and ctrl q i.e suspending and recontinueing
	// icrnl cr stands for carriage return nl stands for new line i.e making ctrl m work form 10 to 13
	// opost turning of ouput processing .... the cusor move the new line but wont change its place i.e will move a new line from the same place it ended the first line means blocking the carriage return
	// CS8 krne se we bitmasked our input to 8 bits

	// c_cc control charachters VMIN the number of bites minimum to be read befor initiating read()
	//	VTIME time duration to wait for accepting the charachter
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

int editorReadKey()
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}

	if (c == '\x1b')
	{
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';

		if (seq[0] == '[')
		{
			if (seq[1] >= '0' && seq[1] <= '9')
			{
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~')
				{
					switch (seq[1])
					{
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					}
				}
			}
			else
			{

				switch (seq[1])
				{
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				}
			}
		}
		return '\x1b';
	}
	else
	{
		return c;
	}
}

int getCursorPosition(int *rows, int *cols)
{

	char buf[32];
	unsigned int i = 0;
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	//[6n [6 tells us cursor ki position and n is for device
	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;

	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return getCursorPosition(rows, cols);
	}
	else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** append buffer ***/

// dynamic string kind of thing ... so we dont have to render write() again & again
struct abuf
{
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL)
		return;

	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++)
	{
		if (y == E.screenrows / 3)
		{
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome), "Gram editor --version %s", GRAM_VERSION);
			if (welcomelen > E.screencols)
				welcomelen = E.screencols;
			int padding = (E.screencols - welcomelen) / 2;
			if (padding)
			{
				abAppend(ab, "~", 1);
				padding--;
			}
			while (padding--)
				abAppend(ab, " ", 1);
			abAppend(ab, welcome, welcomelen);
		}
		else
		{
			abAppend(ab, "~", 1);
		}
		abAppend(ab, "\x1b[K", 3);
		// now refreshing each line than the page itself
		if (y < E.screenrows - 1)
		{
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen()
{
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	// abAppend(&ab , "\x1b[2J" , 4);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[H", 3);
	abAppend(&ab, "\x1b[?25h", 6);
	// For stoping cursor flicker [?25h , l i.e reset set is used
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key)
{
	switch (key)
	{
	case ARROW_LEFT:
		if (E.cx != 0)
		{
			E.cx--;
		}
		break;
	case ARROW_RIGHT:
		if (E.cx != E.screencols - 1)
		{
			E.cx++;
		}
		break;
	case ARROW_UP:
		if (E.cy != 0)
		{
			E.cy--;
		}
		break;
	case ARROW_DOWN:
		if (E.cy != E.screenrows - 1)
		{
			E.cy++;
		}
		break;
	}
}

void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;
	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;
	}
}

/*** init ***/

void initEditor()
{
	E.cx = 0;
	E.cy = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

int main()
{
	enableRawMode();
	initEditor();

	while (1)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}