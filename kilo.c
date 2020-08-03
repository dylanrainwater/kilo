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
    tcgetattr(STDIN_FILENO, &raw);
    
    // Turn off echoing
    raw.c_lflag &= ~(ECHO | ICANON);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    int size = 0;
    // Get user input
    while (read(STDIN_FILENO, &c, 1) && c != 'q') {
        size++;
    }

    printf("size=%d\n", size);
    return 0;
}
