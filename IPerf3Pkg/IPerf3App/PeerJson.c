#include "IPerf3.h"

#define IPERF3_JSON_MAX_DEPTH  32
typedef struct {
  BOOLEAN Negative;
  UINT64 Mantissa;
  INT32 Exponent;
} IPERF3_JSON_NUMBER;

STATIC
CONST CHAR8 *
SkipWhitespace (
  IN CONST CHAR8 *Walker
  )
{
  while (*Walker == ' ' || *Walker == '\t' || *Walker == '\r' || *Walker == '\n')
    Walker++;
  return Walker;
}

STATIC
BOOLEAN
IsHexDigit (
  IN CHAR8 Character
  )
{
  return (BOOLEAN)(
    (Character >= '0' && Character <= '9') ||
    (Character >= 'a' && Character <= 'f') ||
    (Character >= 'A' && Character <= 'F'));
}

STATIC
BOOLEAN
SkipJsonString (
  IN OUT CONST CHAR8 **Walker
  )
{
  CONST CHAR8 *Current;
  UINTN Index;
  Current = *Walker;
  if (*Current != '"')
    return FALSE;
  Current++;
  while (*Current != '\0') {
    if ((UINT8)*Current < 0x20)
      return FALSE;
    if (*Current == '"') {
      *Walker = Current + 1;
      return TRUE;
    }
    if (*Current++ != '\\')
      continue;
    if (*Current == '"' || *Current == '\\' || *Current == '/' ||
        *Current == 'b' || *Current == 'f' || *Current == 'n' ||
        *Current == 'r' || *Current == 't') {
      Current++;
      continue;
    }
    if (*Current != 'u')
      return FALSE;
    Current++;
    for (Index = 0; Index < 4; Index++) {
      if (!IsHexDigit (*Current))
        return FALSE;
      Current++;
    }
  }
  return FALSE;
}
STATIC
BOOLEAN
SkipJsonNumber (
  IN OUT CONST CHAR8 **Walker
  )
{
  CONST CHAR8 *Current;
  Current = *Walker;
  if (*Current == '-')
    Current++;
  if (*Current == '0')
    Current++;
  else {
    if (*Current < '1' || *Current > '9')
      return FALSE;
    while (*Current >= '0' && *Current <= '9')
      Current++;
  }
  if (*Current == '.') {
    Current++;
    if (*Current < '0' || *Current > '9')
      return FALSE;
    while (*Current >= '0' && *Current <= '9')
      Current++;
  }
  if (*Current == 'e' || *Current == 'E') {
    Current++;
    if (*Current == '+' || *Current == '-')
      Current++;
    if (*Current < '0' || *Current > '9')
      return FALSE;
    while (*Current >= '0' && *Current <= '9')
      Current++;
  }
  *Walker = Current;
  return TRUE;
}
STATIC
BOOLEAN
SkipJsonValue (
  IN OUT CONST CHAR8 **Walker,
  IN UINTN           Depth
  )
{
  CONST CHAR8 *Current;
  if (Depth >= IPERF3_JSON_MAX_DEPTH)
    return FALSE;
  Current = SkipWhitespace (*Walker);
  if (*Current == '"') {
    if (!SkipJsonString (&Current))
      return FALSE;
  } else if (*Current == '{') {
    Current = SkipWhitespace (Current + 1);
    if (*Current != '}') {
      for (;;) {
        if (!SkipJsonString (&Current))
          return FALSE;
        Current = SkipWhitespace (Current);
        if (*Current++ != ':')
          return FALSE;
        if (!SkipJsonValue (&Current, Depth + 1))
          return FALSE;
        Current = SkipWhitespace (Current);
        if (*Current != ',')
          break;
        Current = SkipWhitespace (Current + 1);
      }
      if (*Current != '}')
        return FALSE;
    }
    Current++;
  } else if (*Current == '[') {
    Current = SkipWhitespace (Current + 1);
    if (*Current != ']') {
      for (;;) {
        if (!SkipJsonValue (&Current, Depth + 1))
          return FALSE;
        Current = SkipWhitespace (Current);
        if (*Current != ',')
          break;
        Current = SkipWhitespace (Current + 1);
      }
      if (*Current != ']')
        return FALSE;
    }
    Current++;
  } else if (AsciiStrnCmp (Current, "true", 4) == 0)
    Current += 4;
  else if (AsciiStrnCmp (Current, "false", 5) == 0)
    Current += 5;
  else if (AsciiStrnCmp (Current, "null", 4) == 0)
    Current += 4;
  else if (!SkipJsonNumber (&Current))
    return FALSE;
  *Walker = Current;
  return TRUE;
}
STATIC
UINTN
HexValue (
  IN CHAR8 Character
  )
{
  if (Character >= '0' && Character <= '9')
    return (UINTN)(Character - '0');
  if (Character >= 'a' && Character <= 'f')
    return (UINTN)(Character - 'a' + 10);
  return (UINTN)(Character - 'A' + 10);
}
STATIC
BOOLEAN
JsonStringEquals (
  IN CONST CHAR8 *Start,
  IN CONST CHAR8 *End,
  IN CONST CHAR8 *Name
  )
{
  UINTN Code, Index;
  Start++;
  End--;
  while (Start < End && *Name != '\0') {
    Code = (UINT8)*Start++;
    if (Code == '\\') {
      Code = (UINT8)*Start++;
      if (Code == 'u') {
        Code = 0;
        for (Index = 0; Index < 4; Index++)
          Code = (Code << 4) | HexValue (*Start++);
      } else if (Code == 'b')
        Code = '\b';
      else if (Code == 'f')
        Code = '\f';
      else if (Code == 'n')
        Code = '\n';
      else if (Code == 'r')
        Code = '\r';
      else if (Code == 't')
        Code = '\t';
    }
    if (Code != (UINT8)*Name++)
      return FALSE;
  }
  return (BOOLEAN)(Start == End && *Name == '\0');
}
STATIC
BOOLEAN
FindObjectMember (
  IN CONST CHAR8  *Object,
  IN CONST CHAR8  *Name,
  OUT CONST CHAR8 **Value
  )
{
  CONST CHAR8 *Current, *Key, *KeyEnd, *Member;
  Current = SkipWhitespace (Object);
  if (*Current++ != '{')
    return FALSE;
  Current = SkipWhitespace (Current);
  while (*Current != '}') {
    Key = Current;
    if (!SkipJsonString (&Current))
      return FALSE;
    KeyEnd = Current;
    Current = SkipWhitespace (Current);
    if (*Current++ != ':')
      return FALSE;
    Member = SkipWhitespace (Current);
    Current = Member;
    if (!SkipJsonValue (&Current, 1))
      return FALSE;
    if (JsonStringEquals (Key, KeyEnd, Name)) {
      *Value = Member;
      return TRUE;
    }
    Current = SkipWhitespace (Current);
    if (*Current != ',')
      break;
    Current = SkipWhitespace (Current + 1);
  }
  return FALSE;
}

