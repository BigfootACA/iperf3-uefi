#include "IPerf3.h"
#include <Protocol/ShellParameters.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters;
  EFI_STATUS Status;
  IPERF3_CONFIG Config;
  (VOID)SystemTable;
  ShellParameters = NULL;
  Status = gBS->OpenProtocol (
    ImageHandle, &gEfiShellParametersProtocolGuid,
    (VOID **)&ShellParameters, ImageHandle,
    NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );
  if (EFI_ERROR (Status)) {
    Print (L"IPerf3 must be started from UEFI Shell\r\n");
    return Status;
  }
  Status = Iperf3ParseArguments (
    ShellParameters->Argc, ShellParameters->Argv, &Config
  );
  if (Status == EFI_ABORTED || EFI_ERROR (Status)) {
    Iperf3Usage ();
    return Status == EFI_ABORTED ? EFI_SUCCESS : Status;
  }
  Print (L"Connecting to host %s, port %u\r\n", Config.ServerName, Config.ServerPort);
  if (Config.Reverse)
    Print (L"Reverse mode, remote host %s is sending\r\n", Config.ServerName);
  Status = Iperf3RunClient (ImageHandle, &Config);
  if (EFI_ERROR (Status))
    Print (L"IPerf3 failed: %r\r\n", Status);
  else
    Print (L"\r\niperf Done.\r\n");
  return Status;
}

UINT64
Iperf3NowUs (
  VOID
  )
{
  return GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000;
}

VOID
EFIAPI
Iperf3NetworkNotify (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  volatile BOOLEAN *Done;
  (VOID)Event;
  Done = (volatile BOOLEAN *)Context;
  if (Done != NULL)
    *Done = TRUE;
}
