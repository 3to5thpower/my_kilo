#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <termios.h>

#define BUFSIZE 4096

struct abuf {
    char *buf;
    int len;
};
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->buf, ab->len + len);
    if(new == NULL) return;
    memcpy(new+ab->len, s, len);
    ab->buf = new;
    ab->len += len;
}
void abFree(struct abuf *ab) {
    free(ab->buf);
}

/* This structure represents a single line of the file we are editing. */
typedef struct erow {
    int idx;            /* Row index in the file, zero-based. */
    int size;           /* Size of the row, excluding the null term. */
    char *chars;        /* Row content. */
} erow;

struct editorConfig {
    int cx,cy;  /* Cursor x and y position in characters */
    int rowoff;     /* Offset of row displayed. */
    int coloff;     /* Offset of column displayed. */
    int screenrows; /* Number of rows that we can show */
    int screencols; /* Number of cols that we can show */
    int numrows;    /* Number of rows */
    int rawmode;    /* Is terminal raw mode enabled? */
    erow *row;      /* Rows */
    char *filename; /* Currently open filename */
    //char statusmsg[80];
};

static struct editorConfig E;

int getWindowSize(int ifd, int ofd, int *rows, int *cols) {
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
    return -1;
}
void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    if (getWindowSize(STDIN_FILENO,STDOUT_FILENO,
                      &E.screenrows,&E.screencols) == -1)
    {
        perror("Unable to query the screen for size (columns / rows)");
        exit(1);
    }
}

void editorInsertRow(int at, char *s, size_t len) {
    if(at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow)*(E.numrows+1));
    if(at < E.numrows) {
        memmove(E.row+at, E.row+at, sizeof(E.row[0])*(E.numrows-at));
        for(int i = at+1; i <= E.numrows; i++) E.row[i].idx++;
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len + 1);
    E.row[at].idx = at;
    E.numrows++;
}

int editorOpen(char *filename) {
    FILE *fp;
    free(E.filename);
    E.filename = strdup(filename);

    fp = fopen(filename, "r");
    if(!fp) {
        if(errno != ENOENT) {
            perror("Opening file");
            exit(1);
        }
        return 1;
    }
    char *line = NULL;
    size_t cap=0;
    ssize_t len;
    while((len = getline(&line, &cap, fp)) != -1) {
        if(len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        editorInsertRow(E.numrows, line, len);
    }
    free(line);
    fclose(fp);
    return 0;
}

void editorRefreshScreen(void) {
    struct abuf ab = {NULL, 0};
    abAppend(&ab, "\e[?25l", 6); //hide cursor
    abAppend(&ab, "\e[H", 3); //go home
    for(int y = 0; y < E.screenrows; y++) {
        int filerow = E.rowoff + y;
        if(filerow >= E.numrows) {
            abAppend(&ab, "~\e[0K\r\n", 7);
            continue;
        }
        erow *r = &E.row[filerow];
        int len = r->size - E.coloff;
        if(len > 0) {
            if(len > E.screencols) len = E.screencols;
            char *c = r->chars + E.coloff;
            for(int j = 0; j < len; j++) abAppend(&ab, c+j, 1);
        }
        abAppend(&ab, "\e[39m", 5);
        abAppend(&ab, "\e[0K", 4);
        abAppend(&ab, "\r\n", 2);
    }
    char buf[30];
    snprintf(buf, sizeof(buf), "\e[%d;%dH", E.cy+1, E.cx);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\e[?25h", 6);
    write(STDOUT_FILENO, ab.buf, ab.len);
    abFree(&ab);
}

static struct termios orig_termios; /* In order to restore at exit.*/

void disableRawMode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (E.rawmode) {
        tcsetattr(fd,TCSAFLUSH,&orig_termios);
        E.rawmode = 0;
    }
}

/* Called at exit to avoid remaining in raw mode. */
void editorAtExit(void) {
    disableRawMode(STDIN_FILENO);
}

/* Raw mode: 1960 magic shit. */
int enableRawMode(int fd) {
    struct termios raw;

    if (E.rawmode) return 0; /* Already enabled. */
    if (!isatty(STDIN_FILENO)) goto fatal;
    atexit(editorAtExit);
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer. */
    raw.c_cc[VMIN] = 0; /* Return each byte, or zero for timeout. */
    raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    E.rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

enum KEY_ACTION{
        KEY_NULL = 0,       /* NULL */
        CTRL_C = 3,         /* Ctrl-c */
        CTRL_D = 4,         /* Ctrl-d */
        CTRL_F = 6,         /* Ctrl-f */
        CTRL_H = 8,         /* Ctrl-h */
        TAB = 9,            /* Tab */
        CTRL_L = 12,        /* Ctrl+l */
        ENTER = 13,         /* Enter */
        CTRL_Q = 17,        /* Ctrl-q */
        CTRL_S = 19,        /* Ctrl-s */
        CTRL_U = 21,        /* Ctrl-u */
        ESC = 27,           /* Escape */
        BACKSPACE =  127,   /* Backspace */
        /* The following are just soft codes, not really reported by the
         * terminal directly. */
        ARROW_LEFT = 1000,
        ARROW_RIGHT,
        ARROW_UP,
        ARROW_DOWN,
        DEL_KEY,
        HOME_KEY,
        END_KEY,
        PAGE_UP,
        PAGE_DOWN
};

int editorReadKey(int fd) {
    int nread;
    char c, seq[3];
    while((nread = read(fd, &c, 1)) == 0);
    if(nread == -1) exit(1);
    while(1) {
        switch(c) {
            case ESC:
                if(read(fd, seq, 1) == 0) return ESC;
                if(read(fd, seq + 1, 1) == 0) return ESC;
                if(seq[0] == '[') {
                    if('0' <= seq[1] && seq[1] <= '9') {
                        if(read(fd, seq+2, 1) == 0) return ESC;
                        if(seq[2] == '~') {
                            switch(seq[1]) {
                                case '3': return DEL_KEY;
                                case '5': return PAGE_UP;
                                case '6': return PAGE_DOWN;
                            }
                        }
                    } else {
                        switch(seq[1]) {
                            case 'A': return ARROW_UP;
                            case 'B': return ARROW_DOWN;
                            case 'C': return ARROW_RIGHT;
                            case 'D': return ARROW_LEFT;
                            case 'H': return HOME_KEY;
                            case 'F': return END_KEY;
                        }
                    }
                } else if(seq[0] == 'o') {
                    switch(seq[1]) {
                        case 'H': return HOME_KEY;
                        case 'F': return END_KEY;
                    }
                }
                break;
            default:
                return c;
        }
    }
}


void editorProcessKeypress(int fd) {
    int c = editorReadKey(fd);
    printf("%d\n",c);
    if(c == '0') exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: kilo <filename>\n");
        return 1;
    }


    initEditor();
    editorOpen(argv[1]);
    enableRawMode(STDIN_FILENO);

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress(STDIN_FILENO);
    }
    return 0;
}
