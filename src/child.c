#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include "common.h"
#include "ipc.h"

#define TOKEN_MAX 64

typedef struct {
    HANDLE h;
    char buf[BUF_SIZE];
    DWORD pos;
    DWORD len;
} Reader;

typedef struct {
    float sum;
    int has_value;
    int error;
    char token[TOKEN_MAX + 1];
    int tlen;
    int too_long;
    int has_nul;
} LineState;

static int reader_getc(Reader *r, char *c)
{
    if (r->h == NULL) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            return ferror(stdin) ? -1 : 0;
        }
        *c = (char)ch;
        return 1;
    }
    if (r->pos == r->len) {
        if (!ReadFile(r->h, r->buf, sizeof(r->buf), &r->len, NULL)) {
            return -1;
        }
        r->pos = 0;
        if (r->len == 0) {
            return 0;
        }
    }
    *c = r->buf[r->pos++];
    return 1;
}

static void line_reset(LineState *st)
{
    st->sum = 0.0f;
    st->has_value = 0;
    st->error = 0;
    st->tlen = 0;
    st->too_long = 0;
    st->has_nul = 0;
}

static void finish_token(LineState *st, unsigned long long line_no)
{
    char *end;
    float value;

    if (st->tlen == 0 && !st->too_long && !st->has_nul) {
        return;
    }
    st->token[st->tlen] = '\0';
    if (st->too_long) {
        fprintf(stderr, "[child PID %lu] line %llu: number is too long\n",
                (unsigned long)GetCurrentProcessId(), line_no);
        st->error = 1;
    } else {
        value = strtof(st->token, &end);
        if (st->has_nul || end == st->token || *end != '\0' || !isfinite(value)) {
            fprintf(stderr, "[child PID %lu] line %llu: invalid number '%s'\n",
                    (unsigned long)GetCurrentProcessId(), line_no, st->token);
            st->error = 1;
        } else {
            st->sum += value;
            st->has_value = 1;
        }
    }
    st->tlen = 0;
    st->too_long = 0;
    st->has_nul = 0;
}

static int publish(Ipc *ipc, HANDLE parent, const Packet *packet)
{
    HANDLE waits[] = {ipc->stop, parent, ipc->consumed};
    *ipc->packet = *packet;
    if (!SetEvent(ipc->ready)) {
        print_error("SetEvent ready");
        return -1;
    }
    DWORD wait = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0 + 2) {
        return 0;
    }
    if (wait == WAIT_FAILED) {
        print_error("WaitForMultipleObjects child");
    } else {
        fprintf(stderr, "[child] ERROR: parent stopped or exited\n");
    }
    return -1;
}

static int finish_line(LineState *st, Ipc *ipc, HANDLE parent, unsigned long long line_no)
{
    Packet packet = {0};
    unsigned long pid = (unsigned long)GetCurrentProcessId();
    int len;

    if (!st->has_value && !st->error) {
        line_reset(st);
        return 0;
    }
    if (st->error) {
        len = snprintf(packet.text, sizeof(packet.text),
                       "[child PID %lu] line %llu: ERROR (invalid input)\n", pid, line_no);
    } else if (!isfinite(st->sum)) {
        len = snprintf(packet.text, sizeof(packet.text),
                       "[child PID %lu] line %llu: ERROR (float overflow)\n", pid, line_no);
    } else {
        len = snprintf(packet.text, sizeof(packet.text),
                       "[child PID %lu] line %llu: sum = %g\n", pid, line_no, (double)st->sum);
    }
    line_reset(st);
    if (len < 0 || (size_t)len >= sizeof(packet.text)) {
        fprintf(stderr, "[child] ERROR: cannot format result\n");
        return -1;
    }
    if (ipc == NULL) {
        if (fputs(packet.text, stdout) == EOF || fflush(stdout) == EOF) {
            perror("stdout");
            return -1;
        }
        return 0;
    }
    packet.kind = PACKET_LINE;
    packet.length = (DWORD)len;
    return publish(ipc, parent, &packet);
}

int main(int argc, char *argv[])
{
    Ipc ipc = {0};
    HANDLE parent = NULL;
    Reader in = {0};
    LineState st;
    unsigned long long line_no = 1;
    Packet done = {0};
    Ipc *channel = NULL;
    int result = EXIT_FAILURE, r;
    char c;

    if (argc != 1 && argc != 3) {
        fprintf(stderr, "Usage: child.exe [IPC_PREFIX PARENT_PID]\n");
        return EXIT_FAILURE;
    }
    if (argc == 3) {
        char *end;
        errno = 0;
        unsigned long pid = strtoul(argv[2], &end, 10);
        if (argv[2][0] < '0' || argv[2][0] > '9' || *end != '\0' || errno == ERANGE || pid == 0) {
            fprintf(stderr, "[child] ERROR: invalid parent PID\n");
            return EXIT_FAILURE;
        }
        parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (parent == NULL) {
            print_error("OpenProcess parent");
            goto cleanup;
        }
        if (ipc_open(&ipc, argv[1]) != 0) {
            goto cleanup;
        }
        channel = &ipc;
        fprintf(stderr, "[child PID %lu] parent PID %lu, mapped file opened\n",
                (unsigned long)GetCurrentProcessId(), pid);

        in.h = GetStdHandle(STD_INPUT_HANDLE);
        if (in.h == INVALID_HANDLE_VALUE || in.h == NULL) {
            fprintf(stderr, "[child] ERROR: standard input is unavailable\n");
            goto cleanup;
        }
    }
    line_reset(&st);
    while ((r = reader_getc(&in, &c)) == 1) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            finish_token(&st, line_no);
            if (c == '\n') {
                if (finish_line(&st, channel, parent, line_no) != 0) {
                    goto cleanup;
                }
                line_no++;
            }
        } else if (c == '\0') {
            st.has_nul = 1;
        } else if (st.tlen < TOKEN_MAX) {
            st.token[st.tlen++] = c;
        } else {
            st.too_long = 1;
        }
    }
    if (r < 0) {
        if (channel == NULL) {
            perror("stdin");
        } else {
            print_error("ReadFile(stdin)");
        }
        goto cleanup;
    }

    finish_token(&st, line_no);
    if (finish_line(&st, channel, parent, line_no) != 0) {
        goto cleanup;
    }
    done.kind = PACKET_DONE;
    if (channel == NULL || publish(channel, parent, &done) == 0) {
        result = EXIT_SUCCESS;
    }

cleanup:
    if (ipc_close(&ipc) != 0) result = EXIT_FAILURE;
    if (close_handle(&parent) != 0) result = EXIT_FAILURE;
    return result;
}
