#include "IPerf3.h"

STATIC
EFI_STATUS
Iperf3Udp6Wait (
  IN EFI_UDP6_PROTOCOL *Protocol,
  IN volatile BOOLEAN  *Done,
  IN UINT32            TimeoutMs
  )
{
  UINT64 Deadline;
  Deadline = 0;
  if (TimeoutMs != 0)
    Deadline = Iperf3NowUs () + MultU64x32 (TimeoutMs, 1000);
  for (;;) {
    if (*Done)
      return EFI_SUCCESS;
    Protocol->Poll (Protocol);
    if (*Done)
      return EFI_SUCCESS;
    if (Deadline != 0 && Iperf3NowUs () >= Deadline)
      return EFI_TIMEOUT;
    gBS->Stall (50);
  }
}

STATIC
VOID
Iperf3Udp6CaptureEndpoint (
  IN OUT IPERF3_UDP6_SOCKET *Socket
  )
{
  EFI_UDP6_CONFIG_DATA Mode;
  EFI_STATUS Status;
  ZeroMem (&Mode, sizeof (Mode));
  Status = Socket->Protocol->GetModeData (
    Socket->Protocol, &Mode, NULL, NULL, NULL
  );
  if (EFI_ERROR (Status))
    return;
  CopyMem (
    &Socket->Endpoint.LocalAddress.V6,
    &Mode.StationAddress,
    sizeof (Socket->Endpoint.LocalAddress.V6)
    );
  CopyMem (
    &Socket->Endpoint.RemoteAddress.V6,
    &Mode.RemoteAddress,
    sizeof (Socket->Endpoint.RemoteAddress.V6)
    );
  Socket->Endpoint.Version = Iperf3IpVersion6;
  Socket->Endpoint.LocalPort  = Mode.StationPort;
  Socket->Endpoint.RemotePort = Mode.RemotePort;
  Socket->Endpoint.Valid      = TRUE;
}

STATIC
VOID
Iperf3Udp6CancelAndDrain (
  IN IPERF3_UDP6_SOCKET        *Socket,
  IN EFI_UDP6_COMPLETION_TOKEN *Token,
  IN volatile BOOLEAN          *Done
  )
{
  UINT64 Deadline;
  Socket->Protocol->Cancel (Socket->Protocol, Token);
  Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_CANCEL_TIMEOUT_MS, 1000);
  while (!*Done && Iperf3NowUs () < Deadline) {
    Socket->Protocol->Poll (Socket->Protocol);
    gBS->Stall (50);
  }
  if (!*Done)
    Socket->Protocol->Configure (Socket->Protocol, NULL);
}

VOID
Iperf3Udp6Close (
  IN OUT IPERF3_UDP6_SOCKET *Socket
  )
{
  if (Socket == NULL)
    return;
  if (Socket->Protocol != NULL)
    Socket->Protocol->Configure (Socket->Protocol, NULL);
  if (Socket->TxEvent != NULL)
    gBS->CloseEvent (Socket->TxEvent);
  if (Socket->RxEvent != NULL)
    gBS->CloseEvent (Socket->RxEvent);
  if (Socket->Binding != NULL && Socket->Child != NULL)
    Socket->Binding->DestroyChild (Socket->Binding, Socket->Child);
  ZeroMem (Socket, sizeof (*Socket));
}

EFI_STATUS
Iperf3Udp6Open (
  IN EFI_HANDLE             ImageHandle,
  IN EFI_HANDLE             Controller,
  IN EFI_IPv6_ADDRESS       *RemoteAddress,
  IN UINT16                 RemotePort,
  OUT IPERF3_UDP6_SOCKET    *Socket
  )
{
  EFI_UDP6_CONFIG_DATA Config;
  EFI_STATUS Status;
  BOOLEAN Automatic;
  UINT64 MappingDeadline;
  if (Controller == NULL || RemoteAddress == NULL || Socket == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem (Socket, sizeof (*Socket));
  Status = Iperf3PrepareIpv6Controller (Controller);
  if (EFI_ERROR (Status))
    return Status;
  Automatic = Iperf3Ipv6ControllerIsAutomatic (Controller);
  Socket->Controller = Controller;
  Status = gBS->HandleProtocol (
    Controller, &gEfiUdp6ServiceBindingProtocolGuid, (VOID **)&Socket->Binding
  );
  if (EFI_ERROR (Status))
    return Status;
  Status = Socket->Binding->CreateChild (Socket->Binding, &Socket->Child);
  if (EFI_ERROR (Status))
    goto Error;
  Status = gBS->HandleProtocol (
    Socket->Child, &gEfiUdp6ProtocolGuid, (VOID **)&Socket->Protocol
  );
  if (EFI_ERROR (Status))
    goto Error;
  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Iperf3NetworkNotify,
    (VOID *)&Socket->TxDone, &Socket->TxEvent
  );
  if (EFI_ERROR (Status))
    goto Error;
  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Iperf3NetworkNotify,
    (VOID *)&Socket->RxDone, &Socket->RxEvent
  );
  if (EFI_ERROR (Status))
    goto Error;
  ZeroMem (&Config, sizeof (Config));
  Config.AcceptPromiscuous = FALSE;
  Config.AcceptAnyPort = FALSE;
  Config.AllowDuplicatePort = FALSE;
  Config.HopLimit = 64;
  Config.RemoteAddress = *RemoteAddress;
  Config.RemotePort = RemotePort;
  MappingDeadline = Iperf3NowUs () + MultU64x32 (IPERF3_IP6_MAPPING_TIMEOUT_MS, 1000);
  for (;;) {
    Status = Socket->Protocol->Configure (Socket->Protocol, &Config);
    if (Status != EFI_NO_MAPPING)
      break;
    if (!Automatic)
      break;
    Socket->Protocol->Poll (Socket->Protocol);
    if (Iperf3NowUs () >= MappingDeadline) {
      Status = EFI_TIMEOUT;
      break;
    }
    gBS->Stall (100000);
  }
  if (EFI_ERROR (Status))
    goto Error;
  Iperf3Udp6CaptureEndpoint (Socket);
  (VOID)ImageHandle;
  return EFI_SUCCESS;
Error:
  Iperf3Udp6Close (Socket);
  return Status;
}

EFI_STATUS
Iperf3Udp6Write (
  IN IPERF3_UDP6_SOCKET *Socket,
  IN CONST VOID         *Buffer,
  IN UINT32             Length,
  IN UINT32             TimeoutMs
  )
{
  EFI_UDP6_TRANSMIT_DATA TxData;
  EFI_UDP6_COMPLETION_TOKEN Token;
  EFI_STATUS Status;
  if (Length == 0)
    return EFI_SUCCESS;
  ZeroMem (&TxData, sizeof (TxData));
  TxData.DataLength = Length;
  TxData.FragmentCount = 1;
  TxData.FragmentTable[0].FragmentLength = Length;
  TxData.FragmentTable[0].FragmentBuffer = (VOID *)Buffer;
  ZeroMem (&Token, sizeof (Token));
  Socket->TxDone = FALSE;
  Token.Event = Socket->TxEvent;
  Token.Status = EFI_NOT_READY;
  Token.Packet.TxData = &TxData;
  Status = Socket->Protocol->Transmit (Socket->Protocol, &Token);
  if (EFI_ERROR (Status))
    return Status;
  Status = Iperf3Udp6Wait (Socket->Protocol, &Socket->TxDone, TimeoutMs);
  if (Status == EFI_TIMEOUT) {
    Iperf3Udp6CancelAndDrain (Socket, &Token, &Socket->TxDone);
    return EFI_TIMEOUT;
  }
  if (EFI_ERROR (Status))
    return Status;
  return Token.Status;
}

EFI_STATUS
Iperf3Udp6Read (
  IN IPERF3_UDP6_SOCKET *Socket,
  OUT VOID              *Buffer,
  IN OUT UINT32         *Length,
  IN UINT32             TimeoutMs
  )
{
  EFI_UDP6_COMPLETION_TOKEN Token;
  EFI_UDP6_RECEIVE_DATA *RxData;
  EFI_STATUS Status;
  UINT32 Capacity, CopyLength, FragmentLength, Index, Offset;
  Capacity = *Length;
  *Length  = 0;
  if (Capacity == 0)
    return EFI_SUCCESS;
  ZeroMem (&Token, sizeof (Token));
  Socket->RxDone     = FALSE;
  Token.Event         = Socket->RxEvent;
  Token.Status        = EFI_NOT_READY;
  Token.Packet.RxData = NULL;
  Status = Socket->Protocol->Receive (Socket->Protocol, &Token);
  if (EFI_ERROR (Status))
    return Status;
  Status = Iperf3Udp6Wait (Socket->Protocol, &Socket->RxDone, TimeoutMs);
  if (Status == EFI_TIMEOUT) {
    Iperf3Udp6CancelAndDrain (Socket, &Token, &Socket->RxDone);
    if (Token.Packet.RxData != NULL &&
        Token.Packet.RxData->RecycleSignal != NULL)
      gBS->SignalEvent (Token.Packet.RxData->RecycleSignal);
    return EFI_TIMEOUT;
  }
  if (EFI_ERROR (Status))
    return Status;
  if (EFI_ERROR (Token.Status))
    return Token.Status;
  RxData     = Token.Packet.RxData;
  CopyLength = RxData->DataLength;
  if (CopyLength > Capacity)
    CopyLength = Capacity;
  Offset = 0;
  for (Index = 0; Index < RxData->FragmentCount && Offset < CopyLength; Index++) {
    FragmentLength = RxData->FragmentTable[Index].FragmentLength;
    if (FragmentLength > CopyLength - Offset)
      FragmentLength = CopyLength - Offset;
    CopyMem (
      (UINT8 *)Buffer + Offset,
      RxData->FragmentTable[Index].FragmentBuffer,
      FragmentLength
      );
    Offset += FragmentLength;
  }
  *Length = Offset;
  if (RxData->RecycleSignal != NULL)
    gBS->SignalEvent (RxData->RecycleSignal);
  return EFI_SUCCESS;
}
