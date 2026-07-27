#include "IPerf3.h"

#define IPERF3_RATE_SCALE  8000000ULL

STATIC
UINT64
Iperf3SaturatingAdd (
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
Iperf3DurationDeadline (
  IN IPERF3_CONFIG *Config,
  IN UINT64        StartUs
  )
{
  UINT64 DurationUs;
  if (Config->Duration == 0)
    return 0;
  DurationUs = MultU64x32 (Config->Duration, 1000000);
  return Iperf3SaturatingAdd (StartUs, DurationUs);
}

STATIC
UINT64
Iperf3ScaleRemainderCeil (
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
  if (Remainder != 0)
    Quotient++;
  return Quotient;
}

STATIC
UINT64
Iperf3BytesToTargetUs (
  IN UINT64 Bytes,
  IN UINT64 BitsPerSecond
  )
{
  UINT64 Quotient, Remainder, Fraction, TargetUs;
  if (BitsPerSecond == 0)
    return 0;
  Quotient = DivU64x64Remainder (Bytes, BitsPerSecond, &Remainder);
  if (Quotient > MAX_UINT64 / IPERF3_RATE_SCALE)
    return MAX_UINT64;
  TargetUs = Quotient * IPERF3_RATE_SCALE;
  Fraction = Iperf3ScaleRemainderCeil (Remainder, IPERF3_RATE_SCALE, BitsPerSecond);
  return Iperf3SaturatingAdd (TargetUs, Fraction);
}

STATIC
UINT64
Iperf3PacingTarget (
  IN IPERF3_RESULT *Result,
  IN UINT64        Bytes,
  IN UINT64        BitsPerSecond,
  IN UINT64        Deadline
  )
{
  UINT64 Target, TargetUs;
  TargetUs = Iperf3BytesToTargetUs (Bytes, BitsPerSecond);
  Target = Iperf3SaturatingAdd (Result->StartUs, TargetUs);
  if (Deadline != 0 && Target > Deadline)
    Target = Deadline;
  return Target;
}

STATIC
EFI_STATUS
Iperf3WaitUntil (
  IN IPERF3_TCP_SOCKET *Data OPTIONAL,
  IN OUT IPERF3_RESULT *Result,
  IN UINT64            TargetUs
  )
{
  EFI_STATUS Status;
  UINT64 NowUs, StallUs;
  for (;;) {
    NowUs = Iperf3NowUs ();
    if (NowUs >= TargetUs)
      return EFI_SUCCESS;
    if (Data != NULL) {
      Status = Iperf3TcpPoll (Data);
      if (EFI_ERROR (Status) && Status != EFI_NOT_READY)
        return Status;
    }
    Iperf3ReportAt (Result, Iperf3NowUs (), FALSE);
    NowUs = Iperf3NowUs ();
    if (NowUs >= TargetUs)
      return EFI_SUCCESS;
    StallUs = TargetUs - NowUs;
    if (StallUs > 50)
      StallUs = 50;
    gBS->Stall ((UINTN)StallUs);
  }
}

STATIC
EFI_STATUS
Iperf3Pace (
  IN IPERF3_TCP_SOCKET *Data OPTIONAL,
  IN IPERF3_CONFIG     *Config,
  IN OUT IPERF3_RESULT *Result,
  IN UINT64            Bytes,
  IN UINT64            Deadline
  )
{
  UINT64 TargetUs;
  if (Config->Bandwidth == 0)
    return EFI_SUCCESS;
  TargetUs = Iperf3PacingTarget (Result, Bytes, Config->Bandwidth, Deadline);
  return Iperf3WaitUntil (Data, Result, TargetUs);
}

STATIC
UINT64
Iperf3DurationByteBudget (
  IN IPERF3_CONFIG *Config
  )
{
  UINT64 Whole, Partial;
  if (Config->Duration == 0 || Config->Bandwidth == 0)
    return MAX_UINT64;
  Whole = Config->Bandwidth / 8;
  if (Whole > MAX_UINT64 / Config->Duration)
    return MAX_UINT64;
  Partial = MultU64x32 (Config->Bandwidth % 8, Config->Duration) / 8;
  return Iperf3SaturatingAdd (Whole * Config->Duration, Partial);
}

STATIC
VOID
Iperf3InitResult (
  IN IPERF3_CONFIG  *Config,
  OUT IPERF3_RESULT *Result
  )
{
  ZeroMem (Result, sizeof (*Result));
  Result->StartUs = Iperf3NowUs ();
  Result->LastReportUs = Result->StartUs;
  Result->ReportIntervalUs = Config->ReportIntervalUs;
}

STATIC
BOOLEAN
Iperf3ReachedTcpReverseLimit (
  IN IPERF3_CONFIG *Config,
  IN IPERF3_RESULT *Result,
  IN UINT64        NowUs,
  IN UINT64        Deadline
  )
{
  UINT64 TargetBytes;
  if (Deadline != 0 && NowUs >= Deadline)
    return TRUE;
  if (Config->Bytes != 0 && Result->Bytes >= Config->Bytes)
    return TRUE;
  if (Config->Blocks == 0)
    return FALSE;
  if (Config->BlockSize == 0 || Config->Blocks > MAX_UINT64 / Config->BlockSize)
    TargetBytes = MAX_UINT64;
  else
    TargetBytes = Config->Blocks * Config->BlockSize;
  return (BOOLEAN)(Result->Bytes >= TargetBytes);
}

STATIC
EFI_STATUS
Iperf3CompleteTcpWrite (
  IN IPERF3_TCP_SOCKET         *Data,
  IN OUT IPERF3_TCP_TX_REQUEST *Request,
  IN OUT BOOLEAN               *Active,
  IN UINT32                    Length,
  IN OUT IPERF3_RESULT         *Result
  )
{
  EFI_STATUS Status;
  Status = Iperf3TcpWriteWait (Data, Request, IPERF3_DATA_TIMEOUT_MS);
  *Active = FALSE;
  if (EFI_ERROR (Status))
    return Status;
  Result->Bytes = Iperf3SaturatingAdd (Result->Bytes, Length);
  Result->Blocks = Iperf3SaturatingAdd (Result->Blocks, 1);
  if (Result->EndUs == 0)
    Iperf3ReportAt (Result, Iperf3NowUs (), FALSE);
  return EFI_SUCCESS;
}

EFI_STATUS
Iperf3RunTcpData (
  IN IPERF3_TCP_SOCKET *Data,
  IN IPERF3_CONFIG     *Config,
  OUT IPERF3_RESULT    *Result
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINT8      *Buffer;
  UINT32     Length, Received, RequestLength[IPERF3_TCP_PIPE_DEPTH];
  UINT64     Deadline, DurationBudget, NoDataUs, NowUs, ScheduledBlocks = 0, ScheduledBytes = 0;
  UINTN      Index, Initialized = 0, NextRequest = 0;
  BOOLEAN    Active[IPERF3_TCP_PIPE_DEPTH];
  IPERF3_TCP_TX_REQUEST Requests[IPERF3_TCP_PIPE_DEPTH];
  IPERF3_TCP_TX_REQUEST *Request;
  Buffer = AllocatePool (Config->BlockSize);
  if (Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  SetMem (Buffer, Config->BlockSize, 0x5A);
  Iperf3InitResult (Config, Result);
  Deadline = Iperf3DurationDeadline (Config, Result->StartUs);
  DurationBudget = Iperf3DurationByteBudget (Config);
  NoDataUs = Result->StartUs;
  ZeroMem (Requests, sizeof (Requests));
  ZeroMem (RequestLength, sizeof (RequestLength));
  ZeroMem (Active, sizeof (Active));
  if (!Config->Reverse) {
    for (Index = 0; Index < IPERF3_TCP_PIPE_DEPTH; Index++) {
      Status = Iperf3TcpTxRequestInit (Data, &Requests[Index]);
      if (EFI_ERROR (Status))
        goto Finish;
      Initialized++;
    }
  }
  for (;;) {
    NowUs = Iperf3NowUs ();
    if (Config->Reverse) {
      if (Iperf3ReachedTcpReverseLimit (Config, Result, NowUs, Deadline)) {
        Result->EndUs = NowUs;
        break;
      }
    } else {
      if (Deadline != 0 && NowUs >= Deadline)
        break;
      if (DurationBudget != MAX_UINT64 && ScheduledBytes >= DurationBudget) {
        Status = Iperf3WaitUntil (Data, Result, Deadline);
        break;
      }
      if (Config->Bytes != 0 && ScheduledBytes >= Config->Bytes)
        break;
      if (Config->Blocks != 0 && ScheduledBlocks >= Config->Blocks)
        break;
    }
    if (Config->Reverse) {
      Received = Config->BlockSize;
      Status = Iperf3TcpRead (Data, Buffer, &Received, IPERF3_RECEIVE_SLICE_MS);
      if (Status == EFI_TIMEOUT) {
        NowUs = Iperf3NowUs ();
        Iperf3ReportAt (Result, Iperf3NowUs (), FALSE);
        if (Deadline != 0 && NowUs >= Deadline) {
          Status = EFI_SUCCESS;
          Result->EndUs = NowUs;
          break;
        }
        if (NowUs - NoDataUs >= 30000000) {
          Status = EFI_TIMEOUT;
          break;
        }
        Status = EFI_SUCCESS;
        continue;
      }
      if (Status == EFI_CONNECTION_FIN || Status == EFI_ABORTED)
        break;
      if (EFI_ERROR (Status))
        break;
      if (Received == 0)
        continue;
      Result->Bytes = Iperf3SaturatingAdd (Result->Bytes, Received);
      Result->Blocks = Iperf3SaturatingAdd (Result->Blocks, 1);
      NoDataUs = Iperf3NowUs ();
      Iperf3ReportAt (Result, Iperf3NowUs (), FALSE);
      continue;
    }
    Request = &Requests[NextRequest];
    if (Active[NextRequest]) {
      Status = Iperf3CompleteTcpWrite (
        Data, Request, &Active[NextRequest],
        RequestLength[NextRequest], Result
      );
      if (EFI_ERROR (Status))
        break;
    }
    Length = Config->BlockSize;
    if (Config->Bytes != 0 && Config->Bytes - ScheduledBytes < Length)
      Length = (UINT32)(Config->Bytes - ScheduledBytes);
    if (DurationBudget != MAX_UINT64 &&
        DurationBudget - ScheduledBytes < Length)
      Length = (UINT32)(DurationBudget - ScheduledBytes);
    Status = Iperf3TcpWriteAsync (Data, Request, Buffer, Length);
    if (EFI_ERROR (Status))
      break;
    Active[NextRequest] = TRUE;
    RequestLength[NextRequest] = Length;
    ScheduledBytes = Iperf3SaturatingAdd (ScheduledBytes, Length);
    ScheduledBlocks = Iperf3SaturatingAdd (ScheduledBlocks, 1);
    NextRequest++;
    if (NextRequest == IPERF3_TCP_PIPE_DEPTH)
      NextRequest = 0;
    Status = Iperf3Pace (Data, Config, Result, ScheduledBytes, Deadline);
    if (EFI_ERROR (Status))
      break;
  }
  if (!Config->Reverse && !EFI_ERROR (Status)) {
    for (Index = 0; Index < Initialized; Index++) {
      if (!Active[Index])
        continue;
      Status = Iperf3CompleteTcpWrite (
        Data, &Requests[Index], &Active[Index],
        RequestLength[Index], Result
      );
      if (EFI_ERROR (Status))
        break;
    }
  }
Finish:
  if (Result->EndUs == 0)
    Result->EndUs = Iperf3NowUs ();
  for (Index = 0; Index < Initialized; Index++)
    Iperf3TcpTxRequestFree (Data, &Requests[Index]);
  Iperf3ReportAt (Result, Result->EndUs, TRUE);
  FreePool (Buffer);
  return Status;
}

STATIC
UINT32
Iperf3ReadUdpSequence (
  IN UINT8  *Packet,
  IN UINT32 Length,
  OUT INT64 *TransitUs
  )
{
  UINT32 Sec, Usec, Sequence;
  UINT64 SentUs;
  if (Length < 12)
    return 0;
  CopyMem (&Sec, Packet, sizeof (Sec));
  CopyMem (&Usec, Packet + 4, sizeof (Usec));
  CopyMem (&Sequence, Packet + 8, sizeof (Sequence));
  SentUs = MultU64x32 (SwapBytes32 (Sec), 1000000) + SwapBytes32 (Usec);
  *TransitUs = (INT64)Iperf3NowUs () - (INT64)SentUs;
  return SwapBytes32 (Sequence);
}

EFI_STATUS
Iperf3RunUdpData (
  IN IPERF3_UDP_SOCKET *Data,
  IN IPERF3_CONFIG     *Config,
  OUT IPERF3_RESULT    *Result
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINT8      *Buffer;
  UINT32     Length, Sequence, PreviousSequence = 0, Received;
  UINT64     Deadline, DurationBudget, NoDataUs, DeltaUs, Gap, NowUs;
  INT64      TransitUs;
  Buffer = AllocatePool (Config->BlockSize);
  if (Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  SetMem (Buffer, Config->BlockSize, 0xA5);
  Iperf3InitResult (Config, Result);
  Deadline = Iperf3DurationDeadline (Config, Result->StartUs);
  DurationBudget = Iperf3DurationByteBudget (Config);
  NoDataUs = Result->StartUs;
  for (;;) {
    NowUs = Iperf3NowUs ();
    if (!Config->Reverse && DurationBudget != MAX_UINT64 &&
        Result->Bytes >= DurationBudget) {
      Status = Iperf3WaitUntil (NULL, Result, Deadline);
      break;
    }
    if ((Deadline != 0 && NowUs >= Deadline) ||
        (Config->Bytes != 0 && Result->Bytes >= Config->Bytes) ||
        (Config->Blocks != 0 && Result->Blocks >= Config->Blocks))
      break;
    if (Config->Reverse) {
      Received = Config->BlockSize;
      Status = Iperf3UdpRead (Data, Buffer, &Received, IPERF3_RECEIVE_SLICE_MS);
      if (Status == EFI_TIMEOUT) {
        NowUs = Iperf3NowUs ();
        Iperf3ReportAt (Result, Iperf3NowUs (), FALSE);
        if (Deadline != 0 && NowUs >= Deadline) {
          Status = EFI_SUCCESS;
          break;
        }
        if (NowUs - NoDataUs >= 30000000) {
          Status = EFI_TIMEOUT;
          break;
        }
        Status = EFI_SUCCESS;
        continue;
      }
      if (EFI_ERROR (Status))
        break;
      if (Received < 12)
        continue;
      Sequence = Iperf3ReadUdpSequence (Buffer, Received, &TransitUs);
      if (PreviousSequence != 0 && Sequence > PreviousSequence &&
          Sequence - PreviousSequence > 1) {
        Gap = Sequence - PreviousSequence - 1;
        Result->LostPackets = Iperf3SaturatingAdd (Result->LostPackets, Gap);
      }
      if (PreviousSequence != 0 && Sequence <= PreviousSequence) {
        Result->OutOfOrderPackets = Iperf3SaturatingAdd (Result->OutOfOrderPackets, 1);
        if (Sequence < PreviousSequence && Result->LostPackets > 0)
          Result->LostPackets--;
      }
      if (PreviousSequence == 0 || Sequence > PreviousSequence)
        PreviousSequence = Sequence;
      if (Result->HaveTransit) {
        if (TransitUs >= Result->PreviousTransitUs)
          DeltaUs = (UINT64)(TransitUs - Result->PreviousTransitUs);
        else
          DeltaUs = (UINT64)(Result->PreviousTransitUs - TransitUs);
        if (DeltaUs >= Result->JitterUs)
          Result->JitterUs += (DeltaUs - Result->JitterUs) / 16;
        else
          Result->JitterUs -= (Result->JitterUs - DeltaUs) / 16;
      }
      Result->PreviousTransitUs = TransitUs;
      Result->HaveTransit = TRUE;
      Result->Bytes = Iperf3SaturatingAdd (Result->Bytes, Received);
      Result->Blocks = Iperf3SaturatingAdd (Result->Blocks, 1);
      Result->Packets = Iperf3SaturatingAdd (Result->Packets, 1);
      NoDataUs = Iperf3NowUs ();
    } else {
      Sequence = (UINT32)Result->Packets + 1;
      NowUs = Iperf3NowUs ();
      {
        UINT32 Seconds = SwapBytes32 ((UINT32)(NowUs / 1000000));
        UINT32 Microseconds = SwapBytes32 ((UINT32)(NowUs % 1000000));
        CopyMem (Buffer, &Seconds, sizeof (Seconds));
        CopyMem (Buffer + 4, &Microseconds, sizeof (Microseconds));
        Sequence = SwapBytes32 (Sequence);
        CopyMem (Buffer + 8, &Sequence, sizeof (Sequence));
      }
      Length = Config->BlockSize;
      if (Config->Bytes != 0 && Config->Bytes - Result->Bytes < Length)
        Length = (UINT32)(Config->Bytes - Result->Bytes);
      if (DurationBudget != MAX_UINT64 &&
          DurationBudget - Result->Bytes < Length)
        Length = (UINT32)(DurationBudget - Result->Bytes);
      if (DurationBudget != MAX_UINT64 && Length < 12) {
        Status = Iperf3WaitUntil (NULL, Result, Deadline);
        break;
      }
      Status = Iperf3UdpWrite (Data, Buffer, Length, IPERF3_DATA_TIMEOUT_MS);
      if (EFI_ERROR (Status))
        break;
      Result->Bytes = Iperf3SaturatingAdd (Result->Bytes, Length);
      Result->Blocks = Iperf3SaturatingAdd (Result->Blocks, 1);
      Result->Packets = Iperf3SaturatingAdd (Result->Packets, 1);
      Status = Iperf3Pace (NULL, Config, Result, Result->Bytes, Deadline);
      if (EFI_ERROR (Status))
        break;
    }
    Iperf3ReportAt (Result, Iperf3NowUs (), FALSE);
  }
  Result->EndUs = Iperf3NowUs ();
  Iperf3ReportAt (Result, Result->EndUs, TRUE);
  FreePool (Buffer);
  return Status;
}
