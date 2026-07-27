#include "IPerf3.h"

STATIC
UINT64
SaturatingAdd (
  IN UINT64 Left,
  IN UINT64 Right
  )
{
  if (MAX_UINT64 - Left < Right)
    return MAX_UINT64;
  return Left + Right;
}

STATIC
UINT64
ScaleRemainderRounded (
  IN UINT64 Value,
  IN UINT64 Multiplier,
  IN UINT64 Divisor
  )
{
  UINT64 Bit, Quotient, Remainder;
  Bit = 1;
  while (Bit <= Multiplier / 2)
    Bit <<= 1;
  Quotient = 0;
  Remainder = 0;
  while (Bit != 0) {
    Quotient <<= 1;
    if (Remainder >= Divisor - Remainder) {
      Remainder -= Divisor - Remainder;
      Quotient++;
    } else
      Remainder += Remainder;
    if ((Multiplier & Bit) != 0) {
      if (Remainder >= Divisor - Value) {
        Remainder -= Divisor - Value;
        Quotient++;
      } else
        Remainder += Value;
    }
    Bit >>= 1;
  }
  if (Remainder >= Divisor - Remainder)
    Quotient++;
  return Quotient;
}

STATIC
UINT64
ScaleValueRounded (
  IN UINT64 Value,
  IN UINT64 Multiplier,
  IN UINT64 Divisor
  )
{
  UINT64 Whole, Remainder, Fraction;
  if (Multiplier == 0 || Divisor == 0)
    return MAX_UINT64;
  Whole = DivU64x64Remainder (Value, Divisor, &Remainder);
  Fraction = ScaleRemainderRounded (Remainder, Multiplier, Divisor);
  if (Fraction >= Multiplier) {
    if (Whole == MAX_UINT64)
      return MAX_UINT64;
    Whole++;
    Fraction -= Multiplier;
  }
  if (Whole > (MAX_UINT64 - Fraction) / Multiplier)
    return MAX_UINT64;
  return Whole * Multiplier + Fraction;
}

STATIC
VOID
FormatAdaptive (
  OUT CHAR16       *Buffer,
  IN UINTN         BufferSize,
  IN UINT64        Value,
  IN UINT64        Divisor,
  IN CONST CHAR16  *Unit
  )
{
  UINT64 Hundredths, Rounded;
  Hundredths = ScaleValueRounded (Value, 100, Divisor);
  if (Hundredths < 1000)
    UnicodeSPrint (
      Buffer, BufferSize, L"%Lu.%02Lu %s",
      Hundredths / 100, Hundredths % 100, Unit
      );
  else if (Hundredths < 10000) {
    Rounded = ScaleValueRounded (Value, 10, Divisor);
    UnicodeSPrint (
      Buffer, BufferSize, L"%Lu.%Lu %s",
      Rounded / 10, Rounded % 10, Unit
      );
  } else {
    Rounded = ScaleValueRounded (Value, 1, Divisor);
    UnicodeSPrint (Buffer, BufferSize, L"%Lu %s", Rounded, Unit);
  }
}

STATIC
VOID
FormatTransfer (
  OUT CHAR16 *Buffer,
  IN UINTN   BufferSize,
  IN UINT64  Bytes
  )
{
  STATIC CONST CHAR16 *Units[] = {
    L"Bytes", L"KBytes", L"MBytes", L"GBytes", L"TBytes", L"PBytes", L"EBytes"
  };
  UINT64 Divisor = 1;
  UINTN Unit = 0;
  while (Unit + 1 < ARRAY_SIZE (Units) && Bytes / Divisor >= 1024) Divisor *= 1024, Unit++;
  FormatAdaptive (Buffer, BufferSize, Bytes, Divisor, Units[Unit]);
}

STATIC
UINT64
CalculateBitRate (
  IN UINT64 Bytes,
  IN UINT64 DurationUs
  )
{
  UINT64 Whole, Remainder, Partial;
  if (DurationUs == 0)
    DurationUs = 1;
  Whole = DivU64x64Remainder (Bytes, DurationUs, &Remainder);
  if (Whole > MAX_UINT64 / 8000000)
    return MAX_UINT64;
  Partial = ScaleRemainderRounded (Remainder, 8000000, DurationUs);
  if (MAX_UINT64 - Whole * 8000000 < Partial)
    return MAX_UINT64;
  return Whole * 8000000 + Partial;
}

