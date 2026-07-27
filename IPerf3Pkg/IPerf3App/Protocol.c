#include "IPerf3.h"

#define IPERF3_UDP_CONNECT_MAX_DISCARDS  8

STATIC
VOID
Iperf3MakeCookie (
  OUT CHAR8 Cookie[IPERF3_COOKIE_SIZE]
  )
{
  UINT64 Counter = GetPerformanceCounter ();
  UINT32 A = (UINT32)Counter;
  UINT16 B = (UINT16)(Counter >> 32);
  UINT16 C = (UINT16)(Counter >> 48);
  UINT16 D = (UINT16)(A ^ 0x4000);
  UINT32 E = A ^ (UINT32)(Counter >> 16) ^ 0x80000000U;
  UINT16 F = (UINT16)(Counter ^ (Counter >> 24));
  AsciiSPrint (
    Cookie, IPERF3_COOKIE_SIZE,
    "%08x-%04x-%04x-%04x-%08x%04x",
    A, B, C, D, E, F
    );
}

STATIC
EFI_STATUS
Iperf3SendJson (
  IN IPERF3_TCP_SOCKET *Control,
  IN CONST CHAR8       *Json
  )
{
  UINT32 Length, Header;
  EFI_STATUS Status;

  Length = (UINT32)AsciiStrLen (Json);
  Header = SwapBytes32 (Length);
  Status = Iperf3TcpWrite (Control, &Header, sizeof (Header), IPERF3_CONTROL_TIMEOUT_MS);
  if (EFI_ERROR (Status))
    return Status;
  return Iperf3TcpWrite (Control, Json, Length, IPERF3_CONTROL_TIMEOUT_MS);
}

