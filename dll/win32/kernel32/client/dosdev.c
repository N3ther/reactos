/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/dosdev.c
 * PURPOSE:         Dos device functions
 * PROGRAMMER:      Ariadne (ariadne@xs4all.nl)
 * UPDATE HISTORY:
 *                  Created 01/11/98
 */

/* INCLUDES ******************************************************************/

#include <k32.h>

#define NDEBUG
#include <debug.h>
#include <dbt.h>
DEBUG_CHANNEL(kernel32file);

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
DefineDosDeviceA(
    DWORD dwFlags,
    LPCSTR lpDeviceName,
    LPCSTR lpTargetPath
    )
{
    BOOL Result;
    NTSTATUS Status;
    ANSI_STRING AnsiString;
    PWSTR TargetPathBuffer;
    UNICODE_STRING TargetPathU;
    PUNICODE_STRING DeviceNameU;

    /* Convert DeviceName using static unicode string */
    RtlInitAnsiString(&AnsiString, lpDeviceName);
    DeviceNameU = &NtCurrentTeb()->StaticUnicodeString;
    Status = RtlAnsiStringToUnicodeString(DeviceNameU, &AnsiString, FALSE);
    if (!NT_SUCCESS(Status))
    {
        /*
         * If the static unicode string is too small,
         * it's because the name is too long...
         * so, return appropriate status!
         */
        if (Status == STATUS_BUFFER_OVERFLOW)
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return FALSE;
        }

        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* Convert target path if existing */
    if (lpTargetPath != NULL)
    {
        RtlInitAnsiString(&AnsiString, lpTargetPath);
        Status = RtlAnsiStringToUnicodeString(&TargetPathU, &AnsiString, TRUE);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }

        TargetPathBuffer = TargetPathU.Buffer;
    }
    else
    {
        TargetPathBuffer = NULL;
    }

    /* Call W */
    Result = DefineDosDeviceW(dwFlags, DeviceNameU->Buffer, TargetPathBuffer);

    /* Free target path if allocated */
    if (TargetPathBuffer != NULL)
    {
        RtlFreeUnicodeString(&TargetPathU);
    }

    return Result;
}


/*
 * @implemented
 */