STATIC
VOID
FormatBitRate (
  OUT CHAR16 *Buffer,
  IN UINTN   BufferSize,
  IN UINT64  Bytes,
  IN UINT64  DurationUs
  )
{
  STATIC CONST CHAR16 *Units[] = {
    L"bits", L"Kbits", L"Mbits", L"Gbits", L"Tbits", L"Pbits", L"Ebits"
  };
  UINT64 Rate = CalculateBitRate (Bytes, DurationUs);
  UINT64 Divisor = 1;
  UINTN Unit = 0;
  Rate = CalculateBitRate (Bytes, DurationUs);
  while (Unit + 1 < ARRAY_SIZE (Units) && Rate / Divisor >= 1000) Divisor *= 1000, Unit++;
  FormatAdaptive (Buffer, BufferSize, Rate, Divisor, Units[Unit]);
}

STATIC
UINT64
RoundCentiseconds (
  IN UINT64 Microseconds
  )
{
  return Microseconds / 10000 + ((Microseconds % 10000 >= 5000) ? 1 : 0);
}

STATIC
VOID
FormatTime (
  OUT CHAR16 *Buffer,
  IN UINTN   BufferSize,
  IN UINT64  Centiseconds
  )
{
  UnicodeSPrint (Buffer, BufferSize, L"%Lu.%02Lu", Centiseconds / 100, Centiseconds % 100);
}

VOID
Iperf3PrintConnection (
  IN IPERF3_ENDPOINT *Endpoint
  )
{
  CHAR16 LocalAddress[64], RemoteAddress[64];
  if (Endpoint == NULL || !Endpoint->Valid)
    return;
  if (Endpoint->Version == Iperf3IpVersion4)
    Print (
      L"[  1] local %u.%u.%u.%u port %u connected to %u.%u.%u.%u port %u\r\n",
      Endpoint->LocalAddress.V4.Addr[0], Endpoint->LocalAddress.V4.Addr[1],
      Endpoint->LocalAddress.V4.Addr[2], Endpoint->LocalAddress.V4.Addr[3], Endpoint->LocalPort,
      Endpoint->RemoteAddress.V4.Addr[0], Endpoint->RemoteAddress.V4.Addr[1],
      Endpoint->RemoteAddress.V4.Addr[2], Endpoint->RemoteAddress.V4.Addr[3], Endpoint->RemotePort
      );
  else if (Endpoint->Version == Iperf3IpVersion6) {
    EFI_STATUS ErrorLocal = NetLibIp6ToStr (&Endpoint->LocalAddress.V6, LocalAddress, sizeof (LocalAddress));
    EFI_STATUS ErrorRemote = NetLibIp6ToStr (&Endpoint->RemoteAddress.V6, RemoteAddress, sizeof (RemoteAddress));
    if (EFI_ERROR (ErrorLocal) || EFI_ERROR (ErrorRemote)) return;
    Print (
      L"[  1] local [%s] port %u connected to [%s] port %u\r\n",
      LocalAddress, Endpoint->LocalPort, RemoteAddress, Endpoint->RemotePort
    );
  }
}

VOID
Iperf3ReportAt (
  IN IPERF3_RESULT *Result,
  IN UINT64        NowUs,
  IN BOOLEAN       Force
  )
{
  CHAR16 Transfer[32], Bitrate[32], StartText[24], EndText[24];
  UINT64 Start, End, Duration, Bytes;
  if (Result->ReportIntervalUs == 0 || NowUs < Result->LastReportUs)
    return;
  Duration = NowUs - Result->LastReportUs;
  if (!Force && Duration < Result->ReportIntervalUs)
    return;
  if (Force && Result->Bytes == Result->LastReportBytes &&
      Duration < Result->ReportIntervalUs)
    return;
  Bytes = Result->Bytes - Result->LastReportBytes;
  Start = RoundCentiseconds (Result->LastReportUs - Result->StartUs);
  End = RoundCentiseconds (NowUs - Result->StartUs);
  FormatTransfer (Transfer, sizeof (Transfer), Bytes);
  FormatBitRate (Bitrate, sizeof (Bitrate), Bytes, Duration);
  FormatTime (StartText, sizeof (StartText), Start);
  FormatTime (EndText, sizeof (EndText), End);
  Print (L"[  1] %6s-%-6s sec  %11s  %11s/sec\r\n", StartText, EndText, Transfer, Bitrate);
  Result->LastReportUs = NowUs;
  Result->LastReportBytes = Result->Bytes;
}