STATIC
EFI_STATUS
Iperf3ReadJson (
  IN IPERF3_TCP_SOCKET *Control,
  OUT CHAR8            **Json
  )
{
  EFI_STATUS Status;
  UINT32 Header, Length;
  CHAR8 *Buffer;
  *Json = NULL;
  Status = Iperf3TcpReadExact (Control, &Header, sizeof (Header), IPERF3_CONTROL_TIMEOUT_MS);
  if (EFI_ERROR (Status))
    return Status;
  Length = SwapBytes32 (Header);
  if (Length == 0 || Length > IPERF3_MAX_JSON_SIZE)
    return EFI_PROTOCOL_ERROR;
  Buffer = AllocateZeroPool ((UINTN)Length + 1);
  if (Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = Iperf3TcpReadExact (Control, Buffer, Length, IPERF3_CONTROL_TIMEOUT_MS);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }
  *Json = Buffer;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3ReadState (
  IN IPERF3_TCP_SOCKET *Control,
  OUT INT8             *State
  )
{
  return Iperf3TcpReadExact (Control, State, sizeof (*State), IPERF3_CONTROL_TIMEOUT_MS);
}

STATIC
EFI_STATUS
Iperf3SendState (
  IN IPERF3_TCP_SOCKET *Control,
  IN INT8              State
  )
{
  return Iperf3TcpWrite (Control, &State, sizeof (State), IPERF3_CONTROL_TIMEOUT_MS);
}

STATIC
EFI_STATUS
Iperf3WaitUdpConnectReply (
  IN IPERF3_UDP_SOCKET *Socket,
  IN IPERF3_CONFIG     *Config
  )
{
  EFI_STATUS Status;
  UINT8 *Buffer;
  UINT64 Deadline, MaxBytes, NowUs, ReceivedBytes, RemainingUs;
  UINT32 Capacity, DiscardedPackets, Length, Reply, TimeoutMs;
  Capacity = Config->BlockSize;
  if (Capacity < sizeof (Reply))
    Capacity = sizeof (Reply);
  Buffer = AllocatePool (Capacity);
  if (Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  Deadline = Iperf3NowUs () + MultU64x32 (IPERF3_CONTROL_TIMEOUT_MS, 1000);
  MaxBytes = sizeof (Reply) + MultU64x32 (Config->BlockSize, 2);
  DiscardedPackets = 0;
  ReceivedBytes = 0;
  Status = EFI_PROTOCOL_ERROR;
  for (;;) {
    NowUs = Iperf3NowUs ();
    if (NowUs >= Deadline) {
      Status = EFI_TIMEOUT;
      break;
    }
    RemainingUs = Deadline - NowUs;
    TimeoutMs = (UINT32)((RemainingUs + 999) / 1000);
    Length = Capacity;
    Status = Iperf3UdpRead (Socket, Buffer, &Length, TimeoutMs);
    if (EFI_ERROR (Status))
      break;
    ReceivedBytes += Length;
    if (Length == sizeof (Reply)) {
      CopyMem (&Reply, Buffer, sizeof (Reply));
      if ((Reply == IPERF3_UDP_CONNECT_REPLY || Reply == IPERF3_UDP_LEGACY_REPLY) && ReceivedBytes <= MaxBytes) {
        Status = EFI_SUCCESS;
        break;
      }
    }
    DiscardedPackets++;
    if (DiscardedPackets > IPERF3_UDP_CONNECT_MAX_DISCARDS ||
        ReceivedBytes > MaxBytes) {
      Status = EFI_PROTOCOL_ERROR;
      break;
    }
  }
  FreePool (Buffer);
  return Status;
}

STATIC
EFI_STATUS
Iperf3BuildParameters (
  IN IPERF3_CONFIG *Config,
  OUT CHAR8        *Buffer,
  IN UINTN         BufferSize
  )
{
  UINTN Length = 0, Written;
  Written = AsciiSPrint (
    Buffer + Length, BufferSize - Length,
    "{\"%a\":true,\"omit\":0,\"time\":%u,\"num\":%Lu,\"blockcount\":%Lu,\"parallel\":1",
    Config->Udp ? "udp" : "tcp", Config->Duration, Config->Bytes, Config->Blocks
  );
  if (Written >= BufferSize - Length)
    return EFI_BUFFER_TOO_SMALL;
  Length += Written;
  if (Config->Reverse) {
    Written = AsciiSPrint (Buffer + Length, BufferSize - Length, ",\"reverse\":true");
    if (Written >= BufferSize - Length)
      return EFI_BUFFER_TOO_SMALL;
    Length += Written;
  }
  if (Config->WindowSize != 0) {
    Written = AsciiSPrint (Buffer + Length, BufferSize - Length, ",\"window\":%u", Config->WindowSize);
    if (Written >= BufferSize - Length)
      return EFI_BUFFER_TOO_SMALL;
    Length += Written;
  }
  Written = AsciiSPrint (Buffer + Length, BufferSize - Length, ",\"len\":%u", Config->BlockSize);
  if (Written >= BufferSize - Length)
    return EFI_BUFFER_TOO_SMALL;
  Length += Written;
  if (Config->Bandwidth != 0) {
    Written = AsciiSPrint (
      Buffer + Length, BufferSize - Length,
      ",\"bandwidth\":%Lu", Config->Bandwidth
    );
    if (Written >= BufferSize - Length)
      return EFI_BUFFER_TOO_SMALL;
    Length += Written;
  }
  if (Config->Udp) {
    Written = AsciiSPrint (
      Buffer + Length, BufferSize - Length,
      ",\"gso\":0,\"gso_dg_size\":0,\"gso_bf_size\":0,\"gro\":0,\"gro_bf_size\":0"
    );
    if (Written >= BufferSize - Length)
      return EFI_BUFFER_TOO_SMALL;
    Length += Written;
  }
  Written = AsciiSPrint (
    Buffer + Length, BufferSize - Length,
    ",\"client_version\":\"3.21-uefi\"}"
  );
  if (Written >= BufferSize - Length)
    return EFI_BUFFER_TOO_SMALL;
  return EFI_SUCCESS;
}


STATIC
EFI_STATUS
Iperf3SendResult (
  IN IPERF3_TCP_SOCKET *Control,
  IN IPERF3_CONFIG     *Config,
  IN IPERF3_RESULT     *Result
  )
{
  CHAR8  Json[1024];
  UINT64 DurationUs = Result->EndUs - Result->StartUs;
  AsciiSPrint (
    Json, sizeof (Json),
    "{\"cpu_util_total\":0,\"cpu_util_user\":0,\"cpu_util_system\":0,"
    "\"sender_has_retransmits\":%d,\"streams\":[{\"id\":1,\"bytes\":%Lu,"
    "\"retransmits\":-1,\"jitter\":%Lu.%06Lu,\"errors\":%Lu,\"omitted_errors\":0,"
    "\"packets\":%Lu,\"omitted_packets\":0,\"start_time\":0,\"end_time\":%Lu.%06Lu}]}" ,
    Config->Reverse ? -1 : 0,
    Result->Bytes,
    Result->JitterUs / 1000000,
    Result->JitterUs % 1000000,
    Result->LostPackets,
    Result->Packets,
    DurationUs / 1000000,
    DurationUs % 1000000
    );
  return Iperf3SendJson (Control, Json);
}

EFI_STATUS
Iperf3RunClient (
  IN EFI_HANDLE    ImageHandle,
  IN IPERF3_CONFIG *Config
  )
{
  EFI_STATUS Status;
  IPERF3_TCP_SOCKET Control, TcpData;
  IPERF3_UDP_SOCKET UdpData;
  IPERF3_RESULT Result;
  IPERF3_PEER_RESULT PeerResult;
  CHAR8 Cookie[IPERF3_COOKIE_SIZE], Parameters[2048], *ServerResults;
  UINT32 Probe;
  INT8 State;
  ZeroMem (&Control, sizeof (Control));
  ZeroMem (&TcpData, sizeof (TcpData));
  ZeroMem (&UdpData, sizeof (UdpData));
  ZeroMem (&PeerResult, sizeof (PeerResult));
  Iperf3MakeCookie (Cookie);
  Status = Iperf3TcpConnectServer (ImageHandle, Config, &Control);
  if (EFI_ERROR (Status))
    return Status;
  if (Config->Udp && !Config->BlockSizeSpecified)
    Config->BlockSize = Config->ServerAddress.Version == Iperf3IpVersion6 ?
      IPERF3_UDP6_BLOCK_SIZE : IPERF3_UDP_BLOCK_SIZE;
  Status = Iperf3TcpWrite (
    &Control, Cookie,
    IPERF3_COOKIE_SIZE,
    IPERF3_CONTROL_TIMEOUT_MS
  );
  if (EFI_ERROR (Status))
    goto Exit;
  Status = Iperf3ReadState (&Control, &State);
  if (EFI_ERROR (Status))
    goto Exit;
  if (State != IPERF3_PARAM_EXCHANGE) {
    Status = EFI_PROTOCOL_ERROR;
    goto Exit;
  }
  Status = Iperf3BuildParameters (Config, Parameters, sizeof (Parameters));
  if (EFI_ERROR (Status))
    goto Exit;
  Status = Iperf3SendJson (&Control, Parameters);
  if (EFI_ERROR (Status))
    goto Exit;
  Status = Iperf3ReadState (&Control, &State);
  if (EFI_ERROR (Status))
    goto Exit;
  if (State != IPERF3_CREATE_STREAMS) {
    Status = EFI_PROTOCOL_ERROR;
    goto Exit;
  }
  if (Config->Udp) {
    Status = Iperf3UdpOpen (
      ImageHandle, Control.Controller,
      &Config->ServerAddress,
      Config->ServerPort, &UdpData
    );
    if (EFI_ERROR (Status))
      goto Exit;
    Probe = IPERF3_UDP_CONNECT_MSG;
    Status = Iperf3UdpWrite (&UdpData, &Probe, sizeof (Probe), IPERF3_CONTROL_TIMEOUT_MS);
    if (EFI_ERROR (Status))
      goto Exit;
    Status = Iperf3WaitUdpConnectReply (&UdpData, Config);
  } else {
    Status = Iperf3TcpConnect (
      ImageHandle, Control.Controller,
      &Config->ServerAddress, Config->ServerPort,
      Config->WindowSize, &TcpData
      );
    if (EFI_ERROR (Status))
      goto Exit;
    Status = Iperf3TcpWrite (
      &TcpData, Cookie,
      IPERF3_COOKIE_SIZE,
      IPERF3_CONTROL_TIMEOUT_MS
      );
  }
  if (EFI_ERROR (Status))
    goto Exit;
  Iperf3PrintConnection (
    Config->Udp ? &UdpData.Endpoint : &TcpData.Endpoint
  );
  Status = Iperf3ReadState (&Control, &State);
  if (EFI_ERROR (Status))
    goto Exit;
  if (State != IPERF3_TEST_START) {
    Status = EFI_PROTOCOL_ERROR;
    goto Exit;
  }
  Status = Iperf3ReadState (&Control, &State);
  if (EFI_ERROR (Status))
    goto Exit;
  if (State != IPERF3_TEST_RUNNING) {
    Status = EFI_PROTOCOL_ERROR;
    goto Exit;
  }
  if (Config->ReportIntervalUs != 0)
    Print (L"[ ID] Interval           Transfer     Bitrate\r\n");
  if (Config->Udp)
    Status = Iperf3RunUdpData (&UdpData, Config, &Result);
  else
    Status = Iperf3RunTcpData (&TcpData, Config, &Result);
  if (EFI_ERROR (Status))
    goto Exit;
  Status = Iperf3SendState (&Control, IPERF3_TEST_END);
  if (EFI_ERROR (Status))
    goto Exit;
  if (!Config->Udp && Config->Reverse) {
    Status = Iperf3TcpDrain (&TcpData, IPERF3_CLOSE_TIMEOUT_MS);
    if (EFI_ERROR (Status))
      goto Exit;
    Iperf3TcpClose (&TcpData, FALSE);
  }
  Status = Iperf3ReadState (&Control, &State);
  if (EFI_ERROR (Status))
    goto Exit;
  if (State != IPERF3_EXCHANGE_RESULTS) {
    Status = EFI_PROTOCOL_ERROR;
    goto Exit;
  }
  Status = Iperf3SendResult (&Control, Config, &Result);
  if (EFI_ERROR (Status))
    goto Exit;
  Status = Iperf3ReadJson (&Control, &ServerResults);
  if (EFI_ERROR (Status))
    goto Exit;
  Iperf3ParsePeerResult (ServerResults, &PeerResult);
  FreePool (ServerResults);
  Status = Iperf3ReadState (&Control, &State);
  if (EFI_ERROR (Status))
    goto Exit;
  if (State != IPERF3_DISPLAY_RESULTS) {
    Status = EFI_PROTOCOL_ERROR;
    goto Exit;
  }
  Iperf3PrintSummary (Config, &Result, &PeerResult);
  if (Config->Udp)
    Iperf3UdpClose (&UdpData);
  else
    Iperf3TcpClose (&TcpData, FALSE);
  Status = Iperf3SendState (&Control, IPERF3_DONE);
  if (EFI_ERROR (Status))
    goto Exit;
  Status = Iperf3TcpDrain (&Control, IPERF3_CONTROL_CLOSE_TIMEOUT_MS);
  if (EFI_ERROR (Status))
    goto Exit;
  Iperf3TcpClose (&Control, FALSE);

Exit:
  if (Config->Udp)
    Iperf3UdpClose (&UdpData);
  else
    Iperf3TcpClose (&TcpData, TRUE);
  Iperf3TcpClose (&Control, EFI_ERROR (Status));
  return Status;
}
