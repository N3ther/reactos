/*
 * COPYRIGHT:  See COPYING in the top level directory
 * PROJECT:    ReactOS kernel
 * FILE:       drivers/filesastems/npfs/dirctl.c
 * PURPOSE:    Named pipe filesystem
 * PROGRAMMER: Eric Kohl
 */

/* INCLUDES ******************************************************************/

#include "npfs.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

static NTSTATUS
NpfsQueryDirectory(PNPFS_CCB Ccb,
                   PIRP Irp,
                   PULONG Size)
{
    PIO_STACK_LOCATION Stack;
    LONG BufferLength = 0;
    PUNICODE_STRING SearchPattern = NULL;
    FILE_INFORMATION_CLASS FileInformationClass;
    ULONG FileIndex = 0;
    PUCHAR Buffer = NULL;
    BOOLEAN First = FALSE;
    PLIST_ENTRY CurrentEntry;
    PNPFS_VCB Vcb;
    PNPFS_FCB PipeFcb;
    ULONG PipeIndex;
    BOOLEAN Found = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_NAMES_INFORMATION NamesBuffer;
    PFILE_DIRECTORY_INFORMATION DirectoryBuffer;
    PFILE_FULL_DIR_INFORMATION FullDirBuffer;
    PFILE_BOTH_DIR_INFORMATION BothDirBuffer;
    ULONG InfoSize = 0;

    Stack = IoGetCurrentIrpStackLocation(Irp);

    /* Obtain the callers parameters */
    BufferLength = Stack->Parameters.QueryDirectory.Length;
    SearchPattern = Stack->Parameters.QueryDirectory.FileName;
    FileInformationClass = Stack->Parameters.QueryDirectory.FileInformationClass;
    FileIndex = Stack->Parameters.QueryDirectory.FileIndex;

    DPRINT("SearchPattern: %p  '%wZ'\n", SearchPattern, SearchPattern);

    /* Determine Buffer for result */
    if (Irp->MdlAddress)
    {
        Buffer = MmGetSystemAddressForMdl(Irp->MdlAddress);
    }
    else
    {
        Buffer = Irp->UserBuffer;
    }

    /* Build the search pattern string */
    DPRINT("Ccb->u.Directory.SearchPattern.Buffer: %p\n", Ccb->u.Directory.SearchPattern.Buffer);
    if (Ccb->u.Directory.SearchPattern.Buffer == NULL)
    {
        First = TRUE;

        if (SearchPattern != NULL)
        {
            Ccb->u.Directory.SearchPattern.Buffer =
                ExAllocatePool(NonPagedPool, SearchPattern->Length + sizeof(WCHAR));
            if (Ccb->u.Directory.SearchPattern.Buffer == NULL)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Ccb->u.Directory.SearchPattern.Length = SearchPattern->Length;
            Ccb->u.Directory.SearchPattern.MaximumLength = SearchPattern->Length + sizeof(WCHAR);
            RtlCopyMemory(Ccb->u.Directory.SearchPattern.Buffer,
                          SearchPattern->Buffer,
                          SearchPattern->Length);
            Ccb->u.Directory.SearchPattern.Buffer[SearchPattern->Length / sizeof(WCHAR)] = 0;
        }
        else
        {
            Ccb->u.Directory.SearchPattern.Buffer =
                ExAllocatePool(NonPagedPool, 2 * sizeof(WCHAR));
            if (Ccb->u.Directory.SearchPattern.Buffer == NULL)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Ccb->u.Directory.SearchPattern.Length = sizeof(WCHAR);
            Ccb->u.Directory.SearchPattern.MaximumLength = 2 * sizeof(WCHAR);
            Ccb->u.Directory.SearchPattern.Buffer[0] = L'*';
            Ccb->u.Directory.SearchPattern.Buffer[1] = 0;
        }
    }
    DPRINT("Search pattern: '%wZ'\n", &Ccb->u.Directory.SearchPattern);

    /* Determine the file index */
    if (First || (Stack->Flags & SL_RESTART_SCAN))
    {
        FileIndex = 0;
    } else if ((Stack->Flags & SL_INDEX_SPECIFIED) == 0)
    {
        FileIndex = Ccb->u.Directory.FileIndex + 1;
    }
    DPRINT("FileIndex: %lu\n", FileIndex);

    DPRINT("Buffer = %p  tofind = %wZ\n", Buffer, &Ccb->u.Directory.SearchPattern);

    switch (FileInformationClass)
    {
        case FileDirectoryInformation:
            InfoSize = sizeof(FILE_DIRECTORY_INFORMATION) - sizeof(WCHAR);
            break;

        case FileFullDirectoryInformation:
            InfoSize = sizeof(FILE_FULL_DIR_INFORMATION) - sizeof(WCHAR);
            break;

        case FileBothDirectoryInformation:
            InfoSize = sizeof(FILE_BOTH_DIR_INFORMATION) - sizeof(WCHAR);
            break;

        case FileNamesInformation:
            InfoSize = sizeof(FILE_NAMES_INFORMATION) - sizeof(WCHAR);
            break;

        default:
            DPRINT1("Invalid information class: %lu\n", FileInformationClass);
            return STATUS_INVALID_INFO_CLASS;
    }

    PipeIndex = 0;

    Vcb = Ccb->Fcb->Vcb;
    CurrentEntry = Vcb->PipeListHead.Flink;
    while (CurrentEntry != &Vcb->PipeListHead &&
           Found == FALSE &&
           Status == STATUS_SUCCESS)
    {
        /* Get the FCB of the next pipe */
        PipeFcb = CONTAINING_RECORD(CurrentEntry,
                                    NPFS_FCB,
                                    PipeListEntry);

        /* Make sure it is a pipe FCB */
        ASSERT(PipeFcb->Type == FCB_PIPE);

        DPRINT("PipeName: %wZ\n", &PipeFcb->PipeName);

        if (FsRtlIsNameInExpression(&Ccb->u.Directory.SearchPattern,
                                    &PipeFcb->PipeName,
                                    TRUE,
                                    NULL))
        {
            DPRINT("Found pipe: %wZ\n", &PipeFcb->PipeName);

            if (PipeIndex >= FileIndex)
            {
                RtlZeroMemory(Buffer, InfoSize);

                switch (FileInformationClass)
                {
                    case FileDirectoryInformation:
                        DirectoryBuffer = (PFILE_DIRECTORY_INFORMATION)Buffer;
                        DirectoryBuffer->NextEntryOffset = 0;
                        DirectoryBuffer->FileIndex = PipeIndex;
                        DirectoryBuffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
                        DirectoryBuffer->EndOfFile.QuadPart = PipeFcb->CurrentInstances;
                        DirectoryBuffer->AllocationSize.LowPart = PipeFcb->MaximumInstances;
                        DirectoryBuffer->FileNameLength = PipeFcb->PipeName.Length;
                        RtlCopyMemory(DirectoryBuffer->FileName,
                                      PipeFcb->PipeName.Buffer,
                                      PipeFcb->PipeName.Length);
                        *Size = InfoSize + PipeFcb->PipeName.Length;
                        Status = STATUS_SUCCESS;
                        break;

                    case FileFullDirectoryInformation:
                        FullDirBuffer = (PFILE_FULL_DIR_INFORMATION)Buffer;
                        FullDirBuffer->NextEntryOffset = 0;
                        FullDirBuffer->FileIndex = PipeIndex;
                        FullDirBuffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
                        FullDirBuffer->EndOfFile.QuadPart = PipeFcb->CurrentInstances;
                        FullDirBuffer->AllocationSize.LowPart = PipeFcb->MaximumInstances;
                        FullDirBuffer->FileNameLength = PipeFcb->PipeName.Length;
                        RtlCopyMemory(FullDirBuffer->FileName,
                                      PipeFcb->PipeName.Buffer,
                                      PipeFcb->PipeName.Length);
                        *Size = InfoSize + PipeFcb->PipeName.Length;
                        Status = STATUS_SUCCESS;
                        break;

                    case FileBothDirectoryInformation:
                        BothDirBuffer = (PFILE_BOTH_DIR_INFORMATION)Buffer;
                        BothDirBuffer->NextEntryOffset = 0;
                        BothDirBuffer->FileIndex = PipeIndex;
                        BothDirBuffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
                        BothDirBuffer->EndOfFile.QuadPart = PipeFcb->CurrentInstances;
                        BothDirBuffer->AllocationSize.LowPart = PipeFcb->MaximumInstances;
                        BothDirBuffer->FileNameLength = PipeFcb->PipeName.Length;
                        RtlCopyMemory(BothDirBuffer->FileName,
                                      PipeFcb->PipeName.Buffer,
                                      PipeFcb->PipeName.Length);
                        *Size = InfoSize + PipeFcb->PipeName.Length;
                        Status = STATUS_SUCCESS;
                        break;

                    case FileNamesInformation:
                        NamesBuffer = (PFILE_NAMES_INFORMATION)Buffer;
                        NamesBuffer->NextEntryOffset = 0;
                        NamesBuffer->FileIndex = PipeIndex;
                        NamesBuffer->FileNameLength = PipeFcb->PipeName.Length;
                        RtlCopyMemory(NamesBuffer->FileName,
                                      PipeFcb->PipeName.Buffer,
                                      PipeFcb->PipeName.Length);
                        *Size = InfoSize + PipeFcb->PipeName.Length;
                        Status = STATUS_SUCCESS;
                        break;

                    default:
                        DPRINT1("Invalid information class: %lu\n", FileInformationClass);
                        Status = STATUS_INVALID_INFO_CLASS;
                        break;
                }

                Ccb->u.Directory.FileIndex = PipeIndex;
                Found = TRUE;

//                if (Stack->Flags & SL_RETURN_SINGLE_ENTRY)
//                    return STATUS_SUCCESS;

                break;
            }

            PipeIndex++;
        }

        CurrentEntry = CurrentEntry->Flink;
    }

    if (Found == FALSE)
        Status = STATUS_NO_MORE_FILES;

    return Status;
}


NTSTATUS NTAPI
NpfsDirectoryControl(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PFILE_OBJECT FileObject;
    PNPFS_CCB Ccb;
    PNPFS_FCB Fcb;
    NTSTATUS Status;
    ULONG Size = 0;

    DPRINT("NpfsDirectoryControl() called\n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    FileObject = IoStack->FileObject;

    if (NpfsGetCcb(FileObject, &Ccb) != CCB_DIRECTORY)
    {
        Status = STATUS_INVALID_PARAMETER;

        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return Status;
    }

    Fcb = Ccb->Fcb;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_QUERY_DIRECTORY:
            Status = NpfsQueryDirectory(Ccb,
                                        Irp,
                                        &Size);
            break;

        case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
            DPRINT1("IRP_MN_NOTIFY_CHANGE_DIRECTORY\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        default:
            DPRINT1("NPFS: MinorFunction %d\n", IoStack->MinorFunction);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Size;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

/* EOF */
