#include "precomp.h"
#include "vioser.h"
#include "public.h"

#if defined(EVENT_TRACING)
#include "Port.tmh"
#endif

EVT_WDF_WORKITEM VIOSerialPortSendPortReady;
EVT_WDF_WORKITEM VIOSerialPortCreateSymbolicName;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSerialDeviceListCreatePdo)
#pragma alloc_text(PAGE, VIOSerialPortRead)
#pragma alloc_text(PAGE, VIOSerialPortWrite)
#pragma alloc_text(PAGE, VIOSerialPortDeviceControl)
#endif



PVIOSERIAL_PORT
VIOSerialFindPortById(
    IN WDFDEVICE Device,
    IN ULONG id
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    WDFCHILDLIST    list;
    WDF_CHILD_LIST_ITERATOR     iterator;
    PRAWPDO_VIOSERIAL_PORT          rawPdo = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP,"%s  port = %d\n", __FUNCTION__, id);

    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);

    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           port;
        WDFDEVICE                hChild;

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &port.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );
        status = WdfChildListRetrieveNextDevice(
                                 list,
                                 &iterator,
                                 &hChild,
                                 &childInfo
                                 );
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        ASSERT(childInfo.Status == WdfChildListRetrieveDeviceSuccess);
        rawPdo = RawPdoSerialPortGetData(hChild);

        if(rawPdo && rawPdo->port->Id == id)
        {
            WdfChildListEndIteration(list, &iterator);
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  id = %d port = 0x%p\n", __FUNCTION__, id, rawPdo->port);
            return rawPdo->port;
        }
    }
    WdfChildListEndIteration(list, &iterator);
    return NULL;
}

VOID
VIOSerialAddPort(
    IN WDFDEVICE Device,
    IN ULONG id
)
{
    VIOSERIAL_PORT  port;
    PPORTS_DEVICE   pContext = GetPortsDevice(Device);
    NTSTATUS        status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  port = %d\n", __FUNCTION__, id);

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );

    port.Id = id;
    port.NameString.Buffer = NULL;
    port.NameString.Length = 0;
    port.NameString.MaximumLength = 0;

    port.InBuf = NULL;
    port.HostConnected = port.GuestConnected = FALSE;
    port.OutVqFull = FALSE;

    port.in_vq = pContext->in_vqs[port.Id];
    port.out_vq = pContext->out_vqs[port.Id];
    port.BusDevice = Device;

    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
                                 WdfFdoGetDefaultChildList(Device), 
                                 &port.Header,
                                 NULL
                                 );

    if (status == STATUS_OBJECT_NAME_EXISTS) 
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
           "The description is already present in the list, the serial number is not unique.\n");
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
           "WdfChildListAddOrUpdateChildDescriptionAsPresent = 0x%x.\n", status);

}

VOID
VIOSerialRemovePort(
    IN WDFDEVICE Device,
    IN PVIOSERIAL_PORT port
)
{
    PPORT_BUFFER    buf;
    PPORTS_DEVICE   pContext = GetPortsDevice(Device);
    NTSTATUS        status = STATUS_SUCCESS;
    WDFCHILDLIST    list;
    WDF_CHILD_LIST_ITERATOR     iterator;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  port = %d\n", __FUNCTION__, port->Id);

    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);


    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           vport;
        WDFDEVICE                hChild;

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &vport.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &vport.Header,
                                 sizeof(vport)
                                 );
        status = WdfChildListRetrieveNextDevice(
                                 list,
                                 &iterator,
                                 &hChild,
                                 &childInfo
                                 );
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        ASSERT(childInfo.Status == WdfChildListRetrieveDeviceSuccess);

        if(vport.Id == port->Id)
        {
           status = WdfChildListUpdateChildDescriptionAsMissing(
                                 list,
                                 &vport.Header
                                 );

           if (status == STATUS_NO_SUCH_DEVICE)
           {
              status = STATUS_INVALID_PARAMETER;
              break;
           }

           VIOSerialEnableDisableInterruptQueue(vport.in_vq, FALSE);
           VIOSerialEnableDisableInterruptQueue(vport.out_vq, FALSE);

           if(vport.GuestConnected)
           {
              VIOSerialSendCtrlMsg(vport.BusDevice, vport.Id, VIRTIO_CONSOLE_PORT_OPEN, 0);
           }

           VIOSerialDiscardPortData(&vport);
           VIOSerialReclaimConsumedBuffers(&vport);
           while (buf = VirtIODeviceDetachUnusedBuf(vport.in_vq))
           {
              VIOSerialFreeBuffer(buf);
           }
           if (vport.NameString.Buffer)
           {
              ExFreePoolWithTag(vport.NameString.Buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
              vport.NameString.Buffer = NULL;
              vport.NameString.Length = 0;
              vport.NameString.MaximumLength = 0;
           }
        }
    }
    WdfChildListEndIteration(list, &iterator);

    if (status != STATUS_NO_MORE_ENTRIES)
    {
        ASSERT(0);
    }
}


VOID
VIOSerialRemoveAllPorts(
    IN WDFDEVICE Device
)
{
    PPORT_BUFFER    buf;
    PPORTS_DEVICE   pContext = GetPortsDevice(Device);
    NTSTATUS        status = STATUS_SUCCESS;
    WDFCHILDLIST    list;
    WDF_CHILD_LIST_ITERATOR     iterator;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s\n", __FUNCTION__);

    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);


    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           vport;
        WDFDEVICE                hChild;

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &vport.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &vport.Header,
                                 sizeof(vport)
                                 );
        status = WdfChildListRetrieveNextDevice(
                                 list,
                                 &iterator,
                                 &hChild,
                                 &childInfo
                                 );
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        ASSERT(childInfo.Status == WdfChildListRetrieveDeviceSuccess);

        status = WdfChildListUpdateChildDescriptionAsMissing(
                                 list,
                                 &vport.Header
                                 );

        if (status == STATUS_NO_SUCH_DEVICE)
        {
           status = STATUS_INVALID_PARAMETER;
           break;
        }

        VIOSerialEnableDisableInterruptQueue(vport.in_vq, FALSE);
        VIOSerialEnableDisableInterruptQueue(vport.out_vq, FALSE);

        if(vport.GuestConnected)
        {
           VIOSerialSendCtrlMsg(vport.BusDevice, vport.Id, VIRTIO_CONSOLE_PORT_OPEN, 0);
        }

        VIOSerialDiscardPortData(&vport);
        VIOSerialReclaimConsumedBuffers(&vport);
        while (buf = VirtIODeviceDetachUnusedBuf(vport.in_vq))
        {
           VIOSerialFreeBuffer(buf);
        }
    }
    WdfChildListEndIteration(list, &iterator);

    if (status != STATUS_NO_MORE_ENTRIES)
    {
        ASSERT(0);
    }
}

VOID
VIOSerialRenewAllPorts(
    IN WDFDEVICE Device
)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDFCHILDLIST                 list;
    WDF_CHILD_LIST_ITERATOR      iterator;
    UINT                         nr_ports, i, j;
    struct virtqueue             *in_vq, *out_vq;
    PPORTS_DEVICE                pContext = GetPortsDevice(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s\n", __FUNCTION__);

    VirtIODeviceReset(&pContext->IODevice);
    VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

    nr_ports = pContext->consoleConfig.max_nr_ports;
    for(i = 0, j = 0; i < nr_ports; i++)
    {
        in_vq  = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i * 2, NULL);
        out_vq = VirtIODeviceFindVirtualQueue(&pContext->IODevice, (i * 2 ) + 1, NULL);

        if(i == 1) // Control Port
        {
           pContext->c_ivq = in_vq;
           pContext->c_ovq = out_vq;
        }
        else
        {
           pContext->in_vqs[j] = in_vq;
           pContext->out_vqs[j] = out_vq;
           ++j;
        }
    }

    if(pContext->isHostMultiport)
    {
        VIOSerialFillQueue(pContext->c_ivq, pContext->CVqLock);
    }


    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);

    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           vport;
        WDFDEVICE                hChild;

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &vport.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &vport.Header,
                                 sizeof(vport)
                                 );
        status = WdfChildListRetrieveNextDevice(
                                 list,
                                 &iterator,
                                 &hChild,
                                 &childInfo
                                 );
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        ASSERT(childInfo.Status == WdfChildListRetrieveDeviceSuccess);

        if (status == STATUS_NO_SUCH_DEVICE)
        {
           status = STATUS_INVALID_PARAMETER;
           break;
        }

        vport.in_vq = pContext->in_vqs[vport.Id];
        vport.out_vq = pContext->out_vqs[vport.Id];
        VIOSerialEnableDisableInterruptQueue(vport.in_vq, TRUE);
        VIOSerialEnableDisableInterruptQueue(vport.out_vq, TRUE);

        if(vport.GuestConnected)
        {
           VIOSerialSendCtrlMsg(vport.BusDevice, vport.Id, VIRTIO_CONSOLE_PORT_OPEN, 1);
        }

    }
    WdfChildListEndIteration(list, &iterator);

    return;
}

VOID
VIOSerialShutdownAllPorts(
    IN WDFDEVICE Device
)
{
    PPORT_BUFFER    buf;
    PPORTS_DEVICE   pContext = GetPortsDevice(Device);
    NTSTATUS        status = STATUS_SUCCESS;
    WDFCHILDLIST    list;
    WDF_CHILD_LIST_ITERATOR     iterator;
    UINT            nr_ports, i;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s\n", __FUNCTION__);

    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);

    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           vport;
        WDFDEVICE                hChild;

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &vport.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &vport.Header,
                                 sizeof(vport)
                                 );
        status = WdfChildListRetrieveNextDevice(
                                 list,
                                 &iterator,
                                 &hChild,
                                 &childInfo
                                 );
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        ASSERT(childInfo.Status == WdfChildListRetrieveDeviceSuccess);

        if (status == STATUS_NO_SUCH_DEVICE)
        {
           status = STATUS_INVALID_PARAMETER;
           break;
        }

        VIOSerialEnableDisableInterruptQueue(vport.in_vq, FALSE);
        VIOSerialEnableDisableInterruptQueue(vport.out_vq, FALSE);

        if(vport.GuestConnected)
        {
           VIOSerialSendCtrlMsg(vport.BusDevice, vport.Id, VIRTIO_CONSOLE_PORT_OPEN, 0);
        }

        VIOSerialDiscardPortData(&vport);
        VIOSerialReclaimConsumedBuffers(&vport);
        while (buf = VirtIODeviceDetachUnusedBuf(vport.in_vq))
        {
           VIOSerialFreeBuffer(buf);
        }
    }
    WdfChildListEndIteration(list, &iterator);

    if(pContext->isHostMultiport)
    {
        if(pContext->c_ivq)
        {
            pContext->c_ivq->vq_ops->shutdown(pContext->c_ivq);
            VirtIODeviceDeleteVirtualQueue(pContext->c_ivq);
            pContext->c_ivq = NULL;
        }
        if(pContext->c_ovq)
        {
            pContext->c_ovq->vq_ops->shutdown(pContext->c_ovq);
            VirtIODeviceDeleteVirtualQueue(pContext->c_ovq);
            pContext->c_ovq = NULL;
        }
    }

    nr_ports = pContext->consoleConfig.max_nr_ports - 1;
    for (i = 0; i < nr_ports; i++ )
    {
        if(pContext->in_vqs && pContext->in_vqs[i])
        {
            pContext->in_vqs[i]->vq_ops->shutdown(pContext->in_vqs[i]);
            VirtIODeviceDeleteVirtualQueue(pContext->in_vqs[i]);
            pContext->in_vqs[i] = NULL;
        }

        if(pContext->out_vqs && pContext->out_vqs[i])
        {
            pContext->out_vqs[i]->vq_ops->shutdown(pContext->out_vqs[i]);
            VirtIODeviceDeleteVirtualQueue(pContext->out_vqs[i]);
            pContext->out_vqs[i] = NULL;
        }
    }
}

