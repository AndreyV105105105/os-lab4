#ifndef IPC_H
#define IPC_H

#include <windows.h>

#define PACKET_LINE  1
#define PACKET_DONE  2

typedef struct {
    DWORD kind;
    DWORD length;
    char text[256];
} Packet;

typedef struct {
    HANDLE file;
    HANDLE mapping;
    HANDLE ready;
    HANDLE consumed;
    HANDLE stop;
    Packet *packet;
} Ipc;

int ipc_create(Ipc *ipc, const char *prefix);
int ipc_open(Ipc *ipc, const char *prefix);
int ipc_close(Ipc *ipc);

#endif
