#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#define BUFSIZE 4096


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
    ssize_t len;
    while(len = getline(&line, NULL, fp) != -1) {
        if(len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        editorInsertRow(E.numrows, line, len);
    }
    free(line);
    fclose(fp);
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: kilo <filename>\n");
        return 1;
    }


    initEditor();
    printf("%d, %d\n", E.screenrows, E.screencols);
    editorOpen(argv[1]);
    /*enableRawMode(STDIN_FILENO);

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress(STDIN_FILENO);
    }*/
    return 0;
}
