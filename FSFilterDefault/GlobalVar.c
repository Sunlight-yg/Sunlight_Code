#include "Launch.h"

PDRIVER_OBJECT g_pstDriverObject = NULL;
PDEVICE_OBJECT g_pstControlDeviceObject = NULL;
FAST_MUTEX g_stAttachLock;