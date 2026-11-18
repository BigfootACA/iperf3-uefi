#include "IPerf3.h"

STATIC
VOID
Iperf3TcpCaptureEndpoint (
  IN OUT IPERF3_TCP4_SOCKET *Socket
  )
{
  EFI_TCP4_CONFIG_DATA Mode;
  EFI_STATUS Status;
  ZeroMem (&Mode, sizeof (Mode));
  Status = Socket->Protocol->GetModeData (
    Socket->Protocol, NULL, &Mode, NULL, NULL, NULL
  );
  if (EFI_ERROR (Status))
    return;
  CopyMem (
    &Socket->Endpoint.LocalAddress.V4,
    &Mode.AccessPoint.StationAddress,
    sizeof (Socket->Endpoint.LocalAddress.V4)
    );
  CopyMem (
    &Socket->Endpoint.RemoteAddress.V4,
    &Mode.AccessPoint.RemoteAddress,
    sizeof (Socket->Endpoint.RemoteAddress.V4)
    );
  Socket->Endpoint.Version = Iperf3IpVersion4;
  Socket->Endpoint.LocalPort = Mode.AccessPoint.StationPort;
  Socket->Endpoint.RemotePort = Mode.AccessPoint.RemotePort;
  Socket->Endpoint.Valid = TRUE;
}

STATIC
EFI_STATUS
Iperf3TcpWait (
  IN EFI_TCP4_PROTOCOL *Protocol,
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
Iperf3TcpCancelAndDrain (
  IN IPERF3_TCP4_SOCKET          *Socket,
  IN EFI_TCP4_COMPLETION_TOKEN   *Token,
  IN volatile BOOLEAN             *Done
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

EFI_STATUS
Iperf3Tcp4TxRequestInit (
  OUT IPERF3_TCP4_TX_REQUEST *Request
  )
{
  EFI_STATUS Status;
  if (Request == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem (Request, sizeof (*Request));
  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    Iperf3NetworkNotify,
    (VOID *)&Request->Done, &Request->Event
    );
  return Status;
}

EFI_STATUS
Iperf3Tcp4WriteAsync (
  IN IPERF3_TCP4_SOCKET         *Socket,
  IN OUT IPERF3_TCP4_TX_REQUEST *Request,
  IN CONST VOID                 *Buffer,
  IN UINT32                     Length
  )
{
  EFI_STATUS Status;
  if (Socket == NULL || Socket->Protocol == NULL || Request == NULL ||
      Request->Event == NULL || Buffer == NULL || Length == 0)
    return EFI_INVALID_PARAMETER;
  if (Request->Active)
    return EFI_ACCESS_DENIED;
  ZeroMem (&Request->Token, sizeof (Request->Token));
  ZeroMem (&Request->Data, sizeof (Request->Data));
  Request->Done = FALSE;
  Request->Length = 0;
  Request->Data.Push = TRUE;
  Request->Data.DataLength = Length;
  Request->Data.FragmentCount = 1;
  Request->Data.FragmentTable[0].FragmentLength = Length;
  Request->Data.FragmentTable[0].FragmentBuffer = (VOID *)Buffer;
  Request->Token.CompletionToken.Event = Request->Event;
  Request->Token.CompletionToken.Status = EFI_NOT_READY;
  Request->Token.Packet.TxData = &Request->Data;
  Status = Socket->Protocol->Transmit (Socket->Protocol, &Request->Token);
  if (!EFI_ERROR (Status)) {
    Request->Active = TRUE;
    Request->Length = Length;
  }
  return Status;
}

EFI_STATUS
Iperf3Tcp4WriteWait (
  IN IPERF3_TCP4_SOCKET         *Socket,
  IN OUT IPERF3_TCP4_TX_REQUEST *Request,
  IN UINT32                     TimeoutMs
  )
{
  EFI_STATUS Status;
  if (Socket == NULL || Request == NULL || !Request->Active)
    return EFI_INVALID_PARAMETER;
  Status = Iperf3TcpWait (Socket->Protocol, &Request->Done, TimeoutMs);
  if (Status == EFI_TIMEOUT)
    Iperf3TcpCancelAndDrain (Socket, &Request->Token.CompletionToken, &Request->Done);
  if (!EFI_ERROR (Status))
    Status = Request->Token.CompletionToken.Status;
  Request->Active = FALSE;
  return Status;
}

VOID
Iperf3Tcp4TxRequestFree (
  IN IPERF3_TCP4_SOCKET         *Socket,
  IN OUT IPERF3_TCP4_TX_REQUEST *Request
  )
{
  if (Request == NULL)
    return;
  if (Request->Active && !Request->Done && Socket != NULL && Socket->Protocol != NULL)
    Iperf3TcpCancelAndDrain (Socket, &Request->Token.CompletionToken, &Request->Done);
  Request->Active = FALSE;
  if (Request->Event != NULL)
    gBS->CloseEvent (Request->Event);
  ZeroMem (Request, sizeof (*Request));
}

VOID
Iperf3Tcp4Close (
  IN OUT IPERF3_TCP4_SOCKET *Socket,
  IN BOOLEAN                AbortOnClose
  )
{
  EFI_TCP4_CLOSE_TOKEN CloseToken;
  EFI_STATUS Status;
  if (Socket == NULL)
    return;
  if (Socket->Protocol != NULL && Socket->TxEvent != NULL) {
    ZeroMem (&CloseToken, sizeof (CloseToken));
    Socket->TxDone = FALSE;
    CloseToken.CompletionToken.Event = Socket->TxEvent;
    CloseToken.CompletionToken.Status = EFI_NOT_READY;
    CloseToken.AbortOnClose = AbortOnClose;
    Status = Socket->Protocol->Close (Socket->Protocol, &CloseToken);
    if (!EFI_ERROR (Status))
      Iperf3TcpWait (Socket->Protocol, &Socket->TxDone, 2000);
    Socket->Protocol->Configure (Socket->Protocol, NULL);
  }
  if (Socket->Binding != NULL && Socket->Child != NULL)
    Socket->Binding->DestroyChild (Socket->Binding, Socket->Child);
  if (Socket->TxEvent != NULL)
    gBS->CloseEvent (Socket->TxEvent);
  if (Socket->RxEvent != NULL)
    gBS->CloseEvent (Socket->RxEvent);
  ZeroMem (Socket, sizeof (*Socket));
}

STATIC
EFI_STATUS
Iperf3TcpOpenOnController (
  IN EFI_HANDLE             ImageHandle,
  IN EFI_HANDLE             Controller,
  IN EFI_IPv4_ADDRESS       *RemoteAddress,
  IN UINT16                 RemotePort,
  IN UINT32                 WindowSize,
  OUT IPERF3_TCP4_SOCKET    *Socket
  )
{
  EFI_TCP4_CONFIG_DATA Config;
  EFI_TCP4_CONNECTION_TOKEN ConnectToken;
  EFI_TCP4_OPTION Option;
  EFI_STATUS Status;
  EFI_EVENT ConnectEvent;
  volatile BOOLEAN ConnectDone;
  UINTN Retry;
  ZeroMem (Socket, sizeof (*Socket));
  Socket->Controller = Controller;
  gBS->ConnectController (Controller, NULL, NULL, TRUE);
  Status = gBS->HandleProtocol (
    Controller, &gEfiTcp4ServiceBindingProtocolGuid, (VOID **)&Socket->Binding);
  if (EFI_ERROR (Status))
    return Status;
  Status = Socket->Binding->CreateChild (Socket->Binding, &Socket->Child);
  if (EFI_ERROR (Status))
    goto Error;
  Status = gBS->HandleProtocol (
    Socket->Child, &gEfiTcp4ProtocolGuid, (VOID **)&Socket->Protocol);
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
  Config.AccessPoint.UseDefaultAddress = TRUE;
  Config.AccessPoint.RemoteAddress = *RemoteAddress;
  Config.AccessPoint.RemotePort = RemotePort;
  Config.AccessPoint.ActiveFlag = TRUE;
  ZeroMem (&Option, sizeof (Option));
  if (WindowSize != 0) {
    Option.ReceiveBufferSize = WindowSize;
    Option.SendBufferSize = WindowSize;
    Option.EnableNagle = TRUE;
    Option.EnableWindowScaling = TRUE;
    Option.EnableSelectiveAck = FALSE;
    Option.EnablePathMtuDiscovery = FALSE;
    Config.ControlOption = &Option;
  }
  for (Retry = 0; Retry < 50; Retry++) {
    Status = Socket->Protocol->Configure (Socket->Protocol, &Config);
    if (Status != EFI_NO_MAPPING)
      break;
    gBS->Stall (100000);
  }
  if (EFI_ERROR (Status))
    goto Error;
  ConnectEvent = NULL;
  ConnectDone = FALSE;
  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Iperf3NetworkNotify,
    (VOID *)&ConnectDone, &ConnectEvent
  );
  if (EFI_ERROR (Status))
    goto Error;
  ZeroMem (&ConnectToken, sizeof (ConnectToken));
  ConnectToken.CompletionToken.Event  = ConnectEvent;
  ConnectToken.CompletionToken.Status = EFI_NOT_READY;
  Status = Socket->Protocol->Connect (Socket->Protocol, &ConnectToken);
  if (!EFI_ERROR (Status))
    Status = Iperf3TcpWait (Socket->Protocol, &ConnectDone, 30000);
  if (Status == EFI_TIMEOUT)
    Iperf3TcpCancelAndDrain (Socket, &ConnectToken.CompletionToken, &ConnectDone);
  if (!EFI_ERROR (Status))
    Status = ConnectToken.CompletionToken.Status;
  gBS->CloseEvent (ConnectEvent);
  if (EFI_ERROR (Status))
    goto Error;
  Iperf3TcpCaptureEndpoint (Socket);
  (VOID)ImageHandle;
  return EFI_SUCCESS;
Error:
  Iperf3Tcp4Close (Socket, TRUE);
  return Status;
}

EFI_STATUS
Iperf3Tcp4Connect (
  IN EFI_HANDLE             ImageHandle,
  IN EFI_HANDLE             Controller OPTIONAL,
  IN EFI_IPv4_ADDRESS       *RemoteAddress,
  IN UINT16                 RemotePort,
  IN UINT32                 WindowSize,
  OUT IPERF3_TCP4_SOCKET    *Socket
  )
{
  EFI_HANDLE *Handles;
  EFI_STATUS Status, MediaState;
  UINTN Count, Index;
  if (Controller != NULL)
    return Iperf3TcpOpenOnController (
      ImageHandle, Controller, RemoteAddress,
      RemotePort, WindowSize, Socket
    );
  Handles = NULL;
  Count   = 0;
  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiTcp4ServiceBindingProtocolGuid,
    NULL, &Count, &Handles
  );
  if (EFI_ERROR (Status))
    return Status;
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (!EFI_ERROR (NetLibDetectMediaWaitTimeout (Handles[Index], 0, &MediaState)) &&
        MediaState != EFI_SUCCESS) {
      Status = MediaState;
      continue;
    }
    Status = Iperf3TcpOpenOnController (
      ImageHandle, Handles[Index], RemoteAddress,
      RemotePort, WindowSize, Socket
    );
    if (!EFI_ERROR (Status))
      break;
  }
  FreePool (Handles);
  return Status;
}