BOOL
WINAPI
DefineDosDeviceW(
    DWORD dwFlags,
    LPCWSTR lpDeviceName,
    LPCWSTR lpTargetPath
    )
{
    ULONG ArgumentCount;
    ULONG BufferSize;
    BASE_API_MESSAGE ApiMessage;
    PBASE_DEFINE_DOS_DEVICE DefineDosDeviceRequest = &ApiMessage.Data.DefineDosDeviceRequest;
    PCSR_CAPTURE_BUFFER CaptureBuffer;
    UNICODE_STRING NtTargetPathU;
    UNICODE_STRING DeviceNameU;
    UNICODE_STRING DeviceUpcaseNameU;
    HANDLE hUser32;
    DEV_BROADCAST_VOLUME dbcv;
    BOOL Result = TRUE;
    DWORD dwRecipients;
    typedef long (WINAPI *BSM_type)(DWORD, LPDWORD, UINT, WPARAM, LPARAM);
    BSM_type BSM_ptr;

    if ( (dwFlags & 0xFFFFFFF0) ||
        ((dwFlags & DDD_EXACT_MATCH_ON_REMOVE) &&
        ! (dwFlags & DDD_REMOVE_DEFINITION)) )
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ArgumentCount = 1;
    BufferSize = 0;
    if (!lpTargetPath)
    {
        RtlInitUnicodeString(&NtTargetPathU,
                             NULL);
    }
    else
    {
        if (dwFlags & DDD_RAW_TARGET_PATH)
        {
            RtlInitUnicodeString(&NtTargetPathU,
                                 lpTargetPath);
        }
        else
        {
            if (!RtlDosPathNameToNtPathName_U(lpTargetPath,
                                              &NtTargetPathU,
                                              NULL,
                                              NULL))
            {
                WARN("RtlDosPathNameToNtPathName_U() failed\n");
                BaseSetLastNTError(STATUS_OBJECT_NAME_INVALID);
                return FALSE;
            }
        }
        ArgumentCount = 2;
        BufferSize += NtTargetPathU.Length;
    }

    RtlInitUnicodeString(&DeviceNameU,
                         lpDeviceName);
    RtlUpcaseUnicodeString(&DeviceUpcaseNameU,
                           &DeviceNameU,
                           TRUE);
    BufferSize += DeviceUpcaseNameU.Length;

    CaptureBuffer = CsrAllocateCaptureBuffer(ArgumentCount,
                                             BufferSize);
    if (!CaptureBuffer)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        Result = FALSE;
    }
    else
    {
        DefineDosDeviceRequest->Flags = dwFlags;

        CsrCaptureMessageBuffer(CaptureBuffer,
                                DeviceUpcaseNameU.Buffer,
                                DeviceUpcaseNameU.Length,
                                (PVOID*)&DefineDosDeviceRequest->DeviceName.Buffer);

        DefineDosDeviceRequest->DeviceName.Length =
            DeviceUpcaseNameU.Length;
        DefineDosDeviceRequest->DeviceName.MaximumLength =
            DeviceUpcaseNameU.Length;

        if (NtTargetPathU.Buffer)
        {
            CsrCaptureMessageBuffer(CaptureBuffer,
                                    NtTargetPathU.Buffer,
                                    NtTargetPathU.Length,
                                    (PVOID*)&DefineDosDeviceRequest->TargetPath.Buffer);
        }
        DefineDosDeviceRequest->TargetPath.Length =
            NtTargetPathU.Length;
        DefineDosDeviceRequest->TargetPath.MaximumLength =
            NtTargetPathU.Length;

        CsrClientCallServer((PCSR_API_MESSAGE)&ApiMessage,
                            CaptureBuffer,
                            CSR_CREATE_API_NUMBER(BASESRV_SERVERDLL_INDEX, BasepDefineDosDevice),
                            sizeof(*DefineDosDeviceRequest));
        CsrFreeCaptureBuffer(CaptureBuffer);

        if (!NT_SUCCESS(ApiMessage.Status))
        {
            WARN("CsrClientCallServer() failed (Status %lx)\n", ApiMessage.Status);
            BaseSetLastNTError(ApiMessage.Status);
            Result = FALSE;
        }
        else
        {
            if (! (dwFlags & DDD_NO_BROADCAST_SYSTEM) &&
                DeviceUpcaseNameU.Length == 2 * sizeof(WCHAR) &&
                DeviceUpcaseNameU.Buffer[1] == L':' &&
                ( (DeviceUpcaseNameU.Buffer[0] - L'A') < 26 ))
            {
                hUser32 = LoadLibraryA("user32.dll");
                if (hUser32)
                {
                    BSM_ptr = (BSM_type)
                        GetProcAddress(hUser32, "BroadcastSystemMessageW");
                    if (BSM_ptr)
                    {
                        dwRecipients = BSM_APPLICATIONS;
                        dbcv.dbcv_size = sizeof(DEV_BROADCAST_VOLUME);
                        dbcv.dbcv_devicetype = DBT_DEVTYP_VOLUME;
                        dbcv.dbcv_reserved = 0;
                        dbcv.dbcv_unitmask |=
                            (1 << (DeviceUpcaseNameU.Buffer[0] - L'A'));
                        dbcv.dbcv_flags = DBTF_NET;
                        (void) BSM_ptr(BSF_SENDNOTIFYMESSAGE | BSF_FLUSHDISK,
                                       &dwRecipients,
                                       WM_DEVICECHANGE,
                                       (WPARAM)DBT_DEVICEARRIVAL,
                                       (LPARAM)&dbcv);
                    }
                    FreeLibrary(hUser32);
                }
            }
        }
    }

    if (NtTargetPathU.Buffer &&
        NtTargetPathU.Buffer != lpTargetPath)
    {
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    NtTargetPathU.Buffer);
    }
    RtlFreeUnicodeString(&DeviceUpcaseNameU);
    return Result;
}


/*
 * @implemented
 */
