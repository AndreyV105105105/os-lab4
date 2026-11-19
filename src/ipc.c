#include <stdio.h>
#include "ipc.h"
#include "common.h"

static int object_name(char *name, size_t size, const char *prefix, const char *suffix){
    int n = snprintf(name, size, "%s_%s", prefix, suffix);
    if (n < 0 || (size_t)n >= size){
        fprintf(stderr, "ERROR: IPC name is too long\n");
        return -1;
    }
    return 0;
}

static HANDLE event_handle(const char *prefix, const char *suffix, int create){
    char name[160];
    if (object_name(name, sizeof(name), prefix, suffix) != 0){
        return NULL;
    }
    HANDLE event;
    if (create){
        event = CreateEventA(NULL, FALSE, FALSE, name);
        if (event != NULL && GetLastError() == ERROR_ALREADY_EXISTS){
            fprintf(stderr, "ERROR: IPC event already exists\n");
            close_handle(&event);
            return NULL;
        }
    }
    else{
        event = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    }
    if (event == NULL){
        print_error(create ? "CreateEventA" : "OpenEventA");
    }
    return event;
}

int ipc_create(Ipc *ipc, const char *prefix){
    char directory[MAX_PATH + 1], path[MAX_PATH + 1], name[160];
    DWORD length = GetTempPathA(sizeof(directory), directory);
    if (length == 0 || length >= sizeof(directory)){
        fprintf(stderr, "ERROR: cannot get temporary directory\n");
        return -1;
    }
    if (!GetTempFileNameA(directory, "os4", 0, path)){
        print_error("GetTempFileNameA");
        return -1;
    }
    ipc->file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (ipc->file == INVALID_HANDLE_VALUE){
        print_error("CreateFileA temporary file");
        if (!DeleteFileA(path)){
            print_error("DeleteFileA temporary file");
        }
        return -1;
    }
    if (object_name(name, sizeof(name), prefix, "mapping") != 0){
        return -1;
    }
    ipc->mapping = CreateFileMappingA(ipc->file, NULL, PAGE_READWRITE, 0, sizeof(Packet), name);
    if (ipc->mapping == NULL){
        print_error("CreateFileMappingA");
        return -1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS){
        fprintf(stderr, "ERROR: mapping already exists\n");
        return -1;
    }
    ipc->packet = MapViewOfFile(ipc->mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Packet));
    if (ipc->packet == NULL){
        print_error("MapViewOfFile");
        return -1;
    }
    ipc->ready = event_handle(prefix, "ready", 1);
    ipc->consumed = event_handle(prefix, "consumed", 1);
    ipc->stop = event_handle(prefix, "stop", 1);
    return ipc->ready != NULL && ipc->consumed != NULL && ipc->stop != NULL ? 0 : -1;
}

int ipc_open(Ipc *ipc, const char *prefix){
    char name[160];
    if (object_name(name, sizeof(name), prefix, "mapping") != 0){
        return -1;
    }
    ipc->mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (ipc->mapping == NULL){
        print_error("OpenFileMappingA");
        return -1;
    }
    ipc->packet = MapViewOfFile(ipc->mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Packet));
    if (ipc->packet == NULL){
        print_error("MapViewOfFile");
        return -1;
    }
    ipc->ready = event_handle(prefix, "ready", 0);
    ipc->consumed = event_handle(prefix, "consumed", 0);
    ipc->stop = event_handle(prefix, "stop", 0);
    return ipc->ready != NULL && ipc->consumed != NULL && ipc->stop != NULL ? 0 : -1;
}

int ipc_close(Ipc *ipc){
    int result = 0;
    if (ipc->packet != NULL){
        if (!UnmapViewOfFile(ipc->packet)){
            print_error("UnmapViewOfFile");
            result = -1;
        }
        else{
            ipc->packet = NULL;
        }
    }
    if (close_handle(&ipc->stop) != 0) result = -1;
    if (close_handle(&ipc->consumed) != 0) result = -1;
    if (close_handle(&ipc->ready) != 0) result = -1;
    if (close_handle(&ipc->mapping) != 0) result = -1;
    if (close_handle(&ipc->file) != 0) result = -1;
    return result;
}
