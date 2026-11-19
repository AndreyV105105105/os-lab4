#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <windows.h>

#define BUF_SIZE 4096

void print_error(const char *context);
BOOL write_all(HANDLE h, const char *data, DWORD len);
int close_handle(HANDLE *handle);
int read_line(FILE *file, char **line, size_t *capacity);

#endif