VOID
VIOSerialInitPortConsole(
    IN PVIOSERIAL_PORT port
)
{
    PPORT_BUFFER    buf;
    PPORTS_DEVICE   pContext = GetPortsDevice(port->BusDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    port->GuestConnected = TRUE;
    VIOSerialSendCtrlMsg(port->BusDevice, port->Id, VIRTIO_CONSOLE_PORT_OPEN, 1);
}

VOID
VIOSerialDiscardPortData(
    IN PVIOSERIAL_PORT port
)
{
    struct virtqueue *vq;
    PPORT_BUFFER buf;
    UINT len;
    PPORTS_DEVICE pContext = GetPortsDevice(port->BusDevice);
    NTSTATUS  status = STATUS_SUCCESS;
    UINT ret = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = port->in_vq;

    if (port->InBuf)
    {
        buf = port->InBuf;
    }
    else
    {
        buf = vq->vq_ops->get_buf(vq, &len);
    }

    while (buf)
    {
        status = VIOSerialAddInBuf(vq, buf); 
        if(!NT_SUCCESS(status))
        {
           ++ret;
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding buffer to queue\n", __FUNCTION__, __LINE__);
           VIOSerialFreeBuffer(buf);  
        }
        buf = vq->vq_ops->get_buf(vq, &len);
    }
    port->InBuf = NULL;
    if (ret > 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding %u buffers back to queue\n",
                      __FUNCTION__, __LINE__, ret);
    }
}

BOOLEAN
VIOSerialPortHasData(
    IN PVIOSERIAL_PORT port
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(port->InBufLock);
    if (port->InBuf) 
    {
        WdfSpinLockRelease(port->InBufLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }
    port->InBuf = VIOSerialGetInBuf(port);
    if (port->InBuf) 
    {
        WdfSpinLockRelease(port->InBufLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }
    WdfSpinLockRelease(port->InBufLock);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
    return FALSE;
}

BOOLEAN
VIOSerialWillReadBlock(
    IN PVIOSERIAL_PORT port
)
{
    return !VIOSerialPortHasData(port) && port->HostConnected;
}

BOOLEAN
VIOSerialWillWriteBlock(
    IN PVIOSERIAL_PORT port
)
{
    BOOLEAN ret = FALSE;

    if (!port->HostConnected)
    {
        return TRUE;
    }

    WdfSpinLockAcquire(port->OutVqLock);
    VIOSerialReclaimConsumedBuffers(port);
    ret = port->OutVqFull;
    WdfSpinLockRelease(port->OutVqLock);
    return ret;
}

VOID
VIOSerialPortSendPortReady (
    IN WDFWORKITEM  WorkItem
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WorkItem);
    PVIOSERIAL_PORT         pport = pdoData->port;

    if(!VIOSerialFindPortById(pport->BusDevice, pport->Id))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s re-enqueue work item for id=%d\n",
        __FUNCTION__, pport->Id);
        WdfWorkItemEnqueue(WorkItem);
        return;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s sending PORT_READY for id=%d\n",
        __FUNCTION__, pport->Id);
    VIOSerialSendCtrlMsg(pport->BusDevice, pport->Id, VIRTIO_CONSOLE_PORT_READY, 1);
}

