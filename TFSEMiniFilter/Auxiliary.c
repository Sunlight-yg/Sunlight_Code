#include "Auxiliary.h"

ULONG 
GetProcessNameOffset()
{
	PEPROCESS       curproc;
	int             i;

	curproc = PsGetCurrentProcess();

	for (i = 0; i < 3 * PAGE_SIZE; i++) {

		if (!strncmp("System", (PCHAR)curproc + i, strlen("System"))) {

			return i;
		}
	}

	return 0;
}

void Cc_ClearFileCache(PFILE_OBJECT FileObject, BOOLEAN bIsFlushCache, PLARGE_INTEGER FileOffset, ULONG Length)
{
	BOOLEAN PurgeRes;
	BOOLEAN ResourceAcquired = FALSE;
	BOOLEAN PagingIoResourceAcquired = FALSE;
	PFSRTL_COMMON_FCB_HEADER Fcb = NULL;
	LARGE_INTEGER Delay50Milliseconds = { (ULONG)(-50 * 1000 * 10), -1 };
	IO_STATUS_BLOCK IoStatus = { 0 };

	if ((FileObject == NULL))
	{
		return;
	}

	Fcb = (PFSRTL_COMMON_FCB_HEADER)FileObject->FsContext;
	if (Fcb == NULL)
	{
		return;
	}

Acquire:
	FsRtlEnterFileSystem();

	if (Fcb->Resource)
		ResourceAcquired = ExAcquireResourceExclusiveLite(Fcb->Resource, TRUE);
	if (Fcb->PagingIoResource)
		PagingIoResourceAcquired = ExAcquireResourceExclusive(Fcb->PagingIoResource, FALSE);
	else
		PagingIoResourceAcquired = TRUE;
	if (!PagingIoResourceAcquired)
	{
		if (Fcb->Resource)  ExReleaseResource(Fcb->Resource);
		FsRtlExitFileSystem();
		KeDelayExecutionThread(KernelMode, FALSE, &Delay50Milliseconds);
		goto Acquire;
	}

	if (FileObject->SectionObjectPointer)
	{
		IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);

		if (bIsFlushCache)
		{
			CcFlushCache(FileObject->SectionObjectPointer, FileOffset, Length, &IoStatus);
		}

		if (FileObject->SectionObjectPointer->ImageSectionObject)
		{
			MmFlushImageSection(
				FileObject->SectionObjectPointer,
				MmFlushForWrite
			);
		}

		if (FileObject->SectionObjectPointer->DataSectionObject)
		{
			PurgeRes = CcPurgeCacheSection(FileObject->SectionObjectPointer,
				NULL,
				0,
				FALSE);
		}

		IoSetTopLevelIrp(NULL);
	}

	if (Fcb->PagingIoResource)
		ExReleaseResourceLite(Fcb->PagingIoResource);
	if (Fcb->Resource)
		ExReleaseResourceLite(Fcb->Resource);

	FsRtlExitFileSystem();
}

NTSTATUS 
GetFileInformation(
	__inout PFLT_CALLBACK_DATA		Data,
	__in PCFLT_RELATED_OBJECTS		FltObjects,
	__inout PBOOLEAN				isEncryptFileType,
	__inout PBOOLEAN				isEncrypted,
	__inout PFILE_STANDARD_INFORMATION pFileInfo
)
{
	NTSTATUS				 	 status = STATUS_UNSUCCESSFUL;

	BOOLEAN						 isDir = FALSE;

	PFLT_FILE_NAME_INFORMATION	 pNameInfo = NULL;

	LONGLONG					 offset = 0;

	CHAR						 buffer[ENCRYPT_MARK_LEN] = { 0 };

	PAGED_CODE();

	// �Ƿ����ļ���
	status = FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &isDir);

	if (NT_SUCCESS(status))
	{
		if (isDir)
		{
			return status;
		}
		else
		{
			//	��ȡ�ļ�����Ϣ
			status = FltGetFileNameInformation(Data,
											   FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
											   &pNameInfo);
			if (NT_SUCCESS(status))
			{
				FltParseFileNameInformation(pNameInfo);

				//	�Ƿ��Ǽ�������
				*isEncryptFileType = IsEncryptFileType(&pNameInfo->Extension);

				if (isEncryptFileType)
				{   
					//	��ѯ�ļ���Ϣ
					status = FltQueryInformationFile(FltObjects->Instance,
													 FltObjects->FileObject,
													 pFileInfo,
													 sizeof(FILE_STANDARD_INFORMATION),
													 FileStandardInformation,
													 NULL);
					if (NT_SUCCESS(status))
					{
						offset = pFileInfo->EndOfFile.QuadPart - ENCRYPT_MARK_STRING_LEN;
					
						//	���ļ�ƫ�ƴ��ڱ�ʶ��С
						if (offset >= 0)
						{
							status = FltReadFile(FltObjects->Instance,
												 FltObjects->FileObject,
												 0, // ��ͷ��ʼ��ȡ��ʶ
												 ENCRYPT_MARK_LEN,
												 (PVOID)buffer,
												 //	�����¶�ȡƫ�� | �ǻ����ȡ
												 FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET | FLTFL_IO_OPERATION_NON_CACHED,
												 NULL, NULL, NULL);
							if (NT_SUCCESS(status))
							{
								if (strncmp(buffer, ENCRYPT_MARK_STRING, ENCRYPT_MARK_STRING_LEN) == 0)
								{
									*isEncrypted = TRUE;
								}
							}
						}
					}
				}
				/*else
				{
					KdPrint(("pNameInfo->Extension: %wZ", pNameInfo->Extension));
				}*/
			}
			else
			{
				KdPrint(("FltGetFileNameInformation fail."));
			}
		}
	}

	return status;
}

