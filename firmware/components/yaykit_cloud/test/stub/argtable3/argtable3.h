// Host tests only (test_account.c): the console is linked there, not used.
#pragma once
#include <stdio.h>
struct arg_str {
    int count;
    const char **sval;
};
struct arg_end {
    int count;
};
struct arg_str *arg_str0(const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
struct arg_end *arg_end(int maxerrors);
int arg_parse(int argc, char **argv, void **argtable);
void arg_print_errors(FILE *fp, struct arg_end *end, const char *progname);
