#include "IPerf3.h"

VOID
Iperf3Usage (
  VOID
  )
{
  Print (L"Usage: IPerf3 -c host [-4|-6] [-R] [-u] [-t seconds | -n bytes | -k blocks]\r\n");
  Print (L"              [-i interval] [-b bitrate] [-l length] [-w window]\r\n");
  Print (L"  -c host      iperf3 server address or DNS name (port 5201)\r\n");
  Print (L"  -4           use IPv4 only\r\n");
  Print (L"  -6           use IPv6 only\r\n");
  Print (L"  -R           reverse test (server sends)\r\n");
  Print (L"  -u           UDP test (default 1 Mbit/s)\r\n");
  Print (L"  -t seconds   test duration (default 10)\r\n");
  Print (L"  -n bytes     transfer byte count (K/M/G suffixes)\r\n");
  Print (L"  -k blocks    transfer block count (K/M/G suffixes)\r\n");
  Print (L"  -i seconds   report interval, 0 disables reports (default 1)\r\n");
  Print (L"  -b bitrate   target bitrate, SI K/M/G/T suffixes (0 is unlimited)\r\n");
  Print (L"  -l length    block length (default TCP 128K, UDP 1460/1452)\r\n");
  Print (L"  -w window    TCP window/socket buffer size\r\n");
}

