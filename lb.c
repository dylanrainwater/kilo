/*** includes ***/

// Feature test macros to make a little more portable
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define LB_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f = 00011111

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY
};

/*** data ***/
typedef struct editor_row {
    int length;
    char *chars;
} erow;

struct editorConfig {
    int cursor_x, cursor_y;
    int row_offset;
    int screen_rows;
    int col_offset;
    int screen_cols;
    int num_rows;
    erow *rows;
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
    raw.c_cc[VTIME] = 10;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("enableRawMode()::tcsetattr");
    }
}

/* Waits for keypress and returns it */
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("editorReadKey()::read");
        }
    }

    // Check for command sequence
    if (c == '\x1b') {
        char seq[3];
        
        // Assume <esc> if nothing after initial seq
        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        
        if (seq[0] == '[') {
            // Check for quick jump commands
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                // Check for arrow keys
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        
        return '\x1b';
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

        return 0;
    }
}

/*** row operations ***/

void editorAppendRow(char *s, size_t len) {
    E.rows = realloc(E.rows, sizeof(erow) * (E.num_rows + 1));

    int r = E.num_rows;
    E.rows[r].length = len;
    E.rows[r].chars = malloc(len + 1);
    memcpy(E.rows[r].chars, s, len);
    E.rows[r].chars[len - 1] = '\0';

    E.num_rows++;
}

/*** file I/O ***/

void openEditor(char *filename) {
    FILE *fp  = fopen(filename, "r");
    if (!fp) {
        die("openEditor::fopen");
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len = 0;
    while((line_len = getline(&line, &line_cap, fp)) != -1) {
        // Strip off newline or carriage returns
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) line_len--;

        editorAppendRow(line, line_len + 1);
    }

    free(line);
    fclose(fp);
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

void editorScroll() {
    /*** Vertical Scrolling ***/
    // Scroll above window if necessary
    if (E.cursor_y < E.row_offset) {
        E.row_offset = E.cursor_y;
    }

    // Scroll to bottom if necessary
    if (E.cursor_y >= E.row_offset + E.screen_rows) {
        E.row_offset = E.cursor_y - E.screen_rows + 1;
    }

    /*** Horizontal Scrolling ***/
    if (E.cursor_x < E.col_offset) {
        E.col_offset = E.cursor_x;
    }

    if (E.cursor_x >= E.col_offset + E.screen_cols) {
        E.col_offset = E.cursor_x + E.screen_cols + 1;
    }

}

/*
 * Works by appending message to ab using calls to abAppend();
 */
void editorDrawRows(struct abuf *ab) {
    int y;
    int screen_rows = E.screen_rows;

    for (y = 0; y < screen_rows; y++) {
        int file_row = y + E.row_offset;
        
        if (file_row >= E.num_rows) {
            // Display welcome message
            if (E.num_rows == 0 && y == screen_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "lb editor -- v%s", LB_VERSION);
                if (welcomelen > E.screen_cols) {
                    welcomelen = E.screen_cols;
                }
                
                int padding = (E.screen_cols - welcomelen) / 2;
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
        } else {
            int len = E.rows[file_row].length - E.col_offset;
            
            if (len < 0) {
                len = 0;
            }

            if (len > E.screen_cols) {
                len = E.screen_cols;
            }
            abAppend(ab, &E.rows[file_row].chars[E.col_offset], len);
        }
        // Clear to end of line
        abAppend(ab, "\x1b[K", 3);
        // Don't scroll on last line
        if (y < E.screen_rows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;
    
    // Hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    // Reposition cursor
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cursor_y - E.row_offset) + 1, (E.cursor_x - E.col_offset) + 1);
    abAppend(&ab, buf, strlen(buf));

    // Show cursor
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);

    abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
    erow *row = (E.cursor_y >= E.num_rows) ? NULL : &E.rows[E.cursor_y];

    switch (key) {
        case ARROW_LEFT:
            if (E.cursor_x != 0) {
                E.cursor_x--;
            } else if (E.cursor_y > 0) {
                E.cursor_y--;
                E.cursor_x = E.rows[E.cursor_y].length;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cursor_x < row->length) {
                E.cursor_x++;
            } else if (row && E.cursor_x == row->length) {
                E.cursor_x = 0;
                E.cursor_y++;
            }
            break;
        case ARROW_UP:
            if (E.cursor_y != 0) {
                E.cursor_y--;
            }
            break;
        case ARROW_DOWN:
            if (E.cursor_y < E.num_rows) {
                E.cursor_y++;
            }
            break;
    }

    // Account for row lengths being different
    row = (E.cursor_y >= E.num_rows) ? NULL : &E.rows[E.cursor_y];

    int row_len = row ? row->length : 0;
    if (E.cursor_x > row_len) {
        E.cursor_x = row_len;
    }
}

void editorProcessKeypresses() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            // Escape command to clear the whole screen
            write(STDOUT_FILENO, "\x1b[2J", 4);
            // Reposition cursor
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screen_rows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case HOME_KEY:
            E.cursor_x = 0;
            break;
        case END_KEY:
            E.cursor_x = E.screen_cols - 1;
            break;

        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/
void initEditor() {
    E.cursor_x = 0;
    E.cursor_y = 0;
    E.num_rows = 0;
    E.rows = NULL;
    E.row_offset = 0;
    E.col_offset = 0;

    if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1) {
        die("initEditor::getWindowSize");
    }
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        openEditor(argv[1]);
    }

    // Input loop
    while (1) {
        editorRefreshScreen();
        editorProcessKeypresses();
    }
    return 0;
}
