#pragma once

//
// SharedMemory.h - class for working with shared memory on Win32 
//                  (adapted from my old works - comments in czech) 
//

///////////////////////////////////////////////////////////////////////////////////////////
// Definition of header struct
typedef struct  {

  // sample rate
  int  SampleRate;
  
  // block length in samples
  int  BlockInSamples;
  
  // center frequency in Hz
  int  L0;

} SM_HDR;

///////////////////////////////////////////////////////////////////////////////////////////
// Deklarace tridy CSharedMemory
class CSharedMemory {

 // Atributy
 private:

  HANDLE      m_MapFile;     // handle na file-mapping objekt

  LPVOID      m_MapAddress;  // adresa zacatku sdilene pameti
  DWORD       m_Length;      // delka sdilene pameti

  LPVOID      m_Data;        // adresa zacatku dat ve sdilene pameti
  DWORD       m_DataLength;  // delka dat ve sdilene pameti

  DWORD       m_PageSize;    // velikost pametove stranky
 
  HANDLE      m_Event;       // event pro udalosti na bufferu (typ. nova data)

  BOOL        m_Write;       // muzeme zapisovat ?

  PBYTE       m_Top;         // ukazatel na zacatek bufferu
  PBYTE       m_Bot;         // ukazatel na konec bufferu
  PBYTE       m_Current;     // ukazatel na aktualni pozici v bufferu
 
  DWORD      *m_pLength;     // ukazatel na delku dat v hlavicce
  DWORD      *m_pCurrent;    // ukazatel na aktualni pozici (offset) v bufferu v hlavicce
  DWORD      *m_pLastWrite;  // ukazatel na cas posledniho zapisu v hlavicce

 
 // Metody
 public:
  
  // vytvor usek sdilene pameti
  BOOL Create(LPCTSTR Name, DWORD Length, BOOL WriteAccess = FALSE);

  // pripoj se na usek sdilene pameti
  BOOL Open(LPCTSTR Name, BOOL WriteAccess = FALSE, char *ErrorInfo = NULL);

  // jsme pripojeni na usek sdilene pameti ?
  BOOL IsOpen(void) {return (m_MapAddress != NULL);}

  // vraci ukazatel na hlavicku bufferu
  SM_HDR *GetHeader(void) {return((SM_HDR *)m_MapAddress);}
  
  // vraci handle na event udalosti na bufferu
  HANDLE GetEvent(void) {return(m_Event);}

  // ziskej pravo zapisu do bufferu
  BOOL GrantWriteAccess(void) 
  {DWORD Dummy;
   if (VirtualProtect(m_MapAddress, m_Length, PAGE_READWRITE, &Dummy)) 
   {m_Write = TRUE;
    return(TRUE);
   }
    else return(FALSE);
  }
 
  // uvolni pravo zapisu do bufferu
  BOOL DenyWriteAccess(void) 
  {DWORD Dummy;
   if (VirtualProtect(m_MapAddress, m_Length, PAGE_READONLY, &Dummy))
   {m_Write = FALSE;
    return(TRUE);
   }
    else return(FALSE);
  }

  // uvolni vsechna prava na buffer
  BOOL DenyAllAccess(void) 
  {DWORD Dummy;
   if (VirtualProtect(m_MapAddress, m_Length, PAGE_NOACCESS, &Dummy))
   {m_Write = FALSE;
    return(TRUE);
   }
    else return(FALSE);
  }
  
  // zjisti vlastnosti sdilene pameti
  BOOL GetMemoryProperties(PMEMORY_BASIC_INFORMATION pMI)
  {
   if (pMI == NULL) return(FALSE);
   if (VirtualQuery(m_MapAddress, pMI, sizeof(MEMORY_BASIC_INFORMATION))
       != sizeof(MEMORY_BASIC_INFORMATION)
      ) return(FALSE);
   return(TRUE);
  }
 
  // pockej na nova data
  // (vraci TRUE pokud nekdo v prubehu cekani nastavil event)
  BOOL WaitForNewData(DWORD TimeOut = INFINITE)
  {
   if (m_Write || (m_Event == NULL)) return(FALSE);
   ResetEvent(m_Event);
   return (WaitForSingleObject(m_Event, TimeOut) != WAIT_TIMEOUT);
  }
 
  // prerus vsechna cekani na data
  void BreakWaitForNewData(void)
  {
   if (m_Event != NULL) SetEvent(m_Event);
  }

  // zapis blok dat
  // (pokud neni pravo k zapisu nebo je blok prilis velky vraci FALSE)
  BOOL Write(PBYTE Ptr, DWORD Len);

  // zjisti cas posledniho zapisu
  DWORD GetLastWrite(void)
  {// pokud neni inicializovano vrat 0
   if (m_Event == NULL) return(0l);
   return(*m_pLastWrite);
  }

  // zjisti pocet bytu k nacteni
  DWORD BytesToRead(void)
  {long len = m_Top + (*m_pCurrent) - m_Current;
   if (len < 0) len += m_DataLength;
   return((DWORD)len);
  }

  // zjisti pocet bytu k nacteni a posun ukazatel za ne
  DWORD ClearBytesToRead(void)
  {PBYTE Current = m_Top + (*m_pCurrent);
   long len = Current - m_Current;
   if (len < 0) len += m_DataLength;
   m_Current = Current;
   return((DWORD)len);
  }

  // precti blok dat
  // (pokud neni k dispozici dostatecny pocet dat vraci FALSE)
  BOOL Read(PBYTE Ptr, DWORD Len);
  
  // odpoj se od useku sdilene pameti
  void Close(void);
  
  // konstruktor
  CSharedMemory(); 
  
  // destruktor
  ~CSharedMemory() {Close();}
};

