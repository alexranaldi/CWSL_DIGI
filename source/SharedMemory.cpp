//
// SharedMemory.h - class for working with shared memory on Win32 
//                  (adapted from my old works - comments in czech) 
//
#include <windows.h>
#include <stdio.h>
#include "SharedMemory.h"

///////////////////////////////////////////////////////////////////////////////////////////
// vytvor usek sdilene pameti
BOOL CSharedMemory::Create(LPCTSTR Name, DWORD Length, BOOL WriteAccess)
{char eName[MAX_PATH + 1];
 SECURITY_DESCRIPTOR *pSD;
 SECURITY_ATTRIBUTES *pSA;

 // pokud je jiz vytvoreno/namapovano vrat FALSE
 if (m_MapAddress != NULL) return(FALSE);

 // spocti delku hlavicky
 m_DataLength = Length;
 m_Length     = Length + m_PageSize;

 // pokus se vytvorit file-mapping objekt
 m_MapFile = CreateFileMapping((HANDLE)0xFFFFFFFF, NULL, PAGE_READWRITE, 0, m_Length, Name);
 if ((m_MapFile == NULL) || (GetLastError() ==  ERROR_ALREADY_EXISTS))
 {// nepovedlo se (nebo objekt tohoto jmena uz existuje)
  if (m_MapFile != NULL) CloseHandle(m_MapFile);
  m_MapFile = NULL;
  m_Length = m_DataLength = 0;
  return(FALSE);
 }

 // namapuj ho do pameti
 m_MapAddress = MapViewOfFile(m_MapFile, FILE_MAP_WRITE, 0, 0, 0);
 if (m_MapAddress == NULL)
 {// nepovedlo se
  CloseHandle(m_MapFile);
  m_MapFile = NULL;
  m_Length = m_DataLength = 0;
  return(FALSE);
 }

 // inicializuj ukazatele na promenne v hlavicce
 m_pLength    = (DWORD *)((PBYTE)m_MapAddress + sizeof(SM_HDR));
 m_pCurrent   = (DWORD *)((PBYTE)m_pLength + sizeof(DWORD));
 m_pLastWrite = (DWORD *)((PBYTE)m_pCurrent + sizeof(DWORD));
 pSA = (SECURITY_ATTRIBUTES *)((PBYTE)m_pLastWrite + sizeof(DWORD));
 pSD = (SECURITY_DESCRIPTOR *)((PBYTE)pSA + sizeof(SECURITY_ATTRIBUTES));

 // pro objekt udalosti na bufferu vytvor bezpecnostni deskriptor
 // povolujici vsechno vsem
 ZeroMemory(pSA, sizeof(SECURITY_ATTRIBUTES));
 ZeroMemory(pSD, SECURITY_DESCRIPTOR_MIN_LENGTH);
 pSA->nLength = sizeof(SECURITY_ATTRIBUTES);
 pSA->lpSecurityDescriptor = pSD;
 pSA->bInheritHandle = TRUE;
 if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
 {
  UnmapViewOfFile(m_MapAddress); m_MapAddress = NULL;
  CloseHandle(m_MapFile);        m_MapFile    = NULL;
  m_Length = m_DataLength = 0;
  return(FALSE);
 }
 if (!SetSecurityDescriptorDacl(pSD, TRUE, (PACL) NULL, FALSE))
 {
  UnmapViewOfFile(m_MapAddress); m_MapAddress = NULL;
  CloseHandle(m_MapFile);        m_MapFile    = NULL;
  m_Length = m_DataLength = 0;
  return(FALSE);
 }

 // vytvor event pro udalosti na bufferu
 sprintf(eName,"e%s",Name);
 m_Event = CreateEvent(pSA, TRUE, FALSE, eName); 
 if ((m_Event == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS))
 {// nepovedlo se (nebo objekt tohoto jmena uz existuje)
  UnmapViewOfFile(m_MapAddress); m_MapAddress = NULL;
  CloseHandle(m_MapFile);        m_MapFile    = NULL;
  m_Length = m_DataLength = 0;
  return(FALSE);
 }
 
 // inicializuj buffer pro zapis
 m_Data          = (LPVOID)((BYTE *)m_MapAddress + m_PageSize);
 (*m_pLength)    = m_DataLength;
 (*m_pCurrent)   = 0L;
 m_Top = m_Current = (PBYTE)m_Data;
 m_Bot           = m_Top + m_DataLength; 
 (*m_pLastWrite) = 0L;

 // nastav pozadovany pristup
 if (!WriteAccess) DenyWriteAccess();
 m_Write = WriteAccess;

 // uspesny navrat
 return(TRUE);
}