DWORD
WINAPI
QueryDosDeviceA(
    LPCSTR lpDeviceName,
    LPSTR lpTargetPath,
    DWORD ucchMax
    )
{
    NTSTATUS Status;
    USHORT CurrentPosition;
    ANSI_STRING AnsiString;
    UNICODE_STRING TargetPathU;
    PUNICODE_STRING DeviceNameU;
    DWORD RetLength, CurrentLength, Length;
    PWSTR DeviceNameBuffer, TargetPathBuffer;

    /* If we want a specific device name, convert it */
    if (lpDeviceName != NULL)
    {
        /* Convert DeviceName using static unicode string */
        RtlInitAnsiString(&AnsiString, lpDeviceName);
        DeviceNameU = &NtCurrentTeb()->StaticUnicodeString;
        Status = RtlAnsiStringToUnicodeString(DeviceNameU, &AnsiString, FALSE);
        if (!NT_SUCCESS(Status))
        {
            /*
             * If the static unicode string is too small,
             * it's because the name is too long...
             * so, return appropriate status!
             */
            if (Status == STATUS_BUFFER_OVERFLOW)
            {
                SetLastError(ERROR_FILENAME_EXCED_RANGE);
                return FALSE;
            }

            BaseSetLastNTError(Status);
            return FALSE;
        }

        DeviceNameBuffer = DeviceNameU->Buffer;
    }
    else
    {
        DeviceNameBuffer = NULL;
    }

    /* Allocate the output buffer for W call */
    TargetPathBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, ucchMax * sizeof(WCHAR));
    if (TargetPathBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    /* Call W */
    Length = QueryDosDeviceW(DeviceNameBuffer, TargetPathBuffer, ucchMax);
    /* We'll return that length in case of a success */
    RetLength = Length;

    /* Handle the case where we would fill output buffer completly */
    if (Length != 0 && Length == ucchMax)
    {
        /* This will be our work length (but not the one we return) */
        --Length;
        /* Already 0 the last char */
        lpTargetPath[Length] = ANSI_NULL;
    }

    /* If we had an output, start the convert loop */
    if (Length != 0)
    {
        /*
         * We'll have to loop because TargetPathBuffer may contain
         * several strings (NULL separated)
         * We'll start at position 0
         */
        CurrentPosition = 0;
        while (CurrentPosition < Length)
        {
            /* Get the maximum length */
            CurrentLength = min(Length - CurrentPosition, MAXUSHORT / 2);

            /* Initialize our output string */
            AnsiString.Length = 0;
            AnsiString.MaximumLength = CurrentLength + sizeof(ANSI_NULL);
            AnsiString.Buffer = &lpTargetPath[CurrentPosition];

            /* Initialize input string that will be converted */
            TargetPathU.Length = CurrentLength * sizeof(WCHAR);
            TargetPathU.MaximumLength = CurrentLength * sizeof(WCHAR) + sizeof(UNICODE_NULL);
            TargetPathU.Buffer = &TargetPathBuffer[CurrentPosition];

            /* Convert to ANSI */
            Status = RtlUnicodeStringToAnsiString(&AnsiString, &TargetPathU, FALSE);
            if (!NT_SUCCESS(Status))
            {
                BaseSetLastNTError(Status);
                /* In case of a failure, forget about everything */
                RetLength = 0;

                goto Leave;
            }

            /* Move to the next string */
            CurrentPosition += CurrentLength;
        }
    }

Leave:
    /* Free our intermediate buffer and leave */
    RtlFreeHeap(RtlGetProcessHeap(), 0, TargetPathBuffer);

    return RetLength;
}


/*
 * @implemented
 */
