#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
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

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    // Input loop
    while (1) {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);

        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }

        if (c == 'q') break;
    }
    return 0;
}