EFI_STATUS
Iperf3Tcp4Write (
  IN IPERF3_TCP4_SOCKET *Socket,
  IN CONST VOID         *Buffer,
  IN UINT32             Length,
  IN UINT32             TimeoutMs
  )
{
  EFI_TCP4_TRANSMIT_DATA TxData;
  EFI_TCP4_IO_TOKEN Token;
  EFI_STATUS Status;
  if (Length == 0)
    return EFI_SUCCESS;
  ZeroMem (&TxData, sizeof (TxData));
  TxData.Push = TRUE;
  TxData.DataLength = Length;
  TxData.FragmentCount = 1;
  TxData.FragmentTable[0].FragmentLength = Length;
  TxData.FragmentTable[0].FragmentBuffer = (VOID *)Buffer;
  ZeroMem (&Token, sizeof (Token));
  Socket->TxDone = FALSE;
  Token.CompletionToken.Event = Socket->TxEvent;
  Token.CompletionToken.Status = EFI_NOT_READY;
  Token.Packet.TxData = &TxData;
  Status = Socket->Protocol->Transmit (Socket->Protocol, &Token);
  if (EFI_ERROR (Status))
    return Status;
  Status = Iperf3TcpWait (Socket->Protocol, &Socket->TxDone, TimeoutMs);
  if (Status == EFI_TIMEOUT) {
    Iperf3TcpCancelAndDrain (Socket, &Token.CompletionToken, &Socket->TxDone);
    return EFI_TIMEOUT;
  }
  if (EFI_ERROR (Status))
    return Status;
  return Token.CompletionToken.Status;
}

