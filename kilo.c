#include <stdio.h>

#define BUFSIZE 4096


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: kilo <filename>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    char buf[BUFSIZE];
    fread(buf, sizeof(char), BUFSIZE, f);
    while(1) {
        int cnt = 0;
        scanf("%d", &cnt);
        printf("\e[2J");   //clear
        printf("\e[1;1H"); //move cursor to [1,1]
        printf("\e[?25l"); //hide cursor
        puts(buf);
        printf("\e[1;1H"); 
        printf("%d\n", cnt);
        printf("\e[?25h"); //print cursor
    }
    return 0;
}
