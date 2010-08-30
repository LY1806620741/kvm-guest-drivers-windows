/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
 *
 * File: vioser.h
 * 
 * Author(s):
 * 
 * Main include file 
 * This file contains various routines and globals 
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#if !defined(VIOSERIAL_H)
#define VIOSERIAL_H


EVT_WDF_DRIVER_DEVICE_ADD VIOSerialEvtDeviceAdd;

EVT_WDF_INTERRUPT_ISR                           VIOSerialInterruptIsr;
EVT_WDF_INTERRUPT_DPC                           VIOSerialInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE                        VIOSerialInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE                       VIOSerialInterruptDisable;



#define VIRTIO_SERIAL_MAX_PORTS 31
#define VIRTIO_SERIAL_MAX_QUEUES_COUPLES (VIRTIO_SERIAL_MAX_PORTS + 1)
#define VIRTIO_SERIAL_CONTROL_PORT_INDEX 1

#define VIRTIO_SERIAL_INVALID_INTERRUPT_STATUS 0xFF

#define VIRTIO_CONSOLE_F_SIZE      0
#define VIRTIO_CONSOLE_F_MULTIPORT 1
#define VIRTIO_CONSOLE_BAD_ID      (~(u32)0)


#define VIRTIO_CONSOLE_DEVICE_READY     0
#define VIRTIO_CONSOLE_PORT_ADD         1
#define VIRTIO_CONSOLE_PORT_REMOVE      2
#define VIRTIO_CONSOLE_PORT_READY       3
  
#define VIRTIO_CONSOLE_CONSOLE_PORT     4
#define VIRTIO_CONSOLE_RESIZE           5
#define VIRTIO_CONSOLE_PORT_OPEN        6
#define VIRTIO_CONSOLE_PORT_NAME        7


#define PORT_MAXIMUM_TRANSFER_LENGTH    (32*1024)


#pragma pack (push)
#pragma pack (1)

typedef struct _tagConsoleConfig {
    //* colums of the screens
    u16 cols;
    //* rows of the screens
    u16 rows;
    //* max. number of ports this device can hold
    u32 max_nr_ports;
} CONSOLE_CONFIG, * PCONSOLE_CONFIG;
#pragma pack (pop)


#pragma pack (push)
#pragma pack (1)
typedef struct _tagVirtioConsoleControl {
    u32 id;
    u16 event;
    u16 value;
}VIRTIO_CONSOLE_CONTROL, * PVIRTIO_CONSOLE_CONTROL;
#pragma pack (pop)


typedef struct _tagPortDevice
{
    VirtIODevice        IODevice;

    PHYSICAL_ADDRESS    PortBasePA;
    ULONG               uPortLength;
    PVOID               pPortBase;
    bool                bPortMapped;

    WDFINTERRUPT        WdfInterrupt;

    int                 isHostMultiport;

    CONSOLE_CONFIG      consoleConfig;
    struct virtqueue    *c_ivq, *c_ovq;
    struct virtqueue    **in_vqs, **out_vqs;
    WDFSPINLOCK         CVqLock;

    WDFDMAENABLER       DmaEnabler;
    ULONG               MaximumTransferLength;

    BOOLEAN             DeviceOK;
} PORTS_DEVICE, *PPORTS_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PORTS_DEVICE, GetPortsDevice)

#define VIOSERIAL_DRIVER_MEMORY_TAG (ULONG)'rsIV'

#define  PORT_DEVICE_ID L"{6FDE7521-1B65-48ae-B628-80BE62016026}\\VIOSerialPort\0"

DEFINE_GUID(GUID_DEVCLASS_PORT_DEVICE,
0x6fde7547, 0x1b65, 0x48ae, 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26);
// {6FDE7547-1B65-48ae-B628-80BE62016026}


DEFINE_GUID (GUID_DEVINTERFACE_PORTSENUM_VIOSERIAL,
        0xF55F7844, 0x6A0C, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//  {F55F7844-6A0C-11d2-B841-00C04FAD5171}


#define DEVICE_DESC_LENGTH  128

typedef struct _tagPortBuffer
{
    PHYSICAL_ADDRESS    pa_buf;
    PVOID               va_buf;
    size_t              size;
    size_t              len;
    size_t              offset;
} PORT_BUFFER, * PPORT_BUFFER;

typedef struct _tagVioSerialPort
{
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;

    WDFDEVICE           BusDevice;
    WDFDEVICE           Device;

    PPORT_BUFFER        InBuf;
    struct virtqueue    *in_vq, *out_vq;
    WDFSPINLOCK         InBufLock;
    WDFSPINLOCK         OutVqLock;
    ANSI_STRING         NameString;
    UINT                Id;

    BOOLEAN             OutVqFull;
    BOOLEAN             HostConnected;
    BOOLEAN             GuestConnected;

    WDFQUEUE            ReadQueue;
    WDFQUEUE            PendingReadQueue;

    WDFQUEUE            WriteQueue;
    WDFCOMMONBUFFER     WriteCommonBuffer;
    WDFDMATRANSACTION   WriteDmaTransaction;
    ULONG               WriteTransferElements;
    size_t              WriteCommonBufferSize;
    PUCHAR              WriteCommonBufferBase;
    PHYSICAL_ADDRESS    WriteCommonBufferBaseLA;

    WDFQUEUE            IoctlQueue;
} VIOSERIAL_PORT, *PVIOSERIAL_PORT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSERIAL_PORT, SerialPortGetData)


