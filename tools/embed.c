/* Build helper: turn a file into a C byte array. Usage: embed input.html output.c */
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: embed input output.c\n"); return 2; }
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "w");
    if (!in || !out) { perror("embed"); return 1; }
    fprintf(out, "/* Generated from %s. Do not edit. */\nconst unsigned char web_index[] = {", argv[1]);
    unsigned long n = 0;
    int ch;
    while ((ch = fgetc(in)) != EOF) fprintf(out, "%s%d,", n++ % 24 ? "" : "\n", ch);
    fprintf(out, "0};\nconst unsigned long web_index_len = %luUL;\n", n);
    fclose(in);
    return fclose(out) ? 1 : 0;
}