STATIC
BOOLEAN
ParseJsonNumber (
  IN CONST CHAR8         *Value,
  OUT IPERF3_JSON_NUMBER *Number
  )
{
  CONST CHAR8 *Current;
  INT32 FractionDigits,Exponent;
  BOOLEAN HaveDigit, ExponentNegative;
  ZeroMem (Number, sizeof (*Number));
  Current = Value;
  if (*Current == '-') {
    Number->Negative = TRUE;
    Current++;
  }
  FractionDigits = 0;
  HaveDigit = FALSE;
  while (*Current >= '0' && *Current <= '9') {
    HaveDigit = TRUE;
    if (Number->Mantissa > (MAX_UINT64 - (UINT64)(*Current - '0')) / 10)
      return FALSE;
    Number->Mantissa = Number->Mantissa * 10 + (UINT64)(*Current++ - '0');
  }
  if (!HaveDigit)
    return FALSE;
  if (*Current == '.') {
    Current++;
    if (*Current < '0' || *Current > '9')
      return FALSE;
    while (*Current >= '0' && *Current <= '9') {
      if (Number->Mantissa > (MAX_UINT64 - (UINT64)(*Current - '0')) / 10)
        return FALSE;
      Number->Mantissa = Number->Mantissa * 10 + (UINT64)(*Current++ - '0');
      if (FractionDigits == MAX_INT32)
        return FALSE;
      FractionDigits++;
    }
  }
  Exponent = 0;
  ExponentNegative = FALSE;
  if (*Current == 'e' || *Current == 'E') {
    Current++;
    if (*Current == '-' || *Current == '+') {
      ExponentNegative = (BOOLEAN)(*Current == '-');
      Current++;
    }
    if (*Current < '0' || *Current > '9')
      return FALSE;
    while (*Current >= '0' && *Current <= '9') {
      if (Exponent > 100000000)
        return FALSE;
      Exponent = Exponent * 10 + (*Current++ - '0');
    }
  }
  Current = SkipWhitespace (Current);
  if (*Current != ',' && *Current != '}' && *Current != ']')
    return FALSE;
  if (ExponentNegative)
    Exponent = -Exponent;
  if (Exponent > MAX_INT32 - FractionDigits ||
      Exponent < (-MAX_INT32 - 1) + FractionDigits)
    return FALSE;
  Number->Exponent = Exponent - FractionDigits;
  return TRUE;
}
STATIC
BOOLEAN
NumberToUnsigned (
  IN IPERF3_JSON_NUMBER *Number,
  IN INT32              ExtraExponent,
  OUT UINT64            *Result
  )
{
  UINT64 Value, Divisor;
  INT32 Exponent;
  if (Number->Negative)
    return FALSE;
  Value = Number->Mantissa;
  if (Number->Exponent > MAX_INT32 - ExtraExponent)
    return FALSE;
  Exponent = Number->Exponent + ExtraExponent;
  while (Exponent > 0) {
    if (Value > MAX_UINT64 / 10)
      return FALSE;
    Value *= 10;
    Exponent--;
  }
  Divisor = 1;
  while (Exponent < 0 && Divisor <= MAX_UINT64 / 10) {
    Divisor *= 10;
    Exponent++;
  }
  if (Exponent < 0) {
    *Result = 0;
    return TRUE;
  }
  *Result = Value / Divisor;
  if (Value % Divisor >= (Divisor + 1) / 2)
    (*Result)++;
  return TRUE;
}
STATIC
BOOLEAN
ParseUnsignedMember (
  IN CONST CHAR8 *Value,
  OUT UINT64     *Result
  )
{
  IPERF3_JSON_NUMBER Number;
  if (!ParseJsonNumber (Value, &Number))
    return FALSE;
  return NumberToUnsigned (&Number, 0, Result);
}
STATIC
BOOLEAN
ParseSignedMember (
  IN CONST CHAR8 *Value,
  OUT INT64      *Result
  )
{
  IPERF3_JSON_NUMBER Number;
  UINT64 Magnitude;
  BOOLEAN Negative;
  if (!ParseJsonNumber (Value, &Number))
    return FALSE;
  Negative = Number.Negative;
  Number.Negative = FALSE;
  if (!NumberToUnsigned (&Number, 0, &Magnitude))
    return FALSE;
  if ((!Negative && Magnitude > MAX_INT64) ||
      (Negative && Magnitude > (UINT64)MAX_INT64 + 1))
    return FALSE;
  if (Negative)
    *Result = (Magnitude == (UINT64)MAX_INT64 + 1) ? (-MAX_INT64 - 1) : -(INT64)Magnitude;
  else
    *Result = (INT64)Magnitude;
  return TRUE;
}
STATIC
BOOLEAN
ParseSecondsMember (
  IN CONST CHAR8 *Value,
  OUT UINT64     *Microseconds
  )
{
  IPERF3_JSON_NUMBER Number;
  if (!ParseJsonNumber (Value, &Number))
    return FALSE;
  return NumberToUnsigned (&Number, 6, Microseconds);
}
STATIC
BOOLEAN
FindStreamOne (
  IN CONST CHAR8  *Streams,
  OUT CONST CHAR8 **Stream
  )
{
  CONST CHAR8 *Current, *End, *IdValue;
  UINT64 Id;
  Current = SkipWhitespace (Streams);
  if (*Current++ != '[')
    return FALSE;
  Current = SkipWhitespace (Current);
  while (*Current != ']') {
    End = Current;
    if (!SkipJsonValue (&End, 1))
      return FALSE;
    if (*Current == '{' && FindObjectMember (Current, "id", &IdValue) &&
        ParseUnsignedMember (IdValue, &Id) && Id == IPERF3_STREAM_ID) {
      *Stream = Current;
      return TRUE;
    }
    Current = SkipWhitespace (End);
    if (*Current != ',')
      break;
    Current = SkipWhitespace (Current + 1);
  }
  return FALSE;
}
BOOLEAN
Iperf3ParsePeerResult (
  IN CONST CHAR8         *Json,
  OUT IPERF3_PEER_RESULT *Peer
  )
{
  IPERF3_PEER_RESULT Parsed;
  CONST CHAR8 *End, *Streams, *Stream, *Value;
  BOOLEAN HaveStart, HaveEnd;
  UINT64 StartUs, EndUs;
  INT64 Signed;
  if (Peer == NULL)
    return FALSE;
  ZeroMem (Peer, sizeof (*Peer));
  if (Json == NULL)
    return FALSE;
  End = SkipWhitespace (Json);
  if (*End != '{' || !SkipJsonValue (&End, 0) || *SkipWhitespace (End))return FALSE;
  ZeroMem (&Parsed, sizeof (Parsed));
  Parsed.SenderHasRetransmits = -1;
  Parsed.Retransmits = -1;
  if (FindObjectMember (Json, "sender_has_retransmits", &Value)) {
    if (!ParseSignedMember (Value, &Signed) || Signed < (-MAX_INT32 - 1) || Signed > MAX_INT32) return FALSE;
    Parsed.SenderHasRetransmits = (INT32)Signed;
  }
  if (!FindObjectMember (Json, "streams", &Streams) || !FindStreamOne (Streams, &Stream)) return FALSE;
  if (!FindObjectMember (Stream, "bytes", &Value) || !ParseUnsignedMember (Value, &Parsed.Bytes)) return FALSE;
  if (FindObjectMember (Stream, "retransmits", &Value) &&!ParseSignedMember (Value, &Parsed.Retransmits)) return FALSE;
  if (FindObjectMember (Stream, "jitter", &Value) && !ParseSecondsMember (Value, &Parsed.JitterUs)) return FALSE;
  if (FindObjectMember (Stream, "errors", &Value) && !ParseUnsignedMember (Value, &Parsed.LostPackets)) return FALSE;
  if (FindObjectMember (Stream, "packets", &Value) && !ParseUnsignedMember (Value, &Parsed.Packets)) return FALSE;
  HaveStart = FindObjectMember (Stream, "start_time", &Value);
  HaveEnd = FindObjectMember (Stream, "end_time", &Value);
  if (HaveStart && !ParseSecondsMember (Value, &StartUs)) return FALSE;
  if (HaveEnd && !ParseSecondsMember (Value, &EndUs)) return FALSE;
  if (HaveStart != HaveEnd || (HaveStart && EndUs < StartUs)) return FALSE;
  if (HaveStart) Parsed.DurationUs = EndUs - StartUs;
  Parsed.Valid = TRUE;
  CopyMem (Peer, &Parsed, sizeof (*Peer));
  return TRUE;
}