typedef struct _tagRawPdoVioSerialPort
{
    PVIOSERIAL_PORT port;
} RAWPDO_VIOSERIAL_PORT, *PRAWPDO_VIOSERIAL_PORT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RAWPDO_VIOSERIAL_PORT, RawPdoSerialPortGetData)


typedef struct _tagTransactionContext {
    WDFREQUEST     Request;
} TRANSACTION_CONTEXT, * PTRANSACTION_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TRANSACTION_CONTEXT, RawPdoSerialPortGetTransactionContext)


NTSTATUS 
VIOSerialFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock
);

NTSTATUS 
VIOSerialAddInBuf(
    IN struct virtqueue *vq,
    IN PPORT_BUFFER buf
);

VOID 
VIOSerialReclaimConsumedBuffers(
    IN PVIOSERIAL_PORT port
);

SSIZE_T 
VIOSerialSendBuffers(
    IN PVIOSERIAL_PORT port,
    IN PVOID buf,
    IN SIZE_T count,
    IN BOOLEAN nonblock
);

SSIZE_T 
VIOSerialFillReadBuf(
    IN PVIOSERIAL_PORT port,
    IN PVOID outbuf,
    IN SIZE_T count
);

PPORT_BUFFER 
VIOSerialAllocateBuffer(
    IN size_t buf_size
);

VOID 
VIOSerialFreeBuffer(
    IN PPORT_BUFFER buf
);

VOID 
VIOSerialSendCtrlMsg(
    IN WDFDEVICE hDevice,
    IN ULONG id,
    IN USHORT event,
    IN USHORT value 
);

VOID
VIOSerialCtrlWorkHandler(
    IN WDFDEVICE Device
);

PVIOSERIAL_PORT
VIOSerialFindPortById(
    IN WDFDEVICE Device,
    IN ULONG id
);

VOID
VIOSerialAddPort(
    IN WDFDEVICE Device,
    IN ULONG id
);

VOID
VIOSerialRemovePort(
    IN WDFDEVICE Device,
    IN PVIOSERIAL_PORT port
);

VOID
VIOSerialRemoveAllPorts(
    IN WDFDEVICE Device
);

VOID
VIOSerialRenewAllPorts(
    IN WDFDEVICE Device
);

VOID
VIOSerialShutdownAllPorts(
    IN WDFDEVICE Device
);

VOID
VIOSerialInitPortConsole(
    IN PVIOSERIAL_PORT port
);

VOID
VIOSerialDiscardPortData(
    IN PVIOSERIAL_PORT port
);

BOOLEAN
VIOSerialPortHasData(
    IN PVIOSERIAL_PORT port
);

PVOID
VIOSerialGetInBuf(
    IN PVIOSERIAL_PORT port
);

BOOLEAN
VIOSerialWillWriteBlock(
    IN PVIOSERIAL_PORT port
);

BOOLEAN
VIOSerialWillReadBlock(
    IN PVIOSERIAL_PORT port
);


VOID 
VIOSerialEnableDisableInterruptQueue(
    IN struct virtqueue *vq,
    IN BOOLEAN bEnable
);


EVT_WDF_CHILD_LIST_CREATE_DEVICE VIOSerialDeviceListCreatePdo;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_COMPARE VIOSerialEvtChildListIdentificationDescriptionCompare;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_CLEANUP VIOSerialEvtChildListIdentificationDescriptionCleanup;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_DUPLICATE VIOSerialEvtChildListIdentificationDescriptionDuplicate;
EVT_WDF_IO_QUEUE_IO_READ VIOSerialPortRead;
EVT_WDF_IO_QUEUE_IO_WRITE VIOSerialPortWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VIOSerialPortDeviceControl;
EVT_WDF_DEVICE_FILE_CREATE VIOSerialPortCreate;
EVT_WDF_FILE_CLOSE VIOSerialPortClose;
EVT_WDF_PROGRAM_DMA VIOSerialPortProgramWriteDma;

VOID
VIOSerialPortWriteRequestComplete(
    IN WDFDMATRANSACTION  DmaTransaction,
    IN NTSTATUS           Status
);

VOID
VIOSerialPortCreateName (
    IN WDFDEVICE WdfDevice,
    IN PVIOSERIAL_PORT port,
    IN PPORT_BUFFER buf
);

VOID
VIOSerialPortCreateSymbolicName(
    IN WDFWORKITEM  WorkItem
);

#endif /* VIOSERIAL_H */
