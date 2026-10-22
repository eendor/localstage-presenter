/* Build helper: turn a file into a C byte array. Usage: embed input output.c [symbol] */
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) { fprintf(stderr, "usage: embed input output.c [symbol]\n"); return 2; }
    const char *sym = argc == 4 ? argv[3] : "web_index";
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "w");
    if (!in || !out) { perror("embed"); return 1; }
    fprintf(out, "/* Generated from %s. Do not edit. */\nconst unsigned char %s[] = {", argv[1], sym);
    unsigned long n = 0;
    int ch;
    while ((ch = fgetc(in)) != EOF) fprintf(out, "%s%d,", n++ % 24 ? "" : "\n", ch);
    fprintf(out, "0};\nconst unsigned long %s_len = %luUL;\n", sym, n);
    fclose(in);
    return fclose(out) ? 1 : 0;
}
