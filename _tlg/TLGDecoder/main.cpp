#include <Windows.h>
#include "TLGDecoder.h"
#include "resource.h"

#define RECORDLIMIT 131072		// һ�δ������ļ�������

HWND hEdit;
CRITICAL_SECTION cs;

struct thread_param
{
	enum {QUEUE_SIZE = 1500};
	PTSTR *queue;
	DWORD front, tail;
	HANDLE hEvent;
	HANDLE hThread;
	bool thread_exit;
};

DWORD WINAPI Thread(PVOID pv);
BOOL CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPreInstance,
					PSTR pCmdLine, int iCmdShow)
{
	DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_MAIN), 0, DlgProc, 0);
	return 0;
}

void AppendMsg(PTSTR szBuffer)
{
	static DWORD dwPos;
	if (0 == szBuffer)
	{
		dwPos = 0;
		SendMessage(hEdit, EM_SETSEL, 0, -1);
		SendMessage(hEdit, EM_REPLACESEL, FALSE, 0);
		return;
	}
	SendMessage(hEdit, EM_SETSEL, (WPARAM)&dwPos, (LPARAM)&dwPos);
	SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM)szBuffer);
	SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM)TEXT("\r\n"));
	SendMessage(hEdit, EM_GETSEL, 0, (LPARAM)&dwPos);
	return;
}

int mycmp(PTSTR src, PTSTR dst)
{
	int i = 0;
	while (src[i]) tolower(src[i++]);
	return lstrcmp(src, dst);
}
//////////////////////////////////////////////////////////////////////////////////
// ����չ����Ŀ¼
// lpszPath		 - ��չ��Ŀ¼
// callback		 - �ص�����
// pcb			 - �ص���������
//////////////////////////////////////////////////////////////////////////////////
typedef int (*CallBack)(struct CB* pcb, PTSTR path);

struct CB
{
	int cnt;
	thread_param* ptp;
	PTSTR filter;
};

int callback(struct CB* pcb, PTSTR path)
{
	int len = lstrlen(path);
	while(len>=0 && path[len-1] != '.') --len;
	if (!lstrcmp(&path[len], pcb->filter))
	{
		while (pcb->ptp[pcb->cnt].front == pcb->ptp[pcb->cnt].tail+1)		// ��������ת��һ��
			pcb->cnt = (pcb->cnt+1)%4;

		EnterCriticalSection(&cs);
		{
			lstrcpy((PTSTR)((PBYTE)*pcb->ptp[pcb->cnt].queue + pcb->ptp[pcb->cnt].tail*MAX_PATH), path);
		
			if (pcb->ptp[pcb->cnt].tail == pcb->ptp[pcb->cnt].front)		// ԭ�ȶ���Ϊ�գ���λ
				SetEvent(pcb->ptp[pcb->cnt].hEvent);

			pcb->ptp[pcb->cnt].tail = (pcb->ptp[pcb->cnt].tail + 1) % pcb->ptp[pcb->cnt].QUEUE_SIZE;// ���¶���
		}
		LeaveCriticalSection(&cs);

		pcb->cnt = (pcb->cnt+1)%4;	// ת��һ���߳�
	}
	return 0;
}

int ExpandDirectory(PTSTR lpszPath, CallBack callback, struct CB* pcb)
{
	static const DWORD MemAllocStep = 1024*MAX_PATH;
	TCHAR			lpFind[MAX_PATH], lpSearch[MAX_PATH], lpPath[MAX_PATH];
	HANDLE			hFindFile;
	WIN32_FIND_DATA FindData;
	int				cnt = 0;

	// Path\*.*
	lstrcpy(lpPath, lpszPath);
	lstrcat(lpPath, TEXT("\\"));
	lstrcpy(lpSearch, lpPath);
	lstrcat(lpSearch, TEXT("*.*"));

	if (INVALID_HANDLE_VALUE != (hFindFile = FindFirstFile(lpSearch, &FindData)))
	{
		do
		{
			// �����ļ���
			lstrcpy(lpFind, lpPath);
			lstrcat(lpFind, FindData.cFileName);

			if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (FindData.cFileName[0] != '.')
					ExpandDirectory(lpFind, callback, pcb);
			}
			else
			{
				callback(pcb, lpFind);
			}
		}while(FindNextFile(hFindFile, &FindData));
		FindClose(hFindFile);
		return 0;
	}
	return -2;
}