EFI_STATUS
Iperf3Tcp4Read (
  IN IPERF3_TCP4_SOCKET *Socket,
  OUT VOID              *Buffer,
  IN OUT UINT32         *Length,
  IN UINT32             TimeoutMs
  )
{
  EFI_TCP4_RECEIVE_DATA RxData;
  EFI_TCP4_IO_TOKEN Token;
  EFI_STATUS Status;
  if (*Length == 0)
    return EFI_SUCCESS;
  ZeroMem (&RxData, sizeof (RxData));
  RxData.DataLength = *Length;
  RxData.FragmentCount = 1;
  RxData.FragmentTable[0].FragmentLength = *Length;
  RxData.FragmentTable[0].FragmentBuffer = Buffer;
  ZeroMem (&Token, sizeof (Token));
  Socket->RxDone = FALSE;
  Token.CompletionToken.Event = Socket->RxEvent;
  Token.CompletionToken.Status = EFI_NOT_READY;
  Token.Packet.RxData = &RxData;
  Status = Socket->Protocol->Receive (Socket->Protocol, &Token);
  if (EFI_ERROR (Status))
    return Status;
  Status = Iperf3TcpWait (Socket->Protocol, &Socket->RxDone, TimeoutMs);
  if (Status == EFI_TIMEOUT) {
    Iperf3TcpCancelAndDrain (Socket, &Token.CompletionToken, &Socket->RxDone);
    *Length = 0;
    return EFI_TIMEOUT;
  }
  if (EFI_ERROR (Status))
    return Status;
  if (EFI_ERROR (Token.CompletionToken.Status))
    return Token.CompletionToken.Status;
  *Length = RxData.DataLength;
  return EFI_SUCCESS;
}

EFI_STATUS
Iperf3Tcp4ReadExact (
  IN IPERF3_TCP4_SOCKET *Socket,
  OUT VOID              *Buffer,
  IN UINT32             Length,
  IN UINT32             TimeoutMs
  )
{
  EFI_STATUS Status;
  UINT32 Received, Total;
  Total = 0;
  while (Total < Length) {
    Received = Length - Total;
    Status = Iperf3Tcp4Read (Socket, (UINT8 *)Buffer + Total, &Received, TimeoutMs);
    if (EFI_ERROR (Status))
      return Status;
    if (Received == 0)
      return EFI_CONNECTION_FIN;
    Total += Received;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
Iperf3Tcp4Drain (
  IN IPERF3_TCP4_SOCKET *Socket,
  IN UINT32             TimeoutMs
  )
{
  EFI_STATUS Status;
  UINT8 Buffer[4096];
  UINT32 Length, WaitMs;
  UINT64 Deadline, NowUs, RemainingUs;
  if ((Socket == NULL) || (Socket->Protocol == NULL) || (TimeoutMs == 0))
    return EFI_INVALID_PARAMETER;
  Deadline = Iperf3NowUs () + MultU64x32 (TimeoutMs, 1000);
  for (;;) {
    NowUs = Iperf3NowUs ();
    if (NowUs >= Deadline)
      return EFI_TIMEOUT;
    RemainingUs = Deadline - NowUs;
    WaitMs = (UINT32)DivU64x32 (RemainingUs + 999, 1000);
    if (WaitMs == 0)
      WaitMs = 1;
    Length = sizeof (Buffer);
    Status = Iperf3Tcp4Read (Socket, Buffer, &Length, WaitMs);
    if (Status == EFI_CONNECTION_FIN)
      return EFI_SUCCESS;
    if (EFI_ERROR (Status))
      return Status;
    if (Length == 0)
      return EFI_SUCCESS;
  }
}
