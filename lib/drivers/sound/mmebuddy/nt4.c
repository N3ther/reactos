/*
    ReactOS Sound System
    MME Driver Helper

    Purpose:
        Legacy (NT4) sound device support

    Author:
        Andrew Greenwood (silverblade@reactos.org)

    History:
        4 July 2008 - Created
*/

/*
    A better way of detecting sound devices...
    Search the appropriate registry key!
*/

#include <windows.h>
#include <mmsystem.h>
#include <ntddsnd.h>
#include <debug.h>

#include <mmebuddy.h>

/*
    Open the parameters key of a sound driver.
    NT4 only.
*/
MMRESULT
OpenSoundDriverParametersRegKey(
    IN  LPWSTR ServiceName,
    OUT PHKEY KeyHandle)
{
    ULONG KeyLength;
    PWCHAR ParametersKeyName;

    if ( ! ServiceName )
        return MMSYSERR_INVALPARAM;

    if ( ! KeyHandle )
        return MMSYSERR_INVALPARAM;

    /* Work out how long the string will be */
    KeyLength = wcslen(REG_SERVICES_KEY_NAME_U) + 1
              + wcslen(ServiceName) + 1
              + wcslen(REG_PARAMETERS_KEY_NAME_U);

    /* Allocate memory for the string */
    ParametersKeyName = AllocateWideString(KeyLength);

    if ( ! ParametersKeyName )
        return MMSYSERR_NOMEM;

    /* Construct the registry path */
    wsprintf(ParametersKeyName,
             L"%s\\%s\\%s",
             REG_SERVICES_KEY_NAME_U,
             ServiceName,
             REG_PARAMETERS_KEY_NAME_U);

    MessageBox(0, ParametersKeyName, L"Parameters key is...", MB_OK | MB_TASKMODAL);

    /* Perform the open */
    if ( RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                      ParametersKeyName,
                      0,
                      KEY_READ,
                      KeyHandle) != ERROR_SUCCESS )
    {
        /* Couldn't open the key */
        FreeMemory(ParametersKeyName);
        return MMSYSERR_ERROR;
    }

    FreeMemory(ParametersKeyName);

    return MMSYSERR_NOERROR;
}

/*
    Open one of the Device sub-keys belonging to the sound driver.
    NT4 only.
*/
MMRESULT
OpenSoundDeviceRegKey(
    IN  LPWSTR ServiceName,
    IN  DWORD DeviceIndex,
    OUT PHKEY KeyHandle)
{
    DWORD PathLength;
    PWCHAR RegPath;

    if ( ! ServiceName )
        return MMSYSERR_INVALPARAM;

    if ( ! KeyHandle )
        return MMSYSERR_INVALPARAM;

    /*
        Work out the space required to hold the path:

        HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\
            sndblst\
                Parameters\
                    Device123\
    */
    PathLength = wcslen(REG_SERVICES_KEY_NAME_U) + 1
               + wcslen(ServiceName) + 1
               + wcslen(REG_PARAMETERS_KEY_NAME_U) + 1
               + wcslen(REG_DEVICE_KEY_NAME_U)
               + GetDigitCount(DeviceIndex);

    /* Allocate storage for the string */
    RegPath = AllocateWideString(PathLength);

    if ( ! RegPath )
    {
        return MMSYSERR_NOMEM;
    }

    /* Write the path */
    wsprintf(RegPath,
             L"%ls\\%ls\\%ls\\%ls%d",
             REG_SERVICES_KEY_NAME_U,
             ServiceName,
             REG_PARAMETERS_KEY_NAME_U,
             REG_DEVICE_KEY_NAME_U,
             DeviceIndex);

    MessageBox(0, RegPath, L"Opening registry path", MB_OK | MB_TASKMODAL);

    /* Perform the open */
    if ( RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                      RegPath,
                      0,
                      KEY_READ,
                      KeyHandle) != ERROR_SUCCESS )
    {
        /* Couldn't open the key */
        FreeMemory(RegPath);
        return MMSYSERR_ERROR;
    }

    FreeMemory(RegPath);

    return MMSYSERR_NOERROR;
}