//////////////////////////////////////////////////////////////////////////////////
// ����pInBuf(�������ļ�)/pInBuf�µ��������ļ�(������Ŀ¼)������
// pInBuf		 - �������ļ�(չ��Ŀ¼)
// callback		 - �ص�����
// pcb			 - �ص���������
//////////////////////////////////////////////////////////////////////////////////
DWORD AppendFileToQueue(PTSTR pInBuf, CallBack callback, struct CB *pcb)
{	
	if (FILE_ATTRIBUTE_DIRECTORY == GetFileAttributes(pInBuf))
	{
		ExpandDirectory(pInBuf, callback, pcb);
	}
	else
	{
		callback(pcb, pInBuf);
	}
	return 0;
}

void OnDropFiles(HDROP hDrop, HWND hwnd, thread_param* ptp)
{
	struct CB cb;
	TCHAR FileName[MAX_PATH];
	DWORD i;
	DWORD FileNum;

	cb.cnt = 0;
	cb.filter = TEXT("tlg");
	cb.ptp = ptp;

	FileNum  = DragQueryFile(hDrop, -1, NULL, 0);

	for (i=0; i<FileNum; ++i)
	{
		DragQueryFile(hDrop, i, (LPTSTR)FileName, MAX_PATH);
		AppendFileToQueue(FileName, callback, &cb);
	}
	DragFinish(hDrop);

	return;
}

