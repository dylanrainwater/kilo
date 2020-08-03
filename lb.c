/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f = 00011111

/*** data ***/
struct termios orig_termios;

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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("disableRawMode()::tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("enableRawMode()::tcgetattr");
    }

    atexit(disableRawMode);

    struct termios raw = orig_termios; 

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

/*** output ***/
void editorRefreshScreen() {
    // Escape command to clear the whole screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // Reposition cursor
    write(STDOUT_FILENO, "\x1b[H", 3);
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
int main() {
    enableRawMode();

    // Input loop
    while (1) {
        editorRefreshScreen();
        editorProcessKeypresses();
    }
    return 0;
}