/*
    This is the "nice" way to discover audio devices in NT4 - go into the
    service registry key and enumerate the Parameters\Device*\Devices
    values. The value names represent the device name, whereas the data
    assigned to them identifies the type of device.
*/
MMRESULT
EnumerateNt4ServiceSoundDevices(
    IN  LPWSTR ServiceName,
    IN  UCHAR DeviceType,
    IN  SOUND_DEVICE_DETECTED_PROC SoundDeviceDetectedProc)
{
    HKEY Key;
    DWORD KeyIndex = 0;

    /* Validate parameters */
    if ( ! ServiceName )
        return MMSYSERR_INVALPARAM;

    if ( ! VALID_SOUND_DEVICE_TYPE(DeviceType) )
        return MMSYSERR_INVALPARAM;

    while ( OpenSoundDeviceRegKey(ServiceName, KeyIndex, &Key) == MMSYSERR_NOERROR )
    {
        HKEY DevicesKey;
        DWORD ValueType = REG_NONE, ValueIndex = 0;
        DWORD MaxNameLength = 0, ValueNameLength = 0;
        PWSTR DevicePath = NULL, ValueName = NULL;
        DWORD ValueDataLength = sizeof(DWORD);
        DWORD ValueData;

        if ( RegOpenKeyEx(Key,
                          REG_DEVICES_KEY_NAME_U,
                          0,
                          KEY_READ,
                          &DevicesKey) == ERROR_SUCCESS )
        {
            /* Find out how much memory is needed for the key name */
            if ( RegQueryInfoKey(DevicesKey,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 &MaxNameLength,
                                 NULL, NULL, NULL) != ERROR_SUCCESS )
            {
                RegCloseKey(DevicesKey);
                RegCloseKey(Key);

                return MMSYSERR_ERROR;
            }

            DevicePath = AllocateWideString(MaxNameLength +
                                            strlen("\\\\.\\"));

            /* Check that the memory allocation was successful */
            if ( ! DevicePath )
            {
                /* There's no point in going further */
                RegCloseKey(DevicesKey);
                RegCloseKey(Key);

                return MMSYSERR_NOMEM;
            }

            /* Insert the device path prefix */
            wsprintf(DevicePath, L"\\\\.\\");

            /* The offset of the string following this prefix */
            ValueName = DevicePath + strlen("\\\\.\\");

            /* Copy this so that it may be overwritten - include NULL */
            ValueNameLength = MaxNameLength + sizeof(WCHAR);

            while ( RegEnumValue(DevicesKey,
                                 ValueIndex,
                                 ValueName,
                                 &ValueNameLength,
                                 NULL,
                                 &ValueType,
                                 (LPBYTE) &ValueData,
                                 &ValueDataLength) == ERROR_SUCCESS )
            {
                /* Device types are stored as DWORDs */
                if ( ( ValueType == REG_DWORD ) &&
                     ( ValueDataLength == sizeof(DWORD) ) )
                {
                    SoundDeviceDetectedProc(
                        DeviceType,
                        DevicePath,
                        INVALID_HANDLE_VALUE);
                }

                /* Reset variables for the next iteration */
                ValueNameLength = MaxNameLength + sizeof(WCHAR);
                ZeroMemory(ValueName, (MaxNameLength+1)*sizeof(WCHAR));
                /*ZeroWideString(ValueName);*/
                ValueDataLength = sizeof(DWORD);
                ValueData = 0;
                ValueType = REG_NONE;

                ++ ValueIndex;
            }

            FreeMemory(DevicePath);

            RegCloseKey(DevicesKey);
        }

        ++ KeyIndex;

        RegCloseKey(Key);
    }

    return MMSYSERR_NOERROR;
}

/*
    Brute-force device detection, using a base device name (eg: \\.\WaveOut).

    This will add the device number as a suffix to the end of the string and
    attempt to open the device based on that name. On success, it will
    increment the device number and repeat this process.

    When it runs out of devices, it will give up.
*/
MMRESULT
DetectNt4SoundDevices(
    IN  UCHAR DeviceType,
    IN  PWSTR BaseDeviceName,
    IN  SOUND_DEVICE_DETECTED_PROC SoundDeviceDetectedProc)
{
    ULONG DeviceNameLength = 0;
    PWSTR DeviceName = NULL;
    ULONG Index = 0, Count = 0;
    HANDLE DeviceHandle;
    BOOLEAN DoSearch = TRUE;

    DPRINT("Detecting NT4 style sound devices of type %d\n", DeviceType);

    if ( ! VALID_SOUND_DEVICE_TYPE(DeviceType) )
    {
        return MMSYSERR_INVALPARAM;
    }

    DeviceNameLength = wcslen(BaseDeviceName);
    /* Consider the length of the number */
    DeviceNameLength += GetDigitCount(Index);

    DeviceName = AllocateWideString(DeviceNameLength);

    if ( ! DeviceName )
    {
        return MMSYSERR_NOMEM;
    }

    while ( DoSearch )
    {
        /* Nothing like a nice clean device name */
        ZeroWideString(DeviceName);
        wsprintf(DeviceName, L"%ls%d", BaseDeviceName, Index);

        if ( OpenKernelSoundDeviceByName(DeviceName,
                                         GENERIC_READ,
                                         &DeviceHandle) == MMSYSERR_NOERROR )
        {
            //DPRINT("Found device %d\n", Index);
            MessageBox(0, DeviceName, L"Opened device", MB_OK | MB_TASKMODAL);

            /* Notify the callback function */
            if ( SoundDeviceDetectedProc(DeviceType, DeviceName, DeviceHandle) )
            {
                ++ Count;
            }

            CloseHandle(DeviceHandle);

            ++ Index;
        }
        else
        {
            DoSearch = FALSE;
        }
    }

    FreeMemory(DeviceName);

    return MMSYSERR_NOERROR;
}
