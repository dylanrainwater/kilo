#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void die(const char *s) {
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
    // Sets character sie to 8, just in case
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

int main() {
    enableRawMode();

    // Input loop
    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
            die("main()::read");
        }

        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }

        if (c == 'q') break;
    }
    return 0;
}
