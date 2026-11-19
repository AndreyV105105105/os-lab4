#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "common.h"

void print_error(const char *context){
    DWORD code = GetLastError();
    wchar_t wmsg[512];
    char msg[1024] = "unknown error";
    DWORD wlen = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, code, 0, wmsg, 512, NULL);
    if (wlen > 0){
        UINT cp = GetConsoleOutputCP();
        if (cp == 0){
            cp = CP_ACP;
        }
        int n = WideCharToMultiByte(cp, 0, wmsg, (int)wlen, msg, (int)sizeof(msg) - 1, NULL, NULL);
        if (n > 0){
            msg[n] = '\0';
            while(n > 0 && (msg[n - 1] == '\r' || msg[n - 1] == '\n' || msg[n - 1] == ' ')){
                msg[--n] = '\0';
            }
        }
        else{
            snprintf(msg, sizeof(msg), "unknown error");
        }
    }
    fprintf(stderr, "[PID %lu] ERROR: %s: %s (code %lu)\n",
            (unsigned long)GetCurrentProcessId(), context, msg, (unsigned long)code);
}

BOOL write_all(HANDLE h, const char *data, DWORD len){
    while(len > 0){
        DWORD written = 0;
        if (!WriteFile(h, data, len, &written, NULL)){
            return FALSE;
        }
        if (written == 0){
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
        data += written;
        len -= written;
    }
    return TRUE;
}

int close_handle(HANDLE *handle){
    if (*handle != NULL && *handle != INVALID_HANDLE_VALUE){
        if (!CloseHandle(*handle)){
            print_error("CloseHandle");
            return -1;
        }
        *handle = NULL;
    }
    return 0;
}

int read_line(FILE *file, char **line, size_t *capacity){
    size_t length = 0;
    int c;
    while((c = fgetc(file)) != EOF){
        if (c == '\0'){
            fprintf(stderr, "ERROR: NUL byte in text input\n");
            return -1;
        }
        if (length + 1 >= *capacity){
            if (*capacity > SIZE_MAX / 2){
                fprintf(stderr, "ERROR: line is too long\n");
                return -1;
            }
            size_t next = *capacity == 0 ? 256 : *capacity * 2;
            char *p = realloc(*line, next);
            if (p == NULL){
                fprintf(stderr, "ERROR: not enough memory for input line\n");
                return -1;
            }
            *line = p;
            *capacity = next;
        }
        if (c == '\n'){
            break;
        }
        (*line)[length++] = (char)c;
    }
    if (ferror(file)){
        perror("read input");
        return -1;
    }
    if (c == EOF && length == 0){
        return 0;
    }
    (*line)[length] = '\0';
    return 1;
}