BOOL CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static thread_param tp[4];

	switch (msg)
	{
	case WM_INITDIALOG:
		hEdit = GetDlgItem(hDlg, IDC_EDIT);
		SendMessage(hEdit, EM_LIMITTEXT, -1, 0);
		AppendMsg(TEXT("�Ϸ�tlg�ļ����˴�..."));

		for (int i=0; i<4; ++i)
		{
			if (!(tp[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
			{
				AppendMsg(TEXT("�¼���ʼ������"));
				EndDialog(hDlg, 0);
			}
			if (!(tp[i].queue = (PTSTR*)VirtualAlloc(NULL, sizeof(PTSTR*), MEM_COMMIT, PAGE_READWRITE)))
			{
				AppendMsg(TEXT("�ڴ�������"));
				EndDialog(hDlg, 0);
			}
			if (!(*(tp[i].queue) = (PTSTR)VirtualAlloc(NULL, tp[i].QUEUE_SIZE*MAX_PATH, MEM_COMMIT, PAGE_READWRITE)))
			{
				AppendMsg(TEXT("�ڴ�������"));
				EndDialog(hDlg, 0);
			}
			if (!(tp[i].hThread = CreateThread(NULL, 0, Thread, &tp[i], 0, NULL)))
			{
				AppendMsg(TEXT("�̴߳���ʧ�ܣ�"));
				EndDialog(hDlg, 0);
			}
			tp[i].front = tp[i].tail = 0;
			tp[i].thread_exit = false;
		}
		InitializeCriticalSection(&cs);
		return TRUE;

	case WM_DROPFILES:
		OnDropFiles((HDROP)wParam, hDlg, tp);
		return TRUE;

	case WM_CLOSE:
		for (int i=0; i<4; ++i)
		{
			tp[i].thread_exit = true;
			SetEvent(tp[i].hEvent);
		}

		EndDialog(hDlg, 0);
		return TRUE;
	}
	return FALSE;
}

DWORD WINAPI Thread(PVOID pv)
{
	HANDLE hFile;
	TCHAR szBuffer[MAX_PATH];
	LPTSTR CurrentFile;
	thread_param * ptp = (thread_param*) pv;
	DWORD ByteRead, dwNowProcess = 0, dwFileProcessed = 0;
	int err;

	while (1)
	{
		WaitForSingleObject(ptp->hEvent, INFINITE);

		if (ptp->thread_exit) break;

		CurrentFile = (PTSTR)((PBYTE)*ptp->queue + ptp->front*MAX_PATH);
		hFile = CreateFile(CurrentFile, GENERIC_READ, FILE_SHARE_READ,
							NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			wsprintf(szBuffer, TEXT("[%d]�޷����ļ�%s������"), ptp->hThread, CurrentFile);
			MessageBox(0, szBuffer, TEXT("��ʾ"), MB_ICONWARNING);
			++dwNowProcess;
			continue;
		}

		DWORD FileLen = GetFileSize(hFile, NULL);
		PVOID FileBuf = GlobalAlloc(GPTR, FileLen);
		ReadFile(hFile, FileBuf, FileLen, &ByteRead, NULL);

		CloseHandle(hFile);
		
		PVOID p = FileBuf;
		while ((PBYTE)p-(PBYTE)FileBuf < FileLen-5 && memcmp(p, "TLG6.0", 6) && memcmp(p, "TLG5.0", 6))
			p = (PBYTE)p + 1;

		if ((PBYTE)p-(PBYTE)FileBuf >= FileLen-5)
		{
			wsprintf(szBuffer, TEXT("�ļ����Դ�������%s"), CurrentFile);
			MessageBox(0, szBuffer, TEXT("��ʾ"), MB_ICONWARNING);
			GlobalFree(FileBuf);
			++dwNowProcess;
			continue;
		}
//		PVOID p = (unsigned char*)FileBuf + 0xF;	// ȥ��ͷ�� "TLG0.0..." һ��

		DWORD RawSize;
		PVOID *Receive = (PVOID*)malloc(sizeof(PVOID));
		if (!Receive)
		{
			wsprintf(szBuffer, TEXT("�ڴ�����������%s"), CurrentFile);
			MessageBox(0, szBuffer, TEXT("��ʾ"), MB_ICONWARNING);
			GlobalFree(FileBuf);
			++dwNowProcess;
			continue;
		}
		
		*Receive = 0;
		bool tlg5 = false;
		if (!memcmp(p, "TLG6.0", 6))
			DecodeTLG6(p, FileLen-((PBYTE)p-(PBYTE)FileBuf), Receive, &RawSize);
		else if (!memcmp(p, "TLG5.0", 6))
		{
			DecodeTLG5(p, FileLen-((PBYTE)p-(PBYTE)FileBuf), Receive, &RawSize);
			tlg5 = true;
		}

		// �������ļ���
		int len = lstrlen(CurrentFile);
		
		while (CurrentFile[len-1] != '.' && len>0) --len;
		lstrcpy(&CurrentFile[len], TEXT("bmp"));

		hFile = CreateFile(CurrentFile, GENERIC_WRITE, 0, NULL,
									CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		if (hFile == INVALID_HANDLE_VALUE || (err = GetLastError()))
		{
			wsprintf(szBuffer, TEXT("�޷������ļ�%s������"), CurrentFile);
			MessageBox(0, szBuffer, TEXT("��ʾ"), MB_ICONWARNING);
		}
		else
		{
			if (tlg5)
			{// ����TLG5.0�ƺ������⣬��Ҫ������
				*((PBYTE)*Receive+2) = 'B';		// ֱ�ӽ��������"MB\0\0" -> "BM"
				*((PBYTE)*Receive+3) = 'M';
				*((PBYTE)*Receive+4) -= 2;		// �ļ���С��2
				*((PBYTE)*Receive+12) -= 2;		// ͼ������ƫ�Ƽ�2
				WriteFile(hFile, (PBYTE)*Receive+2, RawSize-2, &ByteRead, NULL);
			}
			else
			{
				WriteFile(hFile, *Receive, RawSize, &ByteRead, NULL);
			}
			err = GetLastError();
			CloseHandle(hFile);
			err = GetLastError();
			wsprintf(szBuffer, TEXT("[�ѱ���]%s"), CurrentFile);
			AppendMsg(szBuffer);
			
		}
		if (*Receive) free(*Receive);
		free(Receive);
		GlobalFree(FileBuf);

		EnterCriticalSection(&cs);
		{
			ptp->front = (ptp->front + 1) % ptp->QUEUE_SIZE;
		
			if (ptp->front == ptp->tail)
				ResetEvent(ptp->hEvent);
		}
		LeaveCriticalSection(&cs);
	}
	return 0;
}