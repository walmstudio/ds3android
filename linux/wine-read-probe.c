#include <windows.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    DWORD n;
    HANDLE h=CreateFileA("fixture.bin",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    assert(h!=INVALID_HANDLE_VALUE && WriteFile(h,"original",8,&n,NULL));
    CloseHandle(h);
    h=CreateFileA("fixture.bin",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    HANDLE e=CreateEventA(NULL,TRUE,FALSE,NULL);
    assert(h!=INVALID_HANDLE_VALUE && e && SetFilePointer(h,2,NULL,FILE_BEGIN)==2);
    char buf[3]={0};
    BOOL ok=ReadFile(h,buf,2,NULL,NULL);
    printf("noov ok=%d err=%lu\n",ok,(unsigned long)GetLastError());
    for(unsigned i=0;i<4;++i) {
        DWORD offsets[]={4,0,7,20};
        OVERLAPPED ov={0}; ov.Offset=offsets[i]; ov.hEvent=e;
        ResetEvent(e); SetLastError(0); n=999;
        ok=ReadFile(h,buf,2,&n,&ov);
        DWORD err=GetLastError(), wait=WaitForSingleObject(e,2000);
        printf("off=%lu ok=%d err=%lu n=%lu wait=%lu status=%08lx count=%lu pos=%lu\n",
            (unsigned long)offsets[i],ok,(unsigned long)err,(unsigned long)n,(unsigned long)wait,
            (unsigned long)ov.Internal,(unsigned long)ov.InternalHigh,(unsigned long)SetFilePointer(h,0,NULL,FILE_CURRENT));
    }
    CloseHandle(e); CloseHandle(h); DeleteFileA("fixture.bin");
}