DWORD
WINAPI
QueryDosDeviceW(
    LPCWSTR lpDeviceName,
    LPWSTR lpTargetPath,
    DWORD ucchMax
    )
{
    POBJECT_DIRECTORY_INFORMATION DirInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING UnicodeString;
    HANDLE DirectoryHandle;
    HANDLE DeviceHandle;
    ULONG ReturnLength;
    ULONG NameLength;
    ULONG Length;
    ULONG Context;
    BOOLEAN RestartScan;
    NTSTATUS Status;
    UCHAR Buffer[512];
    PWSTR Ptr;

    /* Open the '\??' directory */
    RtlInitUnicodeString(&UnicodeString, L"\\??");
    InitializeObjectAttributes(&ObjectAttributes,
                               &UnicodeString,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenDirectoryObject(&DirectoryHandle,
                                   DIRECTORY_QUERY,
                                   &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        WARN("NtOpenDirectoryObject() failed (Status %lx)\n", Status);
        BaseSetLastNTError(Status);
        return 0;
    }

    Length = 0;

    if (lpDeviceName != NULL)
    {
        /* Open the lpDeviceName link object */
        RtlInitUnicodeString(&UnicodeString, (PWSTR)lpDeviceName);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &UnicodeString,
                                   OBJ_CASE_INSENSITIVE,
                                   DirectoryHandle,
                                   NULL);
        Status = NtOpenSymbolicLinkObject(&DeviceHandle,
                                          SYMBOLIC_LINK_QUERY,
                                          &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            WARN("NtOpenSymbolicLinkObject() failed (Status %lx)\n", Status);
            NtClose(DirectoryHandle);
            BaseSetLastNTError(Status);
            return 0;
        }

        /* Query link target */
        UnicodeString.Length = 0;
        UnicodeString.MaximumLength = (USHORT)ucchMax * sizeof(WCHAR);
        UnicodeString.Buffer = lpTargetPath;

        ReturnLength = 0;
        Status = NtQuerySymbolicLinkObject(DeviceHandle,
                                           &UnicodeString,
                                           &ReturnLength);
        NtClose(DeviceHandle);
        NtClose(DirectoryHandle);
        if (!NT_SUCCESS(Status))
        {
            WARN("NtQuerySymbolicLinkObject() failed (Status %lx)\n", Status);
            BaseSetLastNTError(Status);
            return 0;
        }

        TRACE("ReturnLength: %lu\n", ReturnLength);
        TRACE("TargetLength: %hu\n", UnicodeString.Length);
        TRACE("Target: '%wZ'\n", &UnicodeString);

        Length = UnicodeString.Length / sizeof(WCHAR);
        if (Length < ucchMax)
        {
            /* Append null-character */
            lpTargetPath[Length] = UNICODE_NULL;
            Length++;
        }
        else
        {
            TRACE("Buffer is too small\n");
            BaseSetLastNTError(STATUS_BUFFER_TOO_SMALL);
            return 0;
        }
    }
    else
    {
        RestartScan = TRUE;
        Context = 0;
        Ptr = lpTargetPath;
        DirInfo = (POBJECT_DIRECTORY_INFORMATION)Buffer;

        while (TRUE)
        {
            Status = NtQueryDirectoryObject(DirectoryHandle,
                                            Buffer,
                                            sizeof(Buffer),
                                            TRUE,
                                            RestartScan,
                                            &Context,
                                            &ReturnLength);
            if (!NT_SUCCESS(Status))
            {
                if (Status == STATUS_NO_MORE_ENTRIES)
                {
                    /* Terminate the buffer */
                    *Ptr = UNICODE_NULL;
                    Length++;

                    Status = STATUS_SUCCESS;
                }
                else
                {
                    Length = 0;
                }
                BaseSetLastNTError(Status);
                break;
            }

            if (!wcscmp(DirInfo->TypeName.Buffer, L"SymbolicLink"))
            {
                TRACE("Name: '%wZ'\n", &DirInfo->Name);

                NameLength = DirInfo->Name.Length / sizeof(WCHAR);
                if (Length + NameLength + 1 >= ucchMax)
                {
                    Length = 0;
                    BaseSetLastNTError(STATUS_BUFFER_TOO_SMALL);
                    break;
                }

                memcpy(Ptr, DirInfo->Name.Buffer, DirInfo->Name.Length);
                Ptr += NameLength;
                Length += NameLength;
                *Ptr = UNICODE_NULL;
                Ptr++;
                Length++;
            }

            RestartScan = FALSE;
        }

        NtClose(DirectoryHandle);
    }

    return Length;
}

/* EOF */
