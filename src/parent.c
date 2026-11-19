#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "common.h"

static int child_path(char *path, DWORD size){
    DWORD length = GetModuleFileNameA(NULL, path, size);
    if (length == 0 || length >= size){
        fprintf(stderr, "ERROR: cannot obtain executable path (possibly too long)\n");
        return -1;
    }
    char *slash = strrchr(path, '\\');
    if (slash == NULL || (size_t)(slash + 1 - path) + sizeof("child.exe") > size){
        fprintf(stderr, "ERROR: child path is too long\n");
        return -1;
    }
    strcpy(slash + 1, "child.exe");
    return 0;
}

static int join_child(HANDLE child, HANDLE stop, int cancel){
    if (cancel && !SetEvent(stop)){
        print_error("SetEvent stop");
    }
    DWORD wait = WaitForSingleObject(child, 5000);
    if (wait == WAIT_OBJECT_0){
        return 0;
    }
    if (wait == WAIT_FAILED){
        print_error("WaitForSingleObject child");
    }
    else{
        fprintf(stderr, "ERROR: child did not exit in 5 seconds\n");
    }
    if (!TerminateProcess(child, EXIT_FAILURE)){
        print_error("TerminateProcess");
        return -1;
    }
    if (WaitForSingleObject(child, INFINITE) != WAIT_OBJECT_0){
        print_error("WaitForSingleObject after termination");
    }
    return -1;
}

int main(void){
    Ipc ipc = {0};
    HANDLE input = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {0};
    STARTUPINFOA si = {0};
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    char *filename = NULL;
    size_t capacity = 0;
    char prefix[100], path[MAX_PATH], command[MAX_PATH + 160];
    int result = EXIT_FAILURE, complete = 0;

    fprintf(stderr, "[parent PID %lu] Enter input file name:\n",
            (unsigned long)GetCurrentProcessId());
    if (read_line(stdin, &filename, &capacity) != 1 || filename[0] == '\0'){
        fprintf(stderr, "ERROR: expected a nonempty file name\n");
        goto cleanup;
    }
    input = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, &sa,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE){
        print_error("CreateFileA input");
        goto cleanup;
    }
    snprintf(prefix, sizeof(prefix), "Local\\os_lab4_%lu_%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    if (ipc_create(&ipc, prefix) != 0 || child_path(path, sizeof(path)) != 0){
        goto cleanup;
    }
    int length = snprintf(command, sizeof(command), "\"%s\" %s %lu", path, prefix,
                          (unsigned long)GetCurrentProcessId());
    if (length < 0 || (size_t)length >= sizeof(command)){
        fprintf(stderr, "ERROR: command line is too long\n");
        goto cleanup;
    }
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = input;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (si.hStdOutput == NULL || si.hStdOutput == INVALID_HANDLE_VALUE ||
        si.hStdError == NULL || si.hStdError == INVALID_HANDLE_VALUE){
        fprintf(stderr, "ERROR: standard output/error is unavailable\n");
        goto cleanup;
    }
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)){
        print_error("CreateProcessA");
        goto cleanup;
    }
    fprintf(stderr, "[parent PID %lu] child PID %lu, file mapping + events\n",
            (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId);
    if (close_handle(&pi.hThread) != 0 || close_handle(&input) != 0){
        goto cleanup;
    }

    HANDLE waits[] = {ipc.ready, pi.hProcess};
    for(;;){
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0){
            if (wait == WAIT_FAILED){
                print_error("WaitForMultipleObjects parent");
            }
            else{
                fprintf(stderr, "ERROR: child exited without completion packet\n");
            }
            goto cleanup;
        }
        Packet packet = *ipc.packet;
        if (packet.kind == PACKET_LINE && packet.length > 0 && packet.length < sizeof(packet.text)){
            if (!write_all(si.hStdOutput, packet.text, packet.length)){
                print_error("WriteFile(stdout)");
                goto cleanup;
            }
        }
        else if (packet.kind != PACKET_DONE || packet.length != 0){
            fprintf(stderr, "ERROR: invalid packet\n");
            goto cleanup;
        }
        if (!SetEvent(ipc.consumed)){
            print_error("SetEvent consumed");
            goto cleanup;
        }
        if (packet.kind == PACKET_DONE){
            complete = 1;
            result = EXIT_SUCCESS;
            break;
        }
    }

cleanup:
    if (pi.hProcess != NULL){
        if (join_child(pi.hProcess, ipc.stop, !complete) != 0){
            result = EXIT_FAILURE;
        }
        DWORD code;
        if (!GetExitCodeProcess(pi.hProcess, &code)){
            print_error("GetExitCodeProcess");
            result = EXIT_FAILURE;
        }
        else if (code != EXIT_SUCCESS){
            fprintf(stderr, "[parent] ERROR: child exited with code %lu\n", (unsigned long)code);
            result = EXIT_FAILURE;
        }
    }
    if (close_handle(&pi.hThread) != 0) result = EXIT_FAILURE;
    if (close_handle(&pi.hProcess) != 0) result = EXIT_FAILURE;
    if (close_handle(&input) != 0) result = EXIT_FAILURE;
    if (ipc_close(&ipc) != 0) result = EXIT_FAILURE;
    free(filename);
    return result;
}