BOOLEAN 
IsEncryptFileType(PUNICODE_STRING pType)
{

	UNICODE_STRING encryptType = RTL_CONSTANT_STRING(L"txt");

	if (RtlCompareUnicodeString(pType, &encryptType, FALSE) == 0)
	{
		return TRUE;
	}

	return FALSE;
}

PCHAR
GetCurrentProcessName(ULONG Offset)
{
	PEPROCESS       curproc;

	char            *nameptr;

	if (Offset) 
	{
		curproc = PsGetCurrentProcess();
		nameptr = (PCHAR)curproc + Offset;
	}
	else 
	{
		nameptr = "";
	}

	return nameptr;
}

NTSTATUS 
EncryptFile(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PFILE_STANDARD_INFORMATION	fileInfo
)
{
	NTSTATUS					status = STATUS_UNSUCCESSFUL;

	LONGLONG					EndOfFile = 0;

	ULONG						writeLen = 0;

	ULONG						readLen = 0;

	LARGE_INTEGER				offset = { 0 };

	LARGE_INTEGER				offsetAddEncStrLen = { 0 };

	PVOID						buffer = NULL;

	PMDL						pMdl = NULL;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(Data);


	EndOfFile = fileInfo->EndOfFile.QuadPart;

	buffer = ExAllocatePoolWithTag(NonPagedPool,
								   ENCRYPT_MARK_LEN,
								   BUFFER_TAG);
	if (buffer == NULL)
	{
		KdPrint(("EncryptFile: ExAllocatePoolWithTag fail."));
		return status;
	}

	pMdl = IoAllocateMdl(buffer,
						 ENCRYPT_MARK_LEN,
						 FALSE, FALSE, NULL);
	if (pMdl == NULL)
	{
		KdPrint(("EncryptFile: IoAllocateMdl fail."));
		ExFreePool(buffer);
		return status;
	}

	MmBuildMdlForNonPagedPool(pMdl);

	RtlZeroMemory(buffer, ENCRYPT_MARK_LEN);

	while (offset.QuadPart < EndOfFile)
	{
		//	��ȡ�ļ����ݵ�������
		status = FltReadFile(FltObjects->Instance,
							 FltObjects->FileObject,
							 &offset,
							 ENCRYPT_MARK_LEN,
							 buffer,
							 FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET | FLTFL_IO_OPERATION_NON_CACHED,
							 &readLen, NULL, NULL);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("EncryptFile: FltReadFile fail."));
			goto free;
		}

		//	���ܻ�����
		status = EncryptData(buffer, 0, readLen);
		if (!NT_SUCCESS(status))
		{
			goto free;
		}

		//	���ļ�����д�뵽����ͷ֮��
		offsetAddEncStrLen.QuadPart = offset.QuadPart + ENCRYPT_MARK_STRING_LEN;

		//	д������������ļ�
		status = FltWriteFile(FltObjects->Instance,
							  FltObjects->FileObject,
							  &offsetAddEncStrLen,
							  readLen,
							  buffer,
							  FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET | FLTFL_IO_OPERATION_NON_CACHED,
							  &writeLen, NULL, NULL);
		if (readLen != writeLen)
		{
			KdPrint(("EncryptFile: FltWriteFile readLen != writeLen."));
		}

		if (!NT_SUCCESS(status))
		{
			KdPrint(("EncryptFile: FltWriteFile fail."));
			goto free;
		}

		offset.QuadPart += readLen;
		offsetAddEncStrLen.QuadPart = offset.QuadPart + ENCRYPT_MARK_STRING_LEN;
	}

	RtlCopyMemory(buffer, ENCRYPT_MARK_STRING, ENCRYPT_MARK_STRING_LEN);

	// д�����ͷ
	status = FltWriteFile(FltObjects->Instance,
						  FltObjects->FileObject,
						  0,
						  (ULONG)ENCRYPT_MARK_STRING_LEN,
						  buffer,
						  FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET | FLTFL_IO_OPERATION_NON_CACHED,
						  &writeLen, NULL, NULL);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("EncryptFile: Flags write fail."));
		goto free;
	}

	KdPrint(("EncryptFile: Flags write successful."));


free:
	
	if (buffer != NULL)
	{
		ExFreePool(buffer);
	}

	if (pMdl != NULL)
	{
		IoFreeMdl(pMdl);
	}

	return status;
}

NTSTATUS EncryptData(__inout PVOID pBuffer, __in ULONG offset, __in ULONG len)
{
	PCHAR				pChar = (PCHAR)pBuffer;

	__try {
		for (ULONG i = offset; i < len; i++)
		{
			pChar[i] ^= 77;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		KdPrint(("EncryptData: fail."));
		return STATUS_UNSUCCESSFUL;
	}
	
	return STATUS_SUCCESS;
}

NTSTATUS DecodeData(__inout PVOID pBuffer, __in ULONG offset, __in LONGLONG len)
{
	PCHAR				pChar = (PCHAR)pBuffer;

	__try {
		for (ULONG i = offset; i < len; i++)
		{
			pChar[i] ^= 77;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		KdPrint(("DecodeData: fail."));
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}