NTSTATUS
VIOSerialDeviceListCreatePdo(
    IN WDFCHILDLIST DeviceList,
    IN PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    IN PWDFDEVICE_INIT ChildInit
    )
{
    PVIOSERIAL_PORT pport;
    NTSTATUS  status = STATUS_SUCCESS;

    WDFDEVICE                       hChild = NULL;

    WDF_OBJECT_ATTRIBUTES           attributes;
    WDF_DEVICE_PNP_CAPABILITIES     pnpCaps;
    WDF_DEVICE_STATE                deviceState;
    WDF_IO_QUEUE_CONFIG             queueConfig;
    PRAWPDO_VIOSERIAL_PORT          rawPdo = NULL;
    WDF_FILEOBJECT_CONFIG           fileConfig;
    PPORTS_DEVICE                   pContext = NULL;

    // Work item to send PORT_READY when successfull
    WDF_WORKITEM_CONFIG             workitemConfig;
    WDFWORKITEM                     hWorkItem;
    PRAWPDO_VIOSERIAL_PORT          pdoData = NULL;

    DECLARE_CONST_UNICODE_STRING(deviceId, PORT_DEVICE_ID );
    DECLARE_CONST_UNICODE_STRING(deviceLocation, L"RedHat VIOSerial Port" );

    DECLARE_UNICODE_STRING_SIZE(buffer, DEVICE_DESC_LENGTH);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    pport = CONTAINING_RECORD(IdentificationDescription,
                                 VIOSERIAL_PORT,
                                 Header
                                 );

    WdfDeviceInitSetDeviceType(ChildInit, FILE_DEVICE_SERIAL_PORT);
    WdfDeviceInitSetIoType(ChildInit, WdfDeviceIoDirect);

    do
    {

        status = RtlUnicodeStringPrintf(
                                 &buffer,
                                 L"%ws%vport%up%u",
                                 L"\\Device\\",
                                 0,
                                 pport->Id
                                 );

        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                                 "RtlUnicodeStringPrintf failed 0x%x\n", status
                                 );
           break;
        }

        status = WdfDeviceInitAssignName(ChildInit,&buffer);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                                 "WdfDeviceInitAssignName failed %ws 0x%x\n",
                                 status,
                                 buffer
                                 );
           break;
        }

        WdfDeviceInitSetExclusive(ChildInit, TRUE);
        status = WdfPdoInitAssignRawDevice(ChildInit, &GUID_DEVCLASS_PORT_DEVICE);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignRawDevice failed - 0x%x\n", status);
           break;
        }

        status = WdfDeviceInitAssignSDDLString(ChildInit, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceInitAssignSDDLString failed - 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignDeviceID failed - 0x%x\n", status);
           break;
        }

        status = RtlUnicodeStringPrintf(
                                 &buffer,
                                 L"%04d", 
                                 pport->Id
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "RtlUnicodeStringPrintf failed - 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAddHardwareID(ChildInit, &buffer);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAddHardwareID failed - 0x%x\n", status);
           break;
        }

        status = RtlUnicodeStringPrintf(
                                 &buffer, 
                                 L"%02d", 
                                 pport->Id
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "RtlUnicodeStringPrintf failed - 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAssignInstanceID(ChildInit, &buffer);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignInstanceID failed - 0x%x\n", status);
           break;
        }

        status = RtlUnicodeStringPrintf(
                                 &buffer,
                                 L"vport%up%u",
                                 0,
                                 pport->Id
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlUnicodeStringPrintf failed 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAddDeviceText(
                                 ChildInit,
                                 &buffer,
                                 &deviceLocation,
                                 0x409
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfPdoInitAddDeviceText failed 0x%x\n", status);
           break;
        }

        WdfPdoInitSetDefaultLocale(ChildInit, 0x409);

        WDF_FILEOBJECT_CONFIG_INIT(
                                 &fileConfig,
                                 VIOSerialPortCreate,
                                 VIOSerialPortClose,
                                 WDF_NO_EVENT_CALLBACK
                                 );

        WdfDeviceInitSetFileObjectConfig(
                                 ChildInit,
                                 &fileConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES 
                                 );

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);

        status = WdfDeviceCreate(
                                 &ChildInit,
                                 &attributes,
                                 &hChild
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreate failed 0x%x\n", status);
           break;
        }

        rawPdo = RawPdoSerialPortGetData(hChild);
        rawPdo->port = pport;
        pport->Device = hChild;

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
                                 WdfIoQueueDispatchSequential
                                 );

        queueConfig.EvtIoDeviceControl = VIOSerialPortDeviceControl;
        status = WdfIoQueueCreate(hChild,
                                 &queueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->IoctlQueue
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate failed (IoCtrl Queue): 0x%x\n", status);
           break;
        }
        status = WdfDeviceConfigureRequestDispatching(
                                 hChild,
                                 pport->IoctlQueue,
                                 WdfRequestTypeDeviceControl
                                 );

        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "DeviceConfigureRequestDispatching failed (IoCtrl Queue): 0x%x\n", status);
           break;
        }

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
                                 WdfIoQueueDispatchSequential);

        queueConfig.EvtIoRead   =  VIOSerialPortRead;
        status = WdfIoQueueCreate(hChild,
                                 &queueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->ReadQueue
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate (Read Queue) failed 0x%x\n", status);
           break;
        }

        status = WdfDeviceConfigureRequestDispatching(
                                 hChild,
                                 pport->ReadQueue,
                                 WdfRequestTypeRead
                                 );

        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "DeviceConfigureRequestDispatching failed (Read Queue): 0x%x\n", status);
           break;
        }

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
                                 WdfIoQueueDispatchManual);

        status = WdfIoQueueCreate(hChild,
                                 &queueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->PendingReadQueue
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate (Pending Read Queue) failed 0x%x\n", status);
           break;
        }

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
                                 WdfIoQueueDispatchSequential);

        queueConfig.EvtIoWrite  =  VIOSerialPortWrite;
        status = WdfIoQueueCreate(hChild,
                                 &queueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->WriteQueue
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate failed (Write Queue): 0x%x\n", status);
           break;
        }
        status = WdfDeviceConfigureRequestDispatching(
                                 hChild,
                                 pport->WriteQueue,
                                 WdfRequestTypeWrite
                                 );

        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "DeviceConfigureRequestDispatching failed (Write Queue): 0x%x\n", status);
           break;
        }

        WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);

        pnpCaps.NoDisplayInUI    =  WdfTrue;
        pnpCaps.Removable        =  WdfTrue;
        pnpCaps.EjectSupported   =  WdfTrue;
        pnpCaps.SurpriseRemovalOK=  WdfTrue;
        pnpCaps.Address          =  pport->Id;
        pnpCaps.UINumber         =  pport->Id;

        WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

        WDF_DEVICE_STATE_INIT(&deviceState);
        deviceState.DontDisplayInUI = WdfTrue;
        WdfDeviceSetDeviceState(hChild, &deviceState);

        status = WdfDeviceCreateDeviceInterface(
                                 hChild,
                                 &GUID_VIOSERIAL_PORT,
                                 NULL
                                 );

        if (!NT_SUCCESS (status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreateDeviceInterface failed 0x%x\n", status);
           break;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = hChild;
        status = WdfSpinLockCreate(
                                &attributes,
                                &pport->InBufLock
                                );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfSpinLockCreate failed 0x%x\n", status);
           break;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = hChild;
        status = WdfSpinLockCreate(
                                &attributes,
                                &pport->OutVqLock
                                );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfSpinLockCreate failed 0x%x\n", status);
           break;
        }

        pContext = GetPortsDevice(pport->BusDevice);

        pport->WriteTransferElements =  BYTES_TO_PAGES((ULONG) ROUND_TO_PAGES(
                                    pContext->MaximumTransferLength) + PAGE_SIZE) + 2;

        pport->WriteCommonBufferSize =  sizeof(struct VirtIOBufferDescriptor) * pport->WriteTransferElements;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "pport->WriteCommonBufferSize = %d\n", (int)pport->WriteCommonBufferSize);

        status = WdfCommonBufferCreate(
                                 pContext->DmaEnabler,
                                 pport->WriteCommonBufferSize,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->WriteCommonBuffer
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfCommonBufferCreate (write) failed: 0x%x\n", status);
           break;
        }

        pport->WriteCommonBufferBase =
           WdfCommonBufferGetAlignedVirtualAddress(pport->WriteCommonBuffer);

        pport->WriteCommonBufferBaseLA =
           WdfCommonBufferGetAlignedLogicalAddress(pport->WriteCommonBuffer);

        RtlZeroMemory( pport->WriteCommonBufferBase,
                       pport->WriteCommonBufferSize);

        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                                 "WriteCommonBuffer 0x%p   %08I64X, length %d\n",
                                 pport->WriteCommonBufferBase,
                                 pport->WriteCommonBufferBaseLA.QuadPart,
                                 (int)WdfCommonBufferGetLength(pport->WriteCommonBuffer) );

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, TRANSACTION_CONTEXT);
        status = WdfDmaTransactionCreate(pContext->DmaEnabler,
                                 &attributes,
                                 &pport->WriteDmaTransaction
                                 );

        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfDmaTransactionCreate failed: 0x%x\n", status);
           break;
        }

        status = VIOSerialFillQueue(pport->in_vq, pport->InBufLock);
        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"%s::%d  Error allocating inbufs\n", __FUNCTION__, __LINE__);
           break;
        }

        VIOSerialEnableDisableInterruptQueue(pport->in_vq, TRUE);
        VIOSerialEnableDisableInterruptQueue(pport->out_vq, TRUE);

        // schedule a workitem to send PORT_READY, hopefully runs __after__ this function returns.
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);
        attributes.ParentObject = hChild;
        WDF_WORKITEM_CONFIG_INIT(&workitemConfig, VIOSerialPortSendPortReady);

        status = WdfWorkItemCreate( &workitemConfig,
                                 &attributes,
                                 &hWorkItem);

        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                "%s WdfWorkItemCreate failed with status = 0x%08x\n",
                __FUNCTION__, status);
        }
        else
        {
            pdoData = RawPdoSerialPortGetData(hWorkItem);
            pdoData->port = pport;
            WdfWorkItemEnqueue(hWorkItem);
        }
    } while (0);

    if (!NT_SUCCESS(status))
    {
        // We can send this before PDO is PRESENT since the device won't send any response.
        VIOSerialSendCtrlMsg(pport->BusDevice, pport->Id, VIRTIO_CONSOLE_PORT_READY, 0);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s status 0x%x\n", __FUNCTION__, status);
    return status;
}