///////////////////////////////////////////////////////////////////////////////////////////
// pripoj se na usek sdilene pameti
BOOL CSharedMemory::Open(LPCTSTR Name, BOOL WriteAccess, char *ErrorInfo)
{char eName[MAX_PATH + 1];

 // pro jistotu ...
 if (ErrorInfo != NULL) *ErrorInfo = '\0';
 
 // pokud je jiz vytvoreno/namapovano ...
 if (m_MapAddress != NULL) 
 {// ... vrat FALSE
  if (ErrorInfo != NULL) sprintf(ErrorInfo, "m_MapAddress != NULL");
  return(FALSE);
 }

 // pokus se pripojit na file-mapping objekt
 m_MapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, Name);
 if (m_MapFile == NULL) 
 {// nepovedlo se
  if (ErrorInfo != NULL) sprintf(ErrorInfo, "OpenFileMapping %d", GetLastError());
  return(FALSE);
 }

 // namapuj ho do pameti
 m_MapAddress = MapViewOfFile(m_MapFile, WriteAccess ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
 if (m_MapAddress == NULL)
 {// nepovedlo se
  if (ErrorInfo != NULL) sprintf(ErrorInfo, "MapViewOfFile %d", GetLastError());
  CloseHandle(m_MapFile);
  m_MapFile = NULL;
  return(FALSE);
 }

 // pripoj se na event pro udalosti na bufferu
 sprintf(eName,"e%s",Name);
 m_Event = OpenEvent(EVENT_ALL_ACCESS,FALSE,eName); 
 if (m_Event == NULL)
 {// nepovedlo se 
  if (ErrorInfo != NULL) sprintf(ErrorInfo, "OpenEvent %d", GetLastError());
  UnmapViewOfFile(m_MapAddress); m_MapAddress = NULL;
  CloseHandle(m_MapFile);        m_MapFile    = NULL;
  return(FALSE);
 }

 // inicializuj ukazatele na promenne v hlavicce
 m_pLength    = (DWORD *)((PBYTE)m_MapAddress + sizeof(SM_HDR));
 m_pCurrent   = (DWORD *)((PBYTE)m_pLength + sizeof(DWORD));
 m_pLastWrite = (DWORD *)((PBYTE)m_pCurrent + sizeof(DWORD));

 // inicializuj lokalni ridici promenne bufferu
 m_DataLength = *m_pLength;
 m_Length     = m_DataLength + m_PageSize;
 m_Data       = (LPVOID)((BYTE *)m_MapAddress + m_PageSize);
 m_Top        = (PBYTE)m_Data;
 m_Bot        = m_Top + m_DataLength; 
 m_Current    = m_Top + (*m_pCurrent);
 
 // nastav nejvyssi povoleny pristup
 m_Write = WriteAccess;

 // uspesny navrat
 return(TRUE);
}

///////////////////////////////////////////////////////////////////////////////////////////
// zapis blok dat
BOOL CSharedMemory::Write(PBYTE Ptr, DWORD Len)
{DWORD bLen;
 
 // pokud neni pravo k zapisu, nebo je blok prilis velky, nebo neni inicializovano vraci FALSE
 if ((m_Event == NULL) || (!m_Write) || (Len > m_DataLength)) return(FALSE);

 // spocti kolik muzeme ulozit dat do konce bufferu
 bLen = m_Bot - m_Current;

 // pokud se vejde zapis cele
 if (Len < bLen)
 {CopyMemory((PVOID)m_Current, (CONST VOID *)Ptr, Len);
  m_Current += Len;
 }
  else
 // pokud se nevejde musis to rozdelit na dva bloky
 {// prvni blok
  CopyMemory((PVOID)m_Current, (CONST VOID *)Ptr, bLen);
  Ptr       += bLen;
  Len       -= bLen;
  m_Current  = m_Top;

  // druhy blok
  if (Len > 0)
  {CopyMemory((PVOID)m_Current, (CONST VOID *)Ptr, Len);
   m_Current += Len;
  }
 }

 // aktualizuj ridici promenne ve sdilene pameti
 (*m_pLastWrite) = GetTickCount();
 (*m_pCurrent)   = m_Current - m_Top;

 // oznam, ze prisla nova data
 SetEvent(m_Event);

 // uspesny navrat
 return(TRUE);
}

///////////////////////////////////////////////////////////////////////////////////////////
// precti blok dat
BOOL CSharedMemory::Read(PBYTE Ptr, DWORD Len)
{long dLen;
 DWORD bLen;

 // pokud nejsme inicializovani vrat FALSE
 if (m_Event == NULL) return(FALSE);

 // zjisti kolik je k dispozici dat
 dLen = m_Top + (*m_pCurrent) - m_Current;
 if (dLen < 0) dLen += m_DataLength;

 // pokud je pozadovano vice dat vrat FALSE
 if ((long)Len > dLen) return(FALSE);

 // spocti kolik muzeme precist dat do konce bufferu
 bLen = m_Bot - m_Current;
 
 // pokud lze precist jednofazove, precti
 if (Len < bLen) 
 {CopyMemory((PVOID)Ptr, (CONST VOID *)m_Current, Len);
  m_Current += Len;
 }
  else
 // jinak po blocich
 {// prvni blok
  CopyMemory((PVOID)Ptr, (CONST VOID *)m_Current, bLen);
  Ptr       += bLen;
  Len       -= bLen;
  m_Current  = m_Top;

  // druhy blok
  if (Len > 0)
  {CopyMemory((PVOID)Ptr, (CONST VOID *)m_Current, Len);
   m_Current += Len;
  }
 }

 // uspesny navrat
 return(TRUE);
}
  
///////////////////////////////////////////////////////////////////////////////////////////
// odpoj se od useku sdilene pameti
void CSharedMemory::Close(void)
{
 // pokud je namapovane view zrus ho
 if (m_MapAddress != NULL)
 {UnmapViewOfFile(m_MapAddress);
  m_MapAddress = NULL;
 }

 // pokud je otevreny handle na file-mapping objekt zavri ho
 if (m_MapFile != NULL)
 {CloseHandle(m_MapFile);
  m_MapFile = NULL;
 }

 // pokud je otevreny handle na event zavri ho
 if (m_Event != NULL)
 {CloseHandle(m_Event);
  m_Event = NULL;
 }

 // vynuluj adresu a delku dat
 m_Data = NULL;
 m_Length = m_DataLength = 0;
}


///////////////////////////////////////////////////////////////////////////////////////////
// konstruktor
CSharedMemory::CSharedMemory()
{SYSTEM_INFO SI;

 // inicializuj promenne
 m_MapFile = NULL;
 m_MapAddress = m_Data = NULL;
 m_Length = m_DataLength = 0;
 m_Event = NULL;

 // zjisti velikost pametove stranky
 GetSystemInfo(&SI);
 m_PageSize = SI.dwPageSize;
}
