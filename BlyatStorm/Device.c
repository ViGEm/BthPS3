/*++

Module Name:

    device.c - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "device.tmh"
#include "BthPS3.h"
#include <stdio.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BlyatStormCreateDevice)
#endif

NTSTATUS
BlyatStormCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status;
    WDF_TIMER_CONFIG  timerConfig;
    WDF_OBJECT_ATTRIBUTES  timerAttributes;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (NT_SUCCESS(status)) {
        //
        // Get a pointer to the device context structure that we just associated
        // with the device object. We define this structure in the device.h
        // header file. DeviceGetContext is an inline function generated by
        // using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
        // This function will do the type checking and return the device context.
        // If you pass a wrong object handle it will return NULL and assert if
        // run under framework verifier mode.
        //
        deviceContext = DeviceGetContext(device);

        //
        // Initialize the context.
        //
        WDF_TIMER_CONFIG_INIT(
            &timerConfig,
            OutputReport_EvtTimerFunc
        );

        WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
        timerAttributes.ParentObject = device;
        timerAttributes.ExecutionLevel = WdfExecutionLevelPassive;

        status = WdfTimerCreate(
            &timerConfig,
            &timerAttributes,
            &deviceContext->OutputReportTimer
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfTimerCreate failed with status %!STATUS!",
                status
            );

            return status;
        }

        WDF_TIMER_CONFIG_INIT(
            &timerConfig,
            Init_EvtTimerFunc
        );

        status = WdfTimerCreate(
            &timerConfig,
            &timerAttributes,
            &deviceContext->InitTimer
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfTimerCreate failed with status %!STATUS!",
                status
            );

            return status;
        }

        WDF_TIMER_CONFIG_INIT(
            &timerConfig,
            InputReport_EvtTimerFunc
        );

        status = WdfTimerCreate(
            &timerConfig,
            &timerAttributes,
            &deviceContext->InputReportTimer
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfTimerCreate failed with status %!STATUS!",
                status
            );

            return status;
        }

        WdfTimerStart(deviceContext->OutputReportTimer, WDF_REL_TIMEOUT_IN_MS(0x64));
        WdfTimerStart(deviceContext->InitTimer, WDF_REL_TIMEOUT_IN_MS(0x64));

        //
        // Create a device interface so that applications can find and talk
        // to us.
        //
        status = WdfDeviceCreateDeviceInterface(
            device,
            &GUID_DEVINTERFACE_BlyatStorm,
            NULL // ReferenceString
        );

        if (NT_SUCCESS(status)) {
            //
            // Initialize the I/O Package and any Queues
            //
            status = BlyatStormQueueInitialize(device);
        }
    }

    return status;
}

VOID
TraceDumpBuffer(
    PVOID Buffer,
    ULONG BufferLength
)
{
    PWSTR   dumpBuffer;
    size_t  dumpBufferLength;
    ULONG   i;

    dumpBufferLength = ((BufferLength * sizeof(WCHAR)) * 3) + 2;
    dumpBuffer = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        dumpBufferLength,
        'egrA'
    );
    RtlZeroMemory(dumpBuffer, dumpBufferLength);

    for (i = 0; i < BufferLength; i++)
    {
        swprintf(&dumpBuffer[i * 3], L"%02X ", ((PUCHAR)Buffer)[i]);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION,
        TRACE_DEVICE,
        "%ws",
        dumpBuffer);

    ExFreePoolWithTag(dumpBuffer, 'egrA');
}


_Use_decl_annotations_
VOID
OutputReport_EvtTimerFunc(
    WDFTIMER  Timer
)
{
    WDFDEVICE device = WdfTimerGetParentObject(Timer);
    WDFIOTARGET ioTarget = WdfDeviceGetIoTarget(device);
    PDEVICE_CONTEXT devCtx = DeviceGetContext(device);

    UNREFERENCED_PARAMETER(devCtx);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    static UCHAR G_Ds3HidOutputReport[] = {
    0x52, 0x01, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1E, 0xFF, 0x27, 0x10, 0x00,
    0x32, 0xFF, 0x27, 0x10, 0x00, 0x32, 0xFF, 0x27,
    0x10, 0x00, 0x32, 0xFF, 0x27, 0x10, 0x00, 0x32,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
    };

    static BOOLEAN toggle = FALSE;

    toggle = !toggle;
    G_Ds3HidOutputReport[11] = (toggle) ? 0x02 : 0x04;

    NTSTATUS status;
    PVOID buffer = NULL;
    WDF_MEMORY_DESCRIPTOR  MemoryDescriptor;
    WDFMEMORY  MemoryHandle = NULL;
    status = WdfMemoryCreate(NULL,
        NonPagedPool,
        'aylB',
        0x32,
        &MemoryHandle,
        &buffer);

    RtlCopyMemory(buffer, G_Ds3HidOutputReport, 0x32);

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&MemoryDescriptor,
        MemoryHandle,
        NULL);

    status = WdfIoTargetSendInternalIoctlSynchronously(
        ioTarget,
        NULL,
        IOCTL_BTHPS3_HID_CONTROL_WRITE,
        &MemoryDescriptor,
        NULL,
        NULL,
        NULL
    );

    WdfObjectDelete(MemoryHandle);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfIoTargetSendInternalIoctlSynchronously failed with status %!STATUS!",
            status
        );
        return;
    }

    WdfTimerStart(devCtx->OutputReportTimer, WDF_REL_TIMEOUT_IN_MS(0x01F4));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
VOID
Init_EvtTimerFunc(
    WDFTIMER  Timer
)
{
    WDFDEVICE device = WdfTimerGetParentObject(Timer);
    WDFIOTARGET ioTarget = WdfDeviceGetIoTarget(device);
    PDEVICE_CONTEXT devCtx = DeviceGetContext(device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    UNREFERENCED_PARAMETER(devCtx);

    BYTE hidCommandEnable[] = {
            0x53, 0xF4, 0x42, 0x03, 0x00, 0x00
    };

    NTSTATUS status;
    PVOID buffer = NULL;
    WDF_MEMORY_DESCRIPTOR  MemoryDescriptor;
    WDFMEMORY  MemoryHandle = NULL;
    status = WdfMemoryCreate(NULL,
        NonPagedPool,
        'aylB',
        0x06,
        &MemoryHandle,
        &buffer);

    RtlCopyMemory(buffer, hidCommandEnable, 0x06);

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&MemoryDescriptor,
        MemoryHandle,
        NULL);

    status = WdfIoTargetSendInternalIoctlSynchronously(
        ioTarget,
        NULL,
        IOCTL_BTHPS3_HID_CONTROL_WRITE,
        &MemoryDescriptor,
        NULL,
        NULL,
        NULL
    );

    WdfObjectDelete(MemoryHandle);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfIoTargetSendInternalIoctlSynchronously failed with status %!STATUS!",
            status
        );
        return;
    }

    WdfTimerStart(devCtx->InputReportTimer, WDF_REL_TIMEOUT_IN_MS(0x64));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}

_Use_decl_annotations_
VOID
InputReport_EvtTimerFunc(
    WDFTIMER  Timer
)
{
    WDFDEVICE device = WdfTimerGetParentObject(Timer);
    WDFIOTARGET ioTarget = WdfDeviceGetIoTarget(device);
    PDEVICE_CONTEXT devCtx = DeviceGetContext(device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    NTSTATUS status;
    PVOID buffer = NULL;
    size_t bufferLength = 0;
    WDF_MEMORY_DESCRIPTOR  MemoryDescriptor;
    WDFMEMORY  MemoryHandle = NULL;
    status = WdfMemoryCreate(NULL,
        NonPagedPool,
        'aylB',
        BTHPS3_SIXAXIS_HID_INPUT_REPORT_SIZE,
        &MemoryHandle,
        &buffer);

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&MemoryDescriptor,
        MemoryHandle,
        NULL);

    status = WdfIoTargetSendInternalIoctlSynchronously(
        ioTarget,
        NULL,
        IOCTL_BTHPS3_HID_INTERRUPT_READ,
        NULL,
        &MemoryDescriptor,
        NULL,
        &bufferLength
    );

    TraceDumpBuffer(buffer, (ULONG)bufferLength);

    WdfObjectDelete(MemoryHandle);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfIoTargetSendInternalIoctlSynchronously failed with status %!STATUS!",
            status
        );
        //return;
    }

    WdfTimerStart(devCtx->InputReportTimer, WDF_REL_TIMEOUT_IN_MS(0x01));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}
