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
#define LB_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f = 00011111

/*** data ***/
struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
    // Escape command to clear the whole screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // Reposition cursor
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("disableRawMode()::tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("enableRawMode()::tcgetattr");
    }

    atexit(disableRawMode);

    struct termios raw = E.orig_termios; 

    // Turn off control characters and carriage return / new line
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    // Turn off output processing (for \n to \r\n translation)
    raw.c_oflag &= ~(OPOST);
    // Sets character size to 8, just in case
    raw.c_cflag |= (CS8);
    // Turn off echoing, canonical mode, SIGINT/SIGTSTP signals, and implementation-defined input processing
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    // Min number of bytes = 0 for timeout
    raw.c_cc[VMIN] = 0;
    // Time to wait for timeout in 1/10 of a second
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("enableRawMode()::tcsetattr");
    }
}

/* Waits for keypress and returns it */
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("editorReadKey()::read");
        }
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // Command to ask for cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    // Read response from request
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }

        if (buf[i] == 'R') {
            break;
        }

        i++;
    }
    buf[i] = '\0';

    // Check for command sequence
    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // As fallback if system doesn't support ioctl
        // Move to bottom right and count how far you moved to get there
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        printf("rows: %d, cols: %d\r\n", *rows, *cols);
        editorReadKey();


        return 0;
    }
}

/*** append buffer ***/
// dynamic, append-only string

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/
void editorDrawRows(struct abuf *ab) {
    int y;
    int numrows = E.screenrows;
    for (y = 0; y < numrows; y++) {
        // Display welcome message
        if (y == numrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "lb editor -- v%s", LB_VERSION);
            if (welcomelen > E.screencols) {
                welcomelen = E.screencols;
            }
            
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }

            while (padding--) {
                abAppend(ab, " ", 1);
            }

            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1); 
        }

        // Clear to end of line
        abAppend(ab, "\x1b[K", 3);
        // Don't scroll on last line
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    
    // Hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    // Reposition cursor
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // Show cursor
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);

    abFree(&ab);
}

/*** input ***/
void editorProcessKeypresses() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            // Escape command to clear the whole screen
            write(STDOUT_FILENO, "\x1b[2J", 4);
            // Reposition cursor
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        default:
            printf("(%d) %c \r\n", c, c);
            break;
    }
}


/*** init ***/
void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("initEditor::getWindowSize");
    }
}

int main() {
    enableRawMode();
    initEditor();

    // Input loop
    while (1) {
        editorRefreshScreen();
        editorProcessKeypresses();
    }
    return 0;
}