STATIC
EFI_STATUS
Iperf3ParseNumber (
  IN CHAR16   *Text,
  OUT UINT64  *Value
  )
{
  CHAR16 *Pointer;
  UINT64 Result, Multiplier;
  UINTN Digit;
  if (Text == NULL || *Text == 0)
    return EFI_INVALID_PARAMETER;
  Pointer = Text;
  Result = 0;
  while (*Pointer >= L'0' && *Pointer <= L'9') {
    Digit = (UINTN)(*Pointer - L'0');
    if (Result > (MAX_UINT64 - Digit) / 10)
      return EFI_INVALID_PARAMETER;
    Result = Result * 10 + Digit;
    Pointer++;
  }
  Multiplier = 1;
  if (*Pointer != 0) {
    if (*Pointer == L'k' || *Pointer == L'K')
      Multiplier = 1024;
    else if (*Pointer == L'm' || *Pointer == L'M')
      Multiplier = 1024 * 1024;
    else if (*Pointer == L'g' || *Pointer == L'G')
      Multiplier = 1024ULL * 1024 * 1024;
    else
      return EFI_INVALID_PARAMETER;
    Pointer++;
    if (*Pointer != 0 || Result > MAX_UINT64 / Multiplier)
      return EFI_INVALID_PARAMETER;
  }
  *Value = Result * Multiplier;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3ParseInterval (
  IN CHAR16   *Text,
  OUT UINT64  *Microseconds
  )
{
  BOOLEAN FractionNonZero = FALSE, HaveDigit = FALSE, Round = FALSE;
  CHAR16 *Pointer, FirstFractionDigit = 0;
  UINT64 Whole = 0, Fraction = 0;
  UINTN Digit = 0, Digits = 0;
  if (Text == NULL || *Text == 0)
    return EFI_INVALID_PARAMETER;
  Pointer = Text;
  while (*Pointer >= L'0' && *Pointer <= L'9') {
    HaveDigit = TRUE;
    Digit = (UINTN)(*Pointer - L'0');
    if (Whole > (MAX_UINT64 - Digit) / 10)
      return EFI_INVALID_PARAMETER;
    Whole = Whole * 10 + Digit;
    Pointer++;
  }
  if (*Pointer == L'.') {
    Pointer++;
    while (*Pointer >= L'0' && *Pointer <= L'9') {
      HaveDigit = TRUE;
      if (FirstFractionDigit == 0)
        FirstFractionDigit = *Pointer;
      if (*Pointer != L'0')
        FractionNonZero = TRUE;
      if (Digits < 6) {
        Fraction = Fraction * 10 + (UINTN)(*Pointer - L'0');
        Digits++;
      } else if (Digits == 6) {
        Round = (BOOLEAN)(*Pointer >= L'5');
        Digits++;
      }
      Pointer++;
    }
  }
  if (!HaveDigit || *Pointer != 0 || Whole > MAX_UINT64 / 1000000)
    return EFI_INVALID_PARAMETER;
  if ((Whole == 0 && FractionNonZero &&
       FirstFractionDigit == L'0') ||
      (Whole == IPERF3_MAX_INTERVAL_US / 1000000 && FractionNonZero) ||
      Whole > IPERF3_MAX_INTERVAL_US / 1000000)
    return EFI_INVALID_PARAMETER;
  while (Digits < 6) {
    Fraction *= 10;
    Digits++;
  }
  if (Round)
    Fraction++;
  if (Fraction >= 1000000) {
    if (Whole == MAX_UINT64)
      return EFI_INVALID_PARAMETER;
    Whole++;
    Fraction -= 1000000;
  }
  if (Whole > (MAX_UINT64 - Fraction) / 1000000)
    return EFI_INVALID_PARAMETER;
  *Microseconds = Whole * 1000000 + Fraction;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3ParseBitrate (
  IN CHAR16   *Text,
  OUT UINT64  *Bitrate
  )
{
  CHAR16 *Pointer;
  UINT64 Whole = 0, Multiplier = 1, FractionRate = 0, WholeRate;
  UINTN Digit, Digits = 0, Exponent = 0;
  BOOLEAN HaveDigit = FALSE, Round = FALSE;
  if (Text == NULL || *Text == 0)
    return EFI_INVALID_PARAMETER;
  Pointer = Text;
  while (*Pointer >= L'0' && *Pointer <= L'9') {
    HaveDigit = TRUE;
    Digit = (UINTN)(*Pointer - L'0');
    if (Whole > (MAX_UINT64 - Digit) / 10)
      return EFI_INVALID_PARAMETER;
    Whole = Whole * 10 + Digit;
    Pointer++;
  }
  if (*Pointer == L'.') {
    Pointer++;
    while (*Pointer >= L'0' && *Pointer <= L'9')
      HaveDigit = TRUE, Pointer++;
  }
  if (!HaveDigit)
    return EFI_INVALID_PARAMETER;
  if (*Pointer != 0) {
    if (*Pointer == L'k' || *Pointer == L'K') {
      Multiplier = 1000;
      Exponent = 3;
    } else if (*Pointer == L'm' || *Pointer == L'M') {
      Multiplier = 1000000;
      Exponent = 6;
    } else if (*Pointer == L'g' || *Pointer == L'G') {
      Multiplier = 1000000000;
      Exponent = 9;
    } else if (*Pointer == L't' || *Pointer == L'T') {
      Multiplier = 1000000000000ULL;
      Exponent = 12;
    } else
      return EFI_INVALID_PARAMETER;
    Pointer++;
  }
  if (*Pointer != 0 || Whole > MAX_UINT64 / Multiplier)
    return EFI_INVALID_PARAMETER;
  Pointer = Text;
  while (*Pointer >= L'0' && *Pointer <= L'9')
    Pointer++;
  if (*Pointer == L'.') {
    Pointer++;
    while (*Pointer >= L'0' && *Pointer <= L'9') {
      if (Digits < Exponent)
        FractionRate = FractionRate * 10 + (UINTN)(*Pointer - L'0');
      else if (Digits == Exponent)
        Round = (BOOLEAN)(*Pointer >= L'5');
      Digits++;
      Pointer++;
    }
    while (Digits < Exponent)
      FractionRate *= 10, Digits++;
  }
  if (Round)
    FractionRate++;
  WholeRate = Whole * Multiplier;
  if (MAX_UINT64 - WholeRate < FractionRate)
    return EFI_INVALID_PARAMETER;
  *Bitrate = WholeRate + FractionRate;
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
Iperf3ParseIpv4 (
  IN CHAR16            *Text,
  OUT EFI_IPv4_ADDRESS *Address
  )
{
  CHAR16 *EndPointer;
  RETURN_STATUS Status;
  Status = StrToIpv4Address (Text, &EndPointer, Address, NULL);
  return (BOOLEAN)(!RETURN_ERROR (Status) && EndPointer != NULL && *EndPointer == 0);
}

STATIC
BOOLEAN
Iperf3ParseIpv6 (
  IN CHAR16            *Text,
  OUT EFI_IPv6_ADDRESS *Address
  )
{
  CHAR16 *EndPointer;
  RETURN_STATUS Status;
  Status = StrToIpv6Address (Text, &EndPointer, Address, NULL);
  return (BOOLEAN)(!RETURN_ERROR (Status) && EndPointer != NULL && *EndPointer == 0);
}

STATIC
EFI_STATUS
Iperf3ParseServer (
  IN OUT IPERF3_CONFIG *Config
  )
{
  EFI_IPv4_ADDRESS Address4;
  EFI_IPv6_ADDRESS Address6;
  if (Config->RequestedVersion != Iperf3IpVersion6 && Iperf3ParseIpv4 (Config->ServerName, &Address4)) {
    Config->ServerAddress.Version = Iperf3IpVersion4;
    Config->ServerAddress.Address.V4 = Address4;
    return EFI_SUCCESS;
  }
  if (Config->RequestedVersion != Iperf3IpVersion4 && Iperf3ParseIpv6 (Config->ServerName, &Address6)) {
    Config->ServerAddress.Version = Iperf3IpVersion6;
    Config->ServerAddress.Address.V6 = Address6;
    return EFI_SUCCESS;
  }
  if ((Config->RequestedVersion == Iperf3IpVersion4 && Iperf3ParseIpv6 (Config->ServerName, &Address6)) ||
      (Config->RequestedVersion == Iperf3IpVersion6 && Iperf3ParseIpv4 (Config->ServerName, &Address4)))
    return EFI_INVALID_PARAMETER;
  if (StrLen (Config->ServerName) == 0 || StrLen (Config->ServerName) > 253 || StrStr (Config->ServerName, L"%") != NULL)
    return EFI_INVALID_PARAMETER;
  Config->ServerAddress.Version = Iperf3IpVersionAny;
  return EFI_SUCCESS;
}

EFI_STATUS
Iperf3ParseArguments (
  IN UINTN          Argc,
  IN CHAR16         **Argv,
  OUT IPERF3_CONFIG *Config
  )
{
  UINTN Index;
  UINT64 Value;
  BOOLEAN HaveServer = FALSE, HaveEndMode = FALSE;
  CHAR16 *Option;
  ZeroMem (Config, sizeof (*Config));
  Config->ServerPort = IPERF3_DEFAULT_PORT;
  Config->Duration = IPERF3_DEFAULT_TIME;
  Config->ReportIntervalUs = IPERF3_DEFAULT_INTERVAL_US;
  for (Index = 1; Index < Argc; Index++) {
    Option = Argv[Index];
    if (StrCmp (Option, L"-R") == 0 || StrCmp (Option, L"--reverse") == 0) {
      Config->Reverse = TRUE;
      continue;
    }
    if (StrCmp (Option, L"-u") == 0 || StrCmp (Option, L"--udp") == 0) {
      Config->Udp = TRUE;
      continue;
    }
    if (StrCmp (Option, L"-4") == 0 || StrCmp (Option, L"--version4") == 0) {
      if (Config->RequestedVersion == Iperf3IpVersion6)
        return EFI_INVALID_PARAMETER;
      Config->RequestedVersion = Iperf3IpVersion4;
      continue;
    }
    if (StrCmp (Option, L"-6") == 0 || StrCmp (Option, L"--version6") == 0) {
      if (Config->RequestedVersion == Iperf3IpVersion4)
        return EFI_INVALID_PARAMETER;
      Config->RequestedVersion = Iperf3IpVersion6;
      continue;
    }
    if (StrCmp (Option, L"-h") == 0 || StrCmp (Option, L"--help") == 0)
      return EFI_ABORTED;
    if (StrCmp (Option, L"-c") != 0 &&
        StrCmp (Option, L"-t") != 0 &&
        StrCmp (Option, L"-n") != 0 &&
        StrCmp (Option, L"-k") != 0 &&
        StrCmp (Option, L"-i") != 0 &&
        StrCmp (Option, L"-b") != 0 &&
        StrCmp (Option, L"-l") != 0 &&
        StrCmp (Option, L"-w") != 0)
      return EFI_INVALID_PARAMETER;
    if (Index + 1 >= Argc)
      return EFI_INVALID_PARAMETER;
    Index++;
    if (StrCmp (Option, L"-c") == 0) {
      if (HaveServer)
        return EFI_INVALID_PARAMETER;
      Config->ServerName = Argv[Index];
      HaveServer = TRUE;
      continue;
    }
    if (StrCmp (Option, L"-i") == 0) {
      if (EFI_ERROR (Iperf3ParseInterval (Argv[Index], &Value)) ||
          (Value != 0 && (Value < IPERF3_MIN_INTERVAL_US || Value > IPERF3_MAX_INTERVAL_US)))
        return EFI_INVALID_PARAMETER;
      Config->ReportIntervalUs = Value;
      continue;
    }
    if (StrCmp (Option, L"-b") == 0) {
      if (EFI_ERROR (Iperf3ParseBitrate (Argv[Index], &Config->Bandwidth)))
        return EFI_INVALID_PARAMETER;
      Config->BandwidthSpecified = TRUE;
      continue;
    }
    if (EFI_ERROR (Iperf3ParseNumber (Argv[Index], &Value)))
      return EFI_INVALID_PARAMETER;
    if (StrCmp (Option, L"-t") == 0) {
      if (HaveEndMode || Value == 0 || Value > MAX_UINT32)
        return EFI_INVALID_PARAMETER;
      Config->Duration = (UINT32)Value;
      Config->Bytes = 0;
      Config->Blocks = 0;
      HaveEndMode = TRUE;
    } else if (StrCmp (Option, L"-n") == 0) {
      if (HaveEndMode || Value == 0)
        return EFI_INVALID_PARAMETER;
      Config->Bytes = Value;
      Config->Duration = 0;
      Config->Blocks = 0;
      HaveEndMode = TRUE;
    } else if (StrCmp (Option, L"-k") == 0) {
      if (HaveEndMode || Value == 0)
        return EFI_INVALID_PARAMETER;
      Config->Blocks = Value;
      Config->Duration = 0;
      Config->Bytes = 0;
      HaveEndMode = TRUE;
    } else if (StrCmp (Option, L"-l") == 0) {
      if (Value == 0 || Value > MAX_UINT32)
        return EFI_INVALID_PARAMETER;
      Config->BlockSize = (UINT32)Value;
      Config->BlockSizeSpecified = TRUE;
    } else {
      if (Value == 0 || Value > MAX_UINT32)
        return EFI_INVALID_PARAMETER;
      Config->WindowSize = (UINT32)Value;
    }
  }
  if (!HaveServer || EFI_ERROR (Iperf3ParseServer (Config)))
    return EFI_INVALID_PARAMETER;
  if (!Config->BandwidthSpecified && Config->Udp)
    Config->Bandwidth = IPERF3_DEFAULT_UDP_RATE;
  if (!Config->BlockSizeSpecified) {
    if (!Config->Udp)
      Config->BlockSize = IPERF3_TCP_BLOCK_SIZE;
    else if (Config->RequestedVersion == Iperf3IpVersion6 ||
             Config->ServerAddress.Version == Iperf3IpVersion6)
      Config->BlockSize = IPERF3_UDP6_BLOCK_SIZE;
    else
      Config->BlockSize = IPERF3_UDP_BLOCK_SIZE;
  }
  if (Config->Udp && (Config->BlockSize < 12 || Config->BlockSize > 65507))
    return EFI_INVALID_PARAMETER;
  if (Config->Udp && Config->Bytes != 0 && Config->Bytes < 12)
    return EFI_INVALID_PARAMETER;
  return EFI_SUCCESS;
}
