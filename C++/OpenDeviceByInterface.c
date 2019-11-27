#include <Windows.h>
#include <stdio.h>
#include <initguid.h>
#include <SetupAPI.h>

#pragma comment (lib, "setupapi.lib")
#define IOCTL_TEST CTL_CODE(FILE_DEVICE_UNKNOWN, 0X800, METHOD_BUFFERED, FILE_ANY_ACCESS)

DEFINE_GUID(DEVICEINTERFACE,
	0xaf9d7207, 0xeac, 0x4d2c, 0x90, 0xe4, 0x7d, 0xcf, 0x84, 0x79, 0xec, 0xa9);

// ��ȡ�豸·��
LPTSTR GetDevicePathByInterface()
{
	HDEVINFO hInfo = NULL;
	SP_DEVICE_INTERFACE_DATA ifData = { 0 };
	DWORD dwSize = 0;
	PSP_DEVICE_INTERFACE_DETAIL_DATA pDetailData = NULL;

	// �����豸�ӿڷ����豸��Ϣ�����
	hInfo = SetupDiGetClassDevs(&DEVICEINTERFACE,
		NULL,
		NULL,
		DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hInfo == NULL)
	{
		return NULL;
	}

	ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	// ö���豸��Ϣ���а������豸�ӿ�
	if (!SetupDiEnumDeviceInterfaces(hInfo, NULL, &DEVICEINTERFACE, 0, &ifData))
	{
		printf("SetupDiEnumDeviceInterfaces: ö��ʧ��, %d��", GetLastError());
		return NULL;
	}

	// ���ر�����ϸ��Ϣ�ṹ����Ĵ�С
	SetupDiGetDeviceInterfaceDetail(hInfo, &ifData, NULL, 0, &dwSize, NULL);

	pDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(dwSize);
	pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	// ��ȡ�豸��ϸ��Ϣ
	if (!SetupDiGetDeviceInterfaceDetail(hInfo, &ifData, pDetailData, dwSize, NULL, NULL))
	{
		printf("SetupDiGetDeviceInterfaceDetail: ��ȡ�豸��ϸ��Ϣʧ��, %d��", GetLastError());
		return NULL;
	}

	return pDetailData->DevicePath;
}


//int main()
//{
//	LPTSTR DevicePath = GetDevicePath();
//
//	if (DevicePath != NULL)
//	{
//		printf("%s\n", DevicePath);
//		HANDLE hDevice = CreateFile(DevicePath,
//									GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE,
//									FILE_SHARE_WRITE | FILE_SHARE_READ,
//									NULL,
//									OPEN_EXISTING,
//									FILE_ATTRIBUTE_NORMAL,
//									NULL);
//		if (hDevice == INVALID_HANDLE_VALUE)
//		{
//			printf("File open failed!%d\n", GetLastError());
//			return -1;
//		}
//
//		DWORD dwRet;
//		char n = getchar();
//		char buffer[4] = { 0 };
//
//		DeviceIoControl(hDevice, IOCTL_TEST, &n, sizeof(n), buffer, sizeof(buffer), &dwRet, NULL);
//		printf("%s", buffer);
//
//		CloseHandle(hDevice);
//	}
//	else
//		printf("Get device path failed!\n");
//
//	return 0;
//}