#include "IPerf3.h"
#include <Protocol/Dns4.h>
#include <Protocol/Dns6.h>

STATIC
VOID
Iperf3FreeDns4Response (
  IN OUT EFI_DNS4_COMPLETION_TOKEN *Token
  )
{
  if (Token->RspData.H2AData == NULL)
    return;
  if (Token->RspData.H2AData->IpList != NULL)
    FreePool (Token->RspData.H2AData->IpList);
  FreePool (Token->RspData.H2AData);
  Token->RspData.H2AData = NULL;
}

STATIC
VOID
Iperf3FreeDns6Response (
  IN OUT EFI_DNS6_COMPLETION_TOKEN *Token
  )
{
  if (Token->RspData.H2AData == NULL)
    return;
  if (Token->RspData.H2AData->IpList != NULL)
    FreePool (Token->RspData.H2AData->IpList);
  FreePool (Token->RspData.H2AData);
  Token->RspData.H2AData = NULL;
}

STATIC
EFI_STATUS
Iperf3WaitDns4 (
  IN EFI_DNS4_PROTOCOL          *Dns,
  IN OUT EFI_DNS4_COMPLETION_TOKEN *Token,
  IN volatile BOOLEAN          *Done
  )
{
  EFI_STATUS Status;
  UINT64 Deadline;
  Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_DNS_TIMEOUT_MS, 1000);
  while (!*Done && Iperf3NowUs () < Deadline) {
    Status = Dns->Poll (Dns);
    if (*Done)
      return Token->Status;
    if (EFI_ERROR (Status)) {
      Dns->Cancel (Dns, Token);
      Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_CANCEL_TIMEOUT_MS, 1000);
      while (!*Done && Iperf3NowUs () < Deadline) {
        Dns->Poll (Dns);
        gBS->Stall (50);
      }
      return Status;
    }
    gBS->Stall (50);
  }
  if (*Done)
    return Token->Status;
  Dns->Cancel (Dns, Token);
  Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_CANCEL_TIMEOUT_MS, 1000);
  while (!*Done && Iperf3NowUs () < Deadline) {
    Dns->Poll (Dns);
    gBS->Stall (50);
  }
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
Iperf3WaitDns6 (
  IN EFI_DNS6_PROTOCOL          *Dns,
  IN OUT EFI_DNS6_COMPLETION_TOKEN *Token,
  IN volatile BOOLEAN          *Done
  )
{
  EFI_STATUS Status;
  UINT64 Deadline;
  Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_DNS_TIMEOUT_MS, 1000);
  while (!*Done && Iperf3NowUs () < Deadline) {
    Status = Dns->Poll (Dns);
    if (*Done)
      return Token->Status;
    if (EFI_ERROR (Status)) {
      Dns->Cancel (Dns, Token);
      Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_CANCEL_TIMEOUT_MS, 1000);
      while (!*Done && Iperf3NowUs () < Deadline) {
        Dns->Poll (Dns);
        gBS->Stall (50);
      }
      return Status;
    }
    gBS->Stall (50);
  }
  if (*Done)
    return Token->Status;
  Dns->Cancel (Dns, Token);
  Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_CANCEL_TIMEOUT_MS, 1000);
  while (!*Done && Iperf3NowUs () < Deadline) {
    Dns->Poll (Dns);
    gBS->Stall (50);
  }
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
Iperf3CopyDns4Addresses (
  IN EFI_DNS4_COMPLETION_TOKEN *Token,
  OUT IPERF3_ADDRESS           **Addresses,
  OUT UINTN                    *AddressCount
  )
{
  DNS_HOST_TO_ADDR_DATA *Response;
  IPERF3_ADDRESS *Result;
  UINTN Index;
  Response = Token->RspData.H2AData;
  if (Response == NULL || Response->IpCount == 0 || Response->IpList == NULL)
    return EFI_DEVICE_ERROR;
  Result = AllocateZeroPool ((UINTN)Response->IpCount * sizeof (*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < Response->IpCount; Index++) {
    Result[Index].Version = Iperf3IpVersion4;
    CopyMem (
      &Result[Index].Address.V4, &Response->IpList[Index],
      sizeof (Result[Index].Address.V4)
    );
  }
  *Addresses = Result;
  *AddressCount = Response->IpCount;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3CopyDns6Addresses (
  IN EFI_DNS6_COMPLETION_TOKEN *Token,
  OUT IPERF3_ADDRESS           **Addresses,
  OUT UINTN                    *AddressCount
  )
{
  DNS6_HOST_TO_ADDR_DATA *Response;
  IPERF3_ADDRESS *Result;
  UINTN Index;
  Response = Token->RspData.H2AData;
  if (Response == NULL || Response->IpCount == 0 || Response->IpList == NULL)
    return EFI_DEVICE_ERROR;
  Result = AllocateZeroPool ((UINTN)Response->IpCount * sizeof (*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < Response->IpCount; Index++) {
    Result[Index].Version = Iperf3IpVersion6;
    CopyMem (
      &Result[Index].Address.V6, &Response->IpList[Index],
      sizeof (Result[Index].Address.V6)
    );
  }
  *Addresses = Result;
  *AddressCount = Response->IpCount;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3ResolveDns4 (
  IN EFI_HANDLE      Controller,
  IN CHAR16          *HostName,
  OUT IPERF3_ADDRESS **Addresses,
  OUT UINTN          *AddressCount
  )
{
  EFI_SERVICE_BINDING_PROTOCOL *Binding;
  EFI_DNS4_PROTOCOL *Dns;
  EFI_DNS4_CONFIG_DATA Config;
  EFI_DNS4_COMPLETION_TOKEN Token;
  EFI_HANDLE Child;
  EFI_STATUS Status;
  volatile BOOLEAN Done;
  Binding = NULL;
  Dns = NULL;
  Child = NULL;
  ZeroMem (&Token, sizeof (Token));
  Status = gBS->HandleProtocol (
    Controller, &gEfiDns4ServiceBindingProtocolGuid, (VOID **)&Binding
  );
  if (EFI_ERROR (Status))
    return Status;
  Status = Binding->CreateChild (Binding, &Child);
  if (EFI_ERROR (Status))
    return Status;
  Status = gBS->HandleProtocol (Child, &gEfiDns4ProtocolGuid, (VOID **)&Dns);
  if (EFI_ERROR (Status))
    goto Exit;
  ZeroMem (&Config, sizeof (Config));
  Config.UseDefaultSetting = TRUE;
  Config.EnableDnsCache = TRUE;
  Config.Protocol = EFI_IP_PROTO_UDP;
  Status = Dns->Configure (Dns, &Config);
  if (EFI_ERROR (Status))
    goto Exit;
  Done = FALSE;
  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Iperf3NetworkNotify,
    (VOID *)&Done, &Token.Event
  );
  if (EFI_ERROR (Status))
    goto Exit;
  Token.Status = EFI_NOT_READY;
  Token.RetryCount = IPERF3_DNS_TIMEOUT_MS / 2000;
  Token.RetryInterval = 2;
  Status = Dns->HostNameToIp (Dns, HostName, &Token);
  if (!EFI_ERROR (Status))
    Status = Iperf3WaitDns4 (Dns, &Token, &Done);
  if (!EFI_ERROR (Status))
    Status = Iperf3CopyDns4Addresses (&Token, Addresses, AddressCount);
Exit:
  if (Dns != NULL)
    Dns->Configure (Dns, NULL);
  if (Token.Event != NULL)
    gBS->CloseEvent (Token.Event);
  Iperf3FreeDns4Response (&Token);
  if (Child != NULL)
    Binding->DestroyChild (Binding, Child);
  return Status;
}

STATIC
EFI_STATUS
Iperf3ResolveDns6 (
  IN EFI_HANDLE      Controller,
  IN CHAR16          *HostName,
  OUT IPERF3_ADDRESS **Addresses,
  OUT UINTN          *AddressCount
  )
{
  EFI_SERVICE_BINDING_PROTOCOL *Binding;
  EFI_DNS6_PROTOCOL *Dns;
  EFI_DNS6_CONFIG_DATA Config;
  EFI_DNS6_COMPLETION_TOKEN Token;
  EFI_HANDLE Child;
  EFI_STATUS Status;
  UINT64 MappingDeadline;
  volatile BOOLEAN Done;
  Binding = NULL;
  Dns = NULL;
  Child = NULL;
  ZeroMem (&Token, sizeof (Token));
  Status = Iperf3PrepareIpv6Controller (Controller);
  if (EFI_ERROR (Status))
    return Status;
  Status = gBS->HandleProtocol (
    Controller, &gEfiDns6ServiceBindingProtocolGuid, (VOID **)&Binding
  );
  if (EFI_ERROR (Status))
    return Status;
  Status = Binding->CreateChild (Binding, &Child);
  if (EFI_ERROR (Status))
    return Status;
  Status = gBS->HandleProtocol (
    Child, &gEfiDns6ProtocolGuid, (VOID **)&Dns
  );
  if (EFI_ERROR (Status))
    goto Exit;
  ZeroMem (&Config, sizeof (Config));
  Config.EnableDnsCache = TRUE;
  Config.Protocol = EFI_IP_PROTO_UDP;
  MappingDeadline = Iperf3NowUs () + MultU64x32 (IPERF3_IP6_MAPPING_TIMEOUT_MS, 1000);
  for (;;) {
    Status = Dns->Configure (Dns, &Config);
    if (Status != EFI_NO_MAPPING ||
        !Iperf3Ipv6ControllerIsAutomatic (Controller))
      break;
    if (Iperf3NowUs () >= MappingDeadline) {
      Status = EFI_TIMEOUT;
      break;
    }
    gBS->Stall (100000);
  }
  if (EFI_ERROR (Status))
    goto Exit;
  Done = FALSE;
  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Iperf3NetworkNotify,
    (VOID *)&Done, &Token.Event
  );
  if (EFI_ERROR (Status))
    goto Exit;
  Token.Status = EFI_NOT_READY;
  Token.RetryCount = IPERF3_DNS_TIMEOUT_MS / 2000;
  Token.RetryInterval = 2;
  Status = Dns->HostNameToIp (Dns, HostName, &Token);
  if (!EFI_ERROR (Status))
    Status = Iperf3WaitDns6 (Dns, &Token, &Done);
  if (!EFI_ERROR (Status))
    Status = Iperf3CopyDns6Addresses (&Token, Addresses, AddressCount);
Exit:
  if (Dns != NULL)
    Dns->Configure (Dns, NULL);
  if (Token.Event != NULL)
    gBS->CloseEvent (Token.Event);
  Iperf3FreeDns6Response (&Token);
  if (Child != NULL)
    Binding->DestroyChild (Binding, Child);
  return Status;
}

EFI_STATUS
Iperf3ResolveOnController (
  IN EFI_HANDLE        Controller,
  IN IPERF3_IP_VERSION Version,
  IN CHAR16            *HostName,
  OUT IPERF3_ADDRESS   **Addresses,
  OUT UINTN            *AddressCount
  )
{
  if (Addresses == NULL || AddressCount == NULL)
    return EFI_INVALID_PARAMETER;
  *Addresses = NULL;
  *AddressCount = 0;
  if (Controller == NULL || HostName == NULL || *HostName == L'\0')
    return EFI_INVALID_PARAMETER;
  if (Version == Iperf3IpVersion4)
    return Iperf3ResolveDns4 (
      Controller, HostName, Addresses, AddressCount
    );
  if (Version == Iperf3IpVersion6)
    return Iperf3ResolveDns6 (
      Controller, HostName, Addresses, AddressCount
    );
  return EFI_INVALID_PARAMETER;
}