VOID
VIOSerialPortRead(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(Queue));
    SIZE_T             length;
    NTSTATUS           status;
    PUCHAR             systemBuffer;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "-->%s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &systemBuffer, &length);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    if (!VIOSerialPortHasData(pdoData->port))
    {
        if (!pdoData->port->HostConnected)
        {
           WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
           return;
        }

        status = WdfRequestForwardToIoQueue(Request, pdoData->port->PendingReadQueue);
        if (NT_SUCCESS(status)) 
        {
            return;
        } 
        else 
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: %x\n", status);
           WdfRequestComplete(Request, status);
           return;
        }
    }

    length = (ULONG)VIOSerialFillReadBuf(pdoData->port, systemBuffer, length);
    if (length)
    {
        WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)length);
        return;
    }
    WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
    return;
}


VOID
VIOSerialPortWrite(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
    )
{

    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(Queue));
    NTSTATUS           status = STATUS_SUCCESS;
    SIZE_T             length;
    WDFREQUEST         readRequest;
    PUCHAR             systemBuffer;
    PVIOSERIAL_PORT    pport = pdoData->port;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "-->%s\n", __FUNCTION__);

    if (Length > PORT_MAXIMUM_TRANSFER_LENGTH)
    {
        status = STATUS_INVALID_BUFFER_SIZE;
        WdfDmaTransactionRelease(pport->WriteDmaTransaction);
        WdfRequestComplete(Request, status);
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return;
    }

    status = WdfDmaTransactionInitializeUsingRequest(
                                 pport->WriteDmaTransaction,
                                 Request,
                                 VIOSerialPortProgramWriteDma,
                                 WdfDmaDirectionWriteToDevice
                                 );

    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
                                 "WdfDmaTransactionInitializeUsingRequest failed: 0x%x\n",
                                 status
                                 );
        WdfDmaTransactionRelease(pport->WriteDmaTransaction);
        WdfRequestComplete(Request, status);
        return;
    }

    status = WdfDmaTransactionExecute( pport->WriteDmaTransaction,
                                       pport);

    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
                    "WdfDmaTransactionExecute failed: 0x%x\n", status);
        WdfDmaTransactionRelease(pport->WriteDmaTransaction);
        WdfRequestComplete(Request, status);
    }
}

BOOLEAN
VIOSerialPortProgramWriteDma(
    IN  WDFDMATRANSACTION       Transaction,
    IN  WDFDEVICE               Device,
    IN  PVOID                   Context,
    IN  WDF_DMA_DIRECTION       Direction,
    IN  PSCATTER_GATHER_LIST    SgList
    )
{
    UINT len;
    SSIZE_T ret;
    struct VirtIOBufferDescriptor* sg;
    PVIOSERIAL_PORT port = (PVIOSERIAL_PORT)Context;
    struct virtqueue *vq = port->out_vq;
    ULONG i;

    UNREFERENCED_PARAMETER(Device);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s port->OutVqFull = %d\n", __FUNCTION__, port->OutVqFull);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING, "--> %s port->OutVqFull = %d\n", __FUNCTION__, port->OutVqFull);

    WdfSpinLockAcquire(port->OutVqLock);
    VIOSerialReclaimConsumedBuffers(port);

    sg = (struct VirtIOBufferDescriptor*) port->WriteCommonBufferBase;

    for (i=0; i < SgList->NumberOfElements; i++)
    {
        sg[i].physAddr = SgList->Elements[i].Address;
        sg[i].ulSize   = SgList->Elements[i].Length;
    }

    ret = vq->vq_ops->add_buf(vq, sg, i, 0, Context);

    if (ret < 0)
    {
        NTSTATUS status;
        port->OutVqFull = TRUE;
        WdfSpinLockRelease(port->OutVqLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING, "<--> %s::%d port->OutVqFull = %d\n", __FUNCTION__, __LINE__, port->OutVqFull);

        (VOID) WdfDmaTransactionDmaCompletedFinal(Transaction, 0, &status);
        ASSERT(NT_SUCCESS(status));
        VIOSerialPortWriteRequestComplete( Transaction, STATUS_INVALID_DEVICE_STATE );
        return FALSE;
    }

    vq->vq_ops->kick(vq);
    WdfSpinLockRelease(port->OutVqLock);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s port->OutVqFull = %d\n", __FUNCTION__, port->OutVqFull);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "<-- %s port->OutVqFull = %d\n", __FUNCTION__, port->OutVqFull);
    return TRUE;
}

VOID
VIOSerialPortWriteRequestComplete(
    IN WDFDMATRANSACTION  DmaTransaction,
    IN NTSTATUS           Status
    )
{
    WDFDEVICE          device= WdfDmaTransactionGetDevice(DmaTransaction);
    PRAWPDO_VIOSERIAL_PORT   pdoData = RawPdoSerialPortGetData(device);
    WDFREQUEST         request;
    size_t             bytesTransferred;

    request = WdfDmaTransactionGetRequest(DmaTransaction);

    bytesTransferred =  WdfDmaTransactionGetBytesTransferred( DmaTransaction );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                                "%s:  Request %p, Status 0x%x, "
                                "bytes transferred %d\n",
                                 __FUNCTION__,
                                 request,
                                 Status,
                                 (int)bytesTransferred
                                 );

    WdfDmaTransactionRelease(DmaTransaction);

    WdfRequestCompleteWithInformation( request, Status, bytesTransferred);

}

VOID
VIOSerialPortDeviceControl(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     OutputBufferLength,
    IN size_t     InputBufferLength,
    IN ULONG      IoControlCode
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(Queue));
    size_t                  length = 0;
    NTSTATUS                status = STATUS_SUCCESS;
    PVIRTIO_PORT_INFO       pport_info = NULL;
    size_t                  name_size = 0;

    PAGED_CODE();

    UNREFERENCED_PARAMETER( InputBufferLength  );
    UNREFERENCED_PARAMETER( OutputBufferLength  );

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "-->%s\n", __FUNCTION__);

    switch (IoControlCode)
    {

        case IOCTL_GET_INFORMATION:
        {
           status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VIRTIO_PORT_INFO), &pport_info, &length);
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "WdfRequestRetrieveInputBuffer failed 0x%x\n", status);
              WdfRequestComplete(Request, status);
              return;
           }
           if (pdoData->port->NameString.Buffer)
           {
              name_size = pdoData->port->NameString.MaximumLength;
           }
           if (length < sizeof (VIRTIO_PORT_INFO) + name_size)
           {
              status = STATUS_BUFFER_OVERFLOW;
              TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "Buffer too small. get = %d, expected = %d\n", length, sizeof (VIRTIO_PORT_INFO) + name_size);
              length = sizeof (VIRTIO_PORT_INFO) + name_size;
              break;
           }
           RtlZeroMemory(pport_info, sizeof(VIRTIO_PORT_INFO));
           pport_info->Id = pdoData->port->Id;
           pport_info->OutVqFull = pdoData->port->OutVqFull;
           pport_info->HostConnected = pdoData->port->HostConnected;
           pport_info->GuestConnected = pdoData->port->GuestConnected;

           if (pdoData->port->NameString.Buffer)
           {
              RtlZeroMemory(pport_info->Name, name_size);
              status = RtlStringCbCopyA(pport_info->Name, name_size - 1, pdoData->port->NameString.Buffer);   
              if (!NT_SUCCESS(status))
              {
                 TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "RtlStringCbCopyA failed 0x%x\n", status);
                 name_size = 0;
              }
           }
           status = STATUS_SUCCESS;
           length =  sizeof (VIRTIO_PORT_INFO) + name_size;
           break;
        }

        default:
           status = STATUS_INVALID_DEVICE_REQUEST;
           break;
    }
    WdfRequestCompleteWithInformation(Request, status, length);
}

VOID
VIOSerialPortCreate (
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfDevice);
    NTSTATUS                status  = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,"%s Port id = %d\n", __FUNCTION__, pdoData->port->Id);
    TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE,"%s Port id = %d\n", __FUNCTION__, pdoData->port->Id);

    WdfSpinLockAcquire(pdoData->port->InBufLock);
    if (pdoData->port->GuestConnected == TRUE)
    {
        WdfSpinLockRelease(pdoData->port->InBufLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,"Guest already connected Port id = %d\n", pdoData->port->Id);
        status = STATUS_OBJECT_NAME_EXISTS;
    }
    else
    {
        pdoData->port->GuestConnected = TRUE;
        WdfSpinLockRelease(pdoData->port->InBufLock);

        WdfSpinLockAcquire(pdoData->port->OutVqLock);
        VIOSerialReclaimConsumedBuffers(pdoData->port);
        WdfSpinLockRelease(pdoData->port->OutVqLock);

        VIOSerialSendCtrlMsg(pdoData->port->BusDevice, pdoData->port->Id, VIRTIO_CONSOLE_PORT_OPEN, 1);
    }
    WdfRequestComplete(Request, status);

    return;
}

VOID
VIOSerialPortClose (
    IN WDFFILEOBJECT    FileObject
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfFileObjectGetDevice(FileObject));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    VIOSerialSendCtrlMsg(pdoData->port->BusDevice, pdoData->port->Id, VIRTIO_CONSOLE_PORT_OPEN, 0);

    WdfSpinLockAcquire(pdoData->port->InBufLock);
    pdoData->port->GuestConnected = FALSE;
    VIOSerialDiscardPortData(pdoData->port);
    WdfSpinLockRelease(pdoData->port->InBufLock);

    WdfSpinLockAcquire(pdoData->port->OutVqLock);
    VIOSerialReclaimConsumedBuffers(pdoData->port);
    WdfSpinLockRelease(pdoData->port->OutVqLock);

    return;

}

VOID
VIOSerialPortCreateName (
    IN WDFDEVICE WdfDevice,
    IN PVIOSERIAL_PORT port,
    IN PPORT_BUFFER buf
    )
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG   workitemConfig;
    WDFWORKITEM           hWorkItem;
    PRAWPDO_VIOSERIAL_PORT  pdoData = NULL;
    NTSTATUS              status = STATUS_SUCCESS;
    size_t                length;
    PVIRTIO_CONSOLE_CONTROL cpkt;

    cpkt = (PVIRTIO_CONSOLE_CONTROL)((ULONG_PTR)buf->va_buf + buf->offset);
    if (port && !port->NameString.Buffer)
    {
        length = buf->len - buf->offset - sizeof(VIRTIO_CONSOLE_CONTROL);
        port->NameString.Length = (USHORT)( length );
        port->NameString.MaximumLength = port->NameString.Length + 1;
        port->NameString.Buffer = (PCHAR)ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 port->NameString.MaximumLength,
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
        if (port->NameString.Buffer)
        {
           RtlCopyMemory(  port->NameString.Buffer,
                                 (PVOID)((LONG_PTR)buf->va_buf + buf->offset + sizeof(*cpkt)),
                                 length
                                 );
           port->NameString.Buffer[length] = '\0';
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "VIRTIO_CONSOLE_PORT_NAME name_size = %d %s\n", length, port->NameString.Buffer);
        }
        else
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                                 "VIRTIO_CONSOLE_PORT_NAME: Unable to alloc string buffer\n"
                                 );
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);
        attributes.ParentObject = WdfDevice;
        WDF_WORKITEM_CONFIG_INIT(&workitemConfig, VIOSerialPortCreateSymbolicName);

        status = WdfWorkItemCreate( &workitemConfig,
                                 &attributes,
                                 &hWorkItem);

        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "WdfWorkItemCreate failed with status = 0x%08x\n", status);
           return;
        }

        pdoData = RawPdoSerialPortGetData(hWorkItem);

        pdoData->port = port;

        WdfWorkItemEnqueue(hWorkItem);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIRTIO_CONSOLE_PORT_NAME invalid id = %d\n", cpkt->id);
    }
}


VOID
VIOSerialPortCreateSymbolicName (
    IN WDFWORKITEM  WorkItem
    )
{

    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WorkItem);
    PVIOSERIAL_PORT         pport = pdoData->port;
    UNICODE_STRING          deviceUnicodeString = {0};
    NTSTATUS                status  = STATUS_SUCCESS;

    DECLARE_UNICODE_STRING_SIZE(symbolicLinkName, 256);

    do
    {
        if (pport->NameString.Buffer)
        {
           status = RtlAnsiStringToUnicodeString( &deviceUnicodeString,
                                 &pport->NameString,
                                 TRUE
                                 );
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlAnsiStringToUnicodeString failed 0x%x\n", status);
              break;
           }

           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"deviceUnicodeString = %ws\n", deviceUnicodeString.Buffer);

           status = RtlUnicodeStringPrintf(
                                 &symbolicLinkName,
                                 L"%ws%ws",
                                 L"\\DosDevices\\",
                                 deviceUnicodeString.Buffer
                                 );
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlUnicodeStringPrintf failed 0x%x\n", status);
              break;
           }

           status = WdfDeviceCreateSymbolicLink(
                                 pport->Device,
                                 &symbolicLinkName
                                 );
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreateSymbolicLink %ws failed 0x%x\n", status, &symbolicLinkName);
              break;
           }
        }
    } while (0);

    if (deviceUnicodeString.Buffer != NULL)
    {
        RtlFreeUnicodeString( &deviceUnicodeString );
    }
    WdfObjectDelete(WorkItem);
}

NTSTATUS
VIOSerialEvtChildListIdentificationDescriptionDuplicate(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER SourceIdentificationDescription,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER DestinationIdentificationDescription
    )
{
    PVIOSERIAL_PORT src, dst;
    size_t safeMultResult;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceList);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    src = CONTAINING_RECORD(SourceIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);
    dst = CONTAINING_RECORD(DestinationIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);

    dst->BusDevice = src->BusDevice;
    dst->Device = src->Device;

    dst->InBuf = src->InBuf;
    dst->in_vq = src->in_vq;
    dst->out_vq = src->out_vq;
    dst->InBufLock = src->InBufLock;
    dst->OutVqLock = src->OutVqLock;

    dst->NameString.Length = src->NameString.Length;
    dst->NameString.MaximumLength = src->NameString.MaximumLength;
    if (dst->NameString.Length)
    {
        dst->NameString.Buffer = (PCHAR)ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 dst->NameString.MaximumLength,
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
        if (!dst->NameString.Buffer)
        {
           ASSERT(0);
           return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(dst->NameString.Buffer,
                                 src->NameString.Buffer,
                                 dst->NameString.MaximumLength
                                 );
    }

    dst->Id = src->Id;

    dst->OutVqFull = src->OutVqFull;
    dst->HostConnected = src->HostConnected;
    dst->GuestConnected = src->GuestConnected;

    dst->ReadQueue = src->ReadQueue;
    dst->PendingReadQueue = src->PendingReadQueue;

    dst->WriteQueue = src->WriteQueue;
    dst->WriteCommonBuffer = src->WriteCommonBuffer;
    dst->WriteDmaTransaction = src->WriteDmaTransaction;
    dst->WriteTransferElements = src->WriteTransferElements;
    dst->WriteCommonBufferSize = src->WriteCommonBufferSize;
    dst->WriteCommonBufferBase = src->WriteCommonBufferBase;
    dst->WriteCommonBufferBaseLA = src->WriteCommonBufferBaseLA;

    dst->IoctlQueue = src->IoctlQueue;

    return STATUS_SUCCESS;
}

BOOLEAN
VIOSerialEvtChildListIdentificationDescriptionCompare(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER FirstIdentificationDescription,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER SecondIdentificationDescription
    )
{
    PVIOSERIAL_PORT lhs, rhs;

    UNREFERENCED_PARAMETER(DeviceList);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    lhs = CONTAINING_RECORD(FirstIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);
    rhs = CONTAINING_RECORD(SecondIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);

    return (lhs->Id == rhs->Id);
}

VOID
VIOSerialEvtChildListIdentificationDescriptionCleanup(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription
    )
{
    PVIOSERIAL_PORT pDesc;

    UNREFERENCED_PARAMETER(DeviceList);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    pDesc = CONTAINING_RECORD(IdentificationDescription,
                              VIOSERIAL_PORT,
                              Header);

    if (pDesc->NameString.Buffer)
    {
       ExFreePoolWithTag(pDesc->NameString.Buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
       pDesc->NameString.Buffer = NULL;
       pDesc->NameString.Length = 0;
       pDesc->NameString.MaximumLength = 0;
    }

}
