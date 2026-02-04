#include <ntifs.h>

#define MEMTAG '1WOA'  // 注意：标签是小端存储的，调试时显示为'AOW1'

VOID DriverUnload(PDRIVER_OBJECT driverObject) {
    // 可以在这里添加卸载时的清理代码
    UNREFERENCED_PARAMETER(driverObject);
    DbgPrint("Driver Unloaded\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
    DbgBreakPoint();
    UNREFERENCED_PARAMETER(regPath);

    NTSTATUS status = STATUS_SUCCESS;
    PVOID smallPage = NULL;
    PVOID bigPage = NULL;
    PVOID smallNonPage = NULL;
    PVOID bigNonPage = NULL;

    size_t smallPageSize = 0x80;
    size_t bigPageSize = 0x8000;
    size_t smallNonPageSize = 0x80;
    size_t bigNonPageSize = 0x8000;

    driverObject->DriverUnload = DriverUnload;

    // 分配1：小分页内存
    smallPage = ExAllocatePoolWithTag(PagedPool, smallPageSize, MEMTAG);
    if (!smallPage) {
        DbgPrint("Small Paged Allocation Failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    DbgPrint("Small Paged: 0x%p (Size: 0x%zX)\n", smallPage, smallPageSize);

    // 分配2：大分页内存
    bigPage = ExAllocatePool(PagedPool, bigPageSize);
    if (!bigPage) {
        DbgPrint("Big Paged Allocation Failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    DbgPrint("Big Paged: 0x%p (Size: 0x%zX)\n", bigPage, bigPageSize);  

    // 分配3：小非分页内存
    smallNonPage = ExAllocatePool(NonPagedPool, smallNonPageSize);
    if (!smallNonPage) {
        DbgPrint("Small NonPaged Allocation Failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    DbgPrint("Small NonPaged: 0x%p (Size: 0x%zX)\n", smallNonPage, smallNonPageSize);

    // 分配4：大非分页内存
    bigNonPage = ExAllocatePoolWithTag(NonPagedPool, bigNonPageSize, MEMTAG);
    if (!bigNonPage) {
        DbgPrint("Big NonPaged Allocation Failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    DbgPrint("Big NonPaged: 0x%p (Size: 0x%zX)\n", bigNonPage, bigNonPageSize);

    // 可以在这里使用内存进行测试
    RtlFillMemory(smallPage, smallPageSize, 0xAA);
    RtlFillMemory(bigPage, bigPageSize, 0xBB);

Cleanup:
    // 逆序释放内存（分配顺序的逆序）
    if (bigNonPage) {
        ExFreePoolWithTag(bigNonPage, MEMTAG);
        DbgPrint("Freed Big NonPaged: 0x%p\n", bigNonPage);
    }

    if (smallNonPage) {
        ExFreePool(smallNonPage);
        DbgPrint("Freed Small NonPaged: 0x%p\n", smallNonPage);
    }

    if (bigPage) {
        ExFreePool(bigPage);
        DbgPrint("Freed Big Paged: 0x%p\n", bigPage);
    }

    if (smallPage) {
        ExFreePoolWithTag(smallPage, MEMTAG);
        DbgPrint("Freed Small Paged: 0x%p\n", smallPage);
    }

    if (!NT_SUCCESS(status)) {
        DbgPrint("DriverEntry failed with status: 0x%X\n", status);
    }
    else {
        DbgPrint("DriverEntry completed successfully\n");
    }

    return status;
}