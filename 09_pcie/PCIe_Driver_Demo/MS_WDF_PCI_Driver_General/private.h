
#if !defined(_PCI9656_H_)
#define _PCI9659_H_

//
// The device extension for the device object
//
typedef struct _DEVICE_EXTENSION {

    WDFDEVICE               Device;

    // Following fields are specific to the hardware
    // Configuration

    // HW Resources
    PPCI9656_REGS           Regs;             // Registers address
    PUCHAR                  RegsBase;         // Registers base address
    ULONG                   RegsLength;       // Registers base length

    PUCHAR                  PortBase;         // Port base address
    ULONG                   PortLength;       // Port base length

    PUCHAR                  SRAMBase;         // SRAM base address
    ULONG                   SRAMLength;       // SRAM base length

    PUCHAR                  SRAM2Base;        // SRAM (alt) base address
    ULONG                   SRAM2Length;      // SRAM (alt) base length

    WDFINTERRUPT            Interrupt;     // Returned by InterruptCreate

    union {
        INT_CSR bits;
        ULONG   ulong;
    }                       IntCsr;

    union {
        DMA_CSR bits;
        UCHAR   uchar;
    }                       Dma0Csr;

    union {
        DMA_CSR bits;
        UCHAR   uchar;
    }                       Dma1Csr;

    // DmaEnabler
    WDFDMAENABLER           DmaEnabler;
    ULONG                   MaximumTransferLength;

    // IOCTL handling
    WDFQUEUE                ControlQueue;
    BOOLEAN                 RequireSingleTransfer;

#ifdef SIMULATE_MEMORY_FRAGMENTATION
    PMDL                    WriteMdlChain;
    PMDL                    ReadMdlChain;
#endif

    // Write
    WDFQUEUE                WriteQueue;
    WDFDMATRANSACTION       WriteDmaTransaction;

    ULONG                   WriteTransferElements;
    WDFCOMMONBUFFER         WriteCommonBuffer;
    size_t                  WriteCommonBufferSize;
    _Field_size_(WriteCommonBufferSize) PUCHAR WriteCommonBufferBase;
    PHYSICAL_ADDRESS        WriteCommonBufferBaseLA;  // Logical Address

    // Read
    ULONG                   ReadTransferElements;
    WDFCOMMONBUFFER         ReadCommonBuffer;
    size_t                  ReadCommonBufferSize;
    _Field_size_(ReadCommonBufferSize) PUCHAR ReadCommonBufferBase;
    PHYSICAL_ADDRESS        ReadCommonBufferBaseLA;   // Logical Address

    WDFDMATRANSACTION       ReadDmaTransaction;
    WDFQUEUE                ReadQueue;

    ULONG                   HwErrCount;

}  DEVICE_EXTENSION, *PDEVICE_EXTENSION;

//
// This will generate the function named PLxGetDeviceContext to be use for
// retreiving the DEVICE_EXTENSION pointer.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, PLxGetDeviceContext)