STATIC
VOID
PrintSummaryLine (
  IN UINT64       Bytes,
  IN UINT64       DurationUs,
  IN CONST CHAR16 *Role,
  IN BOOLEAN      ShowRetransmits,
  IN INT64        Retransmits
  )
{
  CHAR16 Transfer[32], Bitrate[32], StartText[24], EndText[24];
  UINT64 End;
  End = RoundCentiseconds (DurationUs);
  FormatTransfer (Transfer, sizeof (Transfer), Bytes);
  FormatBitRate (Bitrate, sizeof (Bitrate), Bytes, DurationUs);
  FormatTime (StartText, sizeof (StartText), 0);
  FormatTime (EndText, sizeof (EndText), End);
  if (ShowRetransmits)
    Print (
      L"[  1] %6s-%-6s sec  %11s  %11s/sec  %3Ld             %s\r\n",
      StartText, EndText, Transfer, Bitrate, Retransmits, Role
      );
  else
    Print (
      L"[  1] %6s-%-6s sec  %11s  %11s/sec                  %s\r\n",
      StartText, EndText, Transfer, Bitrate, Role
      );
}

STATIC
VOID
CalculateUdpPacketStats (
  IN IPERF3_CONFIG      *Config,
  IN IPERF3_RESULT      *Result,
  IN IPERF3_PEER_RESULT *Peer,
  OUT UINT64            *LostPackets,
  OUT UINT64            *TotalPackets
  )
{
  UINT64 CountedLoss, TailLoss;
  if (Config->Reverse) {
    if (Peer != NULL && Peer->Valid) {
      *TotalPackets = Peer->Packets;
      CountedLoss = Peer->Packets >= Result->Packets ? Peer->Packets - Result->Packets : 0;
      *LostPackets = CountedLoss > Result->LostPackets ? CountedLoss : Result->LostPackets;
      if (*LostPackets > *TotalPackets)
        *LostPackets = *TotalPackets;
    } else {
      *LostPackets = Result->LostPackets;
      *TotalPackets = SaturatingAdd (Result->Packets, *LostPackets);
    }
    return;
  }
  *TotalPackets = Result->Packets;
  *LostPackets = Peer->LostPackets;
  TailLoss = Peer->Packets < *TotalPackets ? *TotalPackets - Peer->Packets : 0;
  *LostPackets = SaturatingAdd (*LostPackets, TailLoss);
  if (*LostPackets > *TotalPackets)
    *LostPackets = *TotalPackets;
}

VOID
Iperf3PrintSummary (
  IN IPERF3_CONFIG      *Config,
  IN IPERF3_RESULT      *Result,
  IN IPERF3_PEER_RESULT *Peer
  )
{
  UINT64 LocalDuration, PeerDuration, LostPackets, TotalPackets;
  BOOLEAN ShowRetransmits;
  LocalDuration = Result->EndUs > Result->StartUs ? Result->EndUs - Result->StartUs : 0;
  PeerDuration = (Peer != NULL && Peer->DurationUs != 0) ? Peer->DurationUs : LocalDuration;
  ShowRetransmits = (BOOLEAN)(
    !Config->Udp && Config->Reverse && Peer != NULL && Peer->Valid &&
    Peer->SenderHasRetransmits == 1 && Peer->Retransmits >= 0
  );
  Print (L"- - - - - - - - - - - - - - - - - - - - - - - - -\r\n");
  if (ShowRetransmits)
    Print (L"[ ID] Interval           Transfer     Bitrate         Retr\r\n");
  else
    Print (L"[ ID] Interval           Transfer     Bitrate\r\n");
  if (!Config->Reverse)
    PrintSummaryLine (Result->Bytes, LocalDuration, L"sender", FALSE, -1);
  else if (Peer != NULL && Peer->Valid)
    PrintSummaryLine (Peer->Bytes, PeerDuration, L"sender", ShowRetransmits, Peer->Retransmits);
  else
    Print (L"[  1] (sender statistics not available)\r\n");
  if (Config->Reverse)
    PrintSummaryLine (Result->Bytes, LocalDuration, L"receiver", FALSE, -1);
  else if (Peer != NULL && Peer->Valid)
    PrintSummaryLine (Peer->Bytes, PeerDuration, L"receiver", FALSE, -1);
  else
    Print (L"[  1] (receiver statistics not available)\r\n");
  if (Config->Udp) {
    if (Config->Reverse) {
      CalculateUdpPacketStats (Config, Result, Peer, &LostPackets, &TotalPackets);
      Print (
        L"[  1] jitter %Lu.%03Lu ms, lost %Lu/%Lu datagrams, out-of-order %Lu\r\n",
        Result->JitterUs / 1000, Result->JitterUs % 1000,
        LostPackets, TotalPackets,
        Result->OutOfOrderPackets
        );
    } else if (Peer != NULL && Peer->Valid) {
      CalculateUdpPacketStats (Config, Result, Peer, &LostPackets, &TotalPackets);
      Print (
        L"[  1] jitter %Lu.%03Lu ms, lost %Lu/%Lu datagrams\r\n",
        Peer->JitterUs / 1000, Peer->JitterUs % 1000,
        LostPackets, TotalPackets
        );
    }
  }
}
