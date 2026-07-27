#ifndef IPERF3_UEFI_H
#define IPERF3_UEFI_H

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NetLib.h>
#include <Library/PrintLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Ip6Config.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/Tcp4.h>
#include <Protocol/Tcp6.h>
#include <Protocol/Udp4.h>
#include <Protocol/Udp6.h>


#define IPERF3_DEFAULT_PORT              5201
#define IPERF3_DEFAULT_TIME              10
#define IPERF3_TCP_BLOCK_SIZE            (128 * 1024)
#define IPERF3_UDP_BLOCK_SIZE            1460
#define IPERF3_UDP6_BLOCK_SIZE           1452
#define IPERF3_DEFAULT_UDP_RATE          1000000ULL
#define IPERF3_DEFAULT_INTERVAL_US       1000000ULL
#define IPERF3_MIN_INTERVAL_US           100000ULL
#define IPERF3_MAX_INTERVAL_US           60000000ULL
#define IPERF3_COOKIE_SIZE               37
#define IPERF3_CONTROL_TIMEOUT_MS        60000
#define IPERF3_DATA_TIMEOUT_MS           30000
#define IPERF3_DNS_TIMEOUT_MS            30000
#define IPERF3_IP6_MAPPING_TIMEOUT_MS    30000
#define IPERF3_CLOSE_TIMEOUT_MS          5000
#define IPERF3_CONTROL_CLOSE_TIMEOUT_MS  30000
#define IPERF3_CANCEL_TIMEOUT_MS         1000
#define IPERF3_RECEIVE_SLICE_MS          200
#define IPERF3_MAX_JSON_SIZE             (256 * 1024)
#define IPERF3_TCP_PIPE_DEPTH            8
#define IPERF3_STREAM_ID                 1
#define IPERF3_TEST_START                1
#define IPERF3_TEST_RUNNING              2
#define IPERF3_TEST_END                  4
#define IPERF3_PARAM_EXCHANGE            9
#define IPERF3_CREATE_STREAMS            10
#define IPERF3_SERVER_TERMINATE          11
#define IPERF3_EXCHANGE_RESULTS          13
#define IPERF3_DISPLAY_RESULTS           14
#define IPERF3_DONE                      16
#define IPERF3_ACCESS_DENIED             ((INT8)-1)
#define IPERF3_SERVER_ERROR              ((INT8)-2)
#define IPERF3_UDP_CONNECT_MSG           0x39383736U
#define IPERF3_UDP_CONNECT_REPLY         0x36373839U
#define IPERF3_UDP_LEGACY_REPLY          0xB168DE3AU

typedef enum {
  Iperf3IpVersionAny,
  Iperf3IpVersion4 = 4,
  Iperf3IpVersion6 = 6
} IPERF3_IP_VERSION;

typedef union {
  EFI_IPv4_ADDRESS V4;
  EFI_IPv6_ADDRESS V6;
} IPERF3_IP_ADDRESS;

typedef struct IPERF3_ADDRESS {
  IPERF3_IP_VERSION Version;
  IPERF3_IP_ADDRESS Address;
} IPERF3_ADDRESS;

typedef struct IPERF3_CONFIG {
  IPERF3_ADDRESS    ServerAddress;
  CHAR16            *ServerName;
  IPERF3_IP_VERSION RequestedVersion;
  UINT16            ServerPort;
  BOOLEAN           Reverse;
  BOOLEAN           Udp;
  UINT32            Duration;
  UINT64            Bytes;
  UINT64            Blocks;
  UINT32            BlockSize;
  BOOLEAN           BlockSizeSpecified;
  UINT32            WindowSize;
  UINT64            Bandwidth;
  BOOLEAN           BandwidthSpecified;
  UINT64            ReportIntervalUs;
} IPERF3_CONFIG;

typedef struct IPERF3_RESULT {
  UINT64 StartUs;
  UINT64 EndUs;
  UINT64 LastReportUs;
  UINT64 LastReportBytes;
  UINT64 Bytes;
  UINT64 Blocks;
  UINT64 Packets;
  UINT64 LostPackets;
  UINT64 OutOfOrderPackets;
  UINT64 JitterUs;
  INT64  PreviousTransitUs;
  BOOLEAN HaveTransit;
  UINT64 ReportIntervalUs;
} IPERF3_RESULT;

typedef struct IPERF3_ENDPOINT {
  BOOLEAN            Valid;
  IPERF3_IP_VERSION  Version;
  IPERF3_IP_ADDRESS  LocalAddress;
  IPERF3_IP_ADDRESS  RemoteAddress;
  UINT16             LocalPort;
  UINT16             RemotePort;
} IPERF3_ENDPOINT;

typedef struct IPERF3_PEER_RESULT {
  BOOLEAN Valid;
  INT32   SenderHasRetransmits;
  UINT64  Bytes;
  UINT64  DurationUs;
  INT64   Retransmits;
  UINT64  JitterUs;
  UINT64  LostPackets;
  UINT64  Packets;
} IPERF3_PEER_RESULT;

typedef struct {
  EFI_HANDLE                    Controller;
  EFI_HANDLE                    Child;
  EFI_SERVICE_BINDING_PROTOCOL  *Binding;
  EFI_TCP4_PROTOCOL             *Protocol;
  EFI_EVENT                     TxEvent;
  EFI_EVENT                     RxEvent;
  volatile BOOLEAN              TxDone;
  volatile BOOLEAN              RxDone;
  IPERF3_ENDPOINT               Endpoint;
} IPERF3_TCP4_SOCKET;

typedef struct {
  EFI_TCP4_IO_TOKEN       Token;
  EFI_TCP4_TRANSMIT_DATA  Data;
  EFI_EVENT               Event;
  volatile BOOLEAN        Done;
  BOOLEAN                 Active;
  UINT32                  Length;
} IPERF3_TCP4_TX_REQUEST;

typedef struct {
  EFI_HANDLE                    Controller;
  EFI_HANDLE                    Child;
  EFI_SERVICE_BINDING_PROTOCOL  *Binding;
  EFI_TCP6_PROTOCOL             *Protocol;
  EFI_EVENT                     TxEvent;
  EFI_EVENT                     RxEvent;
  volatile BOOLEAN              TxDone;
  volatile BOOLEAN              RxDone;
  IPERF3_ENDPOINT               Endpoint;
} IPERF3_TCP6_SOCKET;

typedef struct {
  EFI_TCP6_IO_TOKEN       Token;
  EFI_TCP6_TRANSMIT_DATA  Data;
  EFI_EVENT               Event;
  volatile BOOLEAN        Done;
  BOOLEAN                 Active;
  UINT32                  Length;
} IPERF3_TCP6_TX_REQUEST;

typedef struct {
  IPERF3_IP_VERSION Version;
  EFI_HANDLE        Controller;
  IPERF3_ENDPOINT   Endpoint;
  union {
    IPERF3_TCP4_SOCKET V4;
    IPERF3_TCP6_SOCKET V6;
  } Socket;
} IPERF3_TCP_SOCKET;

typedef struct {
  IPERF3_IP_VERSION Version;
  union {
    IPERF3_TCP4_TX_REQUEST V4;
    IPERF3_TCP6_TX_REQUEST V6;
  } Request;
} IPERF3_TCP_TX_REQUEST;

typedef struct {
  EFI_HANDLE                    Controller;
  EFI_HANDLE                    Child;
  EFI_SERVICE_BINDING_PROTOCOL  *Binding;
  EFI_UDP4_PROTOCOL             *Protocol;
  EFI_EVENT                     TxEvent;
  EFI_EVENT                     RxEvent;
  volatile BOOLEAN              TxDone;
  volatile BOOLEAN              RxDone;
  IPERF3_ENDPOINT               Endpoint;
} IPERF3_UDP4_SOCKET;

typedef struct {
  EFI_HANDLE                    Controller;
  EFI_HANDLE                    Child;
  EFI_SERVICE_BINDING_PROTOCOL  *Binding;
  EFI_UDP6_PROTOCOL             *Protocol;
  EFI_EVENT                     TxEvent;
  EFI_EVENT                     RxEvent;
  volatile BOOLEAN              TxDone;
  volatile BOOLEAN              RxDone;
  IPERF3_ENDPOINT               Endpoint;
} IPERF3_UDP6_SOCKET;

typedef struct {
  IPERF3_IP_VERSION Version;
  EFI_HANDLE        Controller;
  IPERF3_ENDPOINT   Endpoint;
  union {
    IPERF3_UDP4_SOCKET V4;
    IPERF3_UDP6_SOCKET V6;
  } Socket;
} IPERF3_UDP_SOCKET;

UINT64
Iperf3NowUs (
  VOID
  );

VOID
Iperf3Usage (
  VOID
  );

EFI_STATUS
Iperf3ParseArguments (
  IN UINTN          Argc,
  IN CHAR16         **Argv,
  OUT IPERF3_CONFIG *Config
  );

VOID
Iperf3PrintConnection (
  IN IPERF3_ENDPOINT *Endpoint
  );

VOID
Iperf3ReportAt (
  IN IPERF3_RESULT *Result,
  IN UINT64        NowUs,
  IN BOOLEAN       Force
  );

BOOLEAN
Iperf3ParsePeerResult (
  IN CONST CHAR8        *Json,
  OUT IPERF3_PEER_RESULT *Peer
  );

VOID
Iperf3PrintSummary (
  IN IPERF3_CONFIG      *Config,
  IN IPERF3_RESULT      *Result,
  IN IPERF3_PEER_RESULT *Peer
  );

EFI_STATUS
Iperf3RunClient (
  IN EFI_HANDLE          ImageHandle,
  IN IPERF3_CONFIG       *Config
  );

VOID
EFIAPI
Iperf3NetworkNotify (
  IN EFI_EVENT Event,
  IN VOID      *Context
  );

EFI_STATUS
Iperf3RunTcpData (
  IN IPERF3_TCP_SOCKET *Data,
  IN IPERF3_CONFIG     *Config,
  OUT IPERF3_RESULT    *Result
  );

EFI_STATUS
Iperf3RunUdpData (
  IN IPERF3_UDP_SOCKET *Data,
  IN IPERF3_CONFIG     *Config,
  OUT IPERF3_RESULT    *Result
  );

EFI_STATUS
Iperf3TcpConnectServer (
  IN EFI_HANDLE       ImageHandle,
  IN OUT IPERF3_CONFIG *Config,
  OUT IPERF3_TCP_SOCKET *Socket
  );

EFI_STATUS
Iperf3TcpConnect (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_HANDLE        Controller OPTIONAL,
  IN IPERF3_ADDRESS    *RemoteAddress,
  IN UINT16            RemotePort,
  IN UINT32            WindowSize,
  OUT IPERF3_TCP_SOCKET *Socket
  );

EFI_STATUS
Iperf3TcpWrite (
  IN IPERF3_TCP_SOCKET *Socket,
  IN CONST VOID        *Buffer,
  IN UINT32            Length,
  IN UINT32            TimeoutMs
  );

EFI_STATUS
Iperf3TcpTxRequestInit (
  IN IPERF3_TCP_SOCKET       *Socket,
  OUT IPERF3_TCP_TX_REQUEST  *Request
  );

EFI_STATUS
Iperf3TcpWriteAsync (
  IN IPERF3_TCP_SOCKET          *Socket,
  IN OUT IPERF3_TCP_TX_REQUEST  *Request,
  IN CONST VOID                 *Buffer,
  IN UINT32                     Length
  );

EFI_STATUS
Iperf3TcpWriteWait (
  IN IPERF3_TCP_SOCKET          *Socket,
  IN OUT IPERF3_TCP_TX_REQUEST  *Request,
  IN UINT32                     TimeoutMs
  );

VOID
Iperf3TcpTxRequestFree (
  IN IPERF3_TCP_SOCKET          *Socket,
  IN OUT IPERF3_TCP_TX_REQUEST  *Request
  );

EFI_STATUS
Iperf3TcpRead (
  IN IPERF3_TCP_SOCKET *Socket,
  OUT VOID             *Buffer,
  IN OUT UINT32        *Length,
  IN UINT32            TimeoutMs
  );

EFI_STATUS
Iperf3TcpReadExact (
  IN IPERF3_TCP_SOCKET *Socket,
  OUT VOID             *Buffer,
  IN UINT32            Length,
  IN UINT32            TimeoutMs
  );

EFI_STATUS
Iperf3TcpDrain (
  IN IPERF3_TCP_SOCKET *Socket,
  IN UINT32            TimeoutMs
  );

EFI_STATUS
Iperf3TcpPoll (
  IN IPERF3_TCP_SOCKET *Socket
  );

VOID
Iperf3TcpClose (
  IN OUT IPERF3_TCP_SOCKET *Socket,
  IN BOOLEAN               AbortOnClose
  );

EFI_STATUS
Iperf3UdpOpen (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_HANDLE        Controller,
  IN IPERF3_ADDRESS    *RemoteAddress,
  IN UINT16            RemotePort,
  OUT IPERF3_UDP_SOCKET *Socket
  );

EFI_STATUS
Iperf3UdpWrite (
  IN IPERF3_UDP_SOCKET *Socket,
  IN CONST VOID        *Buffer,
  IN UINT32            Length,
  IN UINT32            TimeoutMs
  );

EFI_STATUS
Iperf3UdpRead (
  IN IPERF3_UDP_SOCKET *Socket,
  OUT VOID             *Buffer,
  IN OUT UINT32        *Length,
  IN UINT32            TimeoutMs
  );

VOID
Iperf3UdpClose (
  IN OUT IPERF3_UDP_SOCKET *Socket
  );

EFI_STATUS
Iperf3PrepareIpv6Controller (
  IN EFI_HANDLE Controller
  );

BOOLEAN
Iperf3Ipv6ControllerIsAutomatic (
  IN EFI_HANDLE Controller
  );

EFI_STATUS
Iperf3ResolveOnController (
  IN EFI_HANDLE         Controller,
  IN IPERF3_IP_VERSION  Version,
  IN CHAR16             *HostName,
  OUT IPERF3_ADDRESS    **Addresses,
  OUT UINTN             *AddressCount
  );

EFI_STATUS Iperf3Tcp4Connect (EFI_HANDLE, EFI_HANDLE, EFI_IPv4_ADDRESS *, UINT16, UINT32, IPERF3_TCP4_SOCKET *);
EFI_STATUS Iperf3Tcp4Write (IPERF3_TCP4_SOCKET *, CONST VOID *, UINT32, UINT32);
EFI_STATUS Iperf3Tcp4TxRequestInit (IPERF3_TCP4_TX_REQUEST *);
EFI_STATUS Iperf3Tcp4WriteAsync (IPERF3_TCP4_SOCKET *, IPERF3_TCP4_TX_REQUEST *, CONST VOID *, UINT32);
EFI_STATUS Iperf3Tcp4WriteWait (IPERF3_TCP4_SOCKET *, IPERF3_TCP4_TX_REQUEST *, UINT32);
VOID Iperf3Tcp4TxRequestFree (IPERF3_TCP4_SOCKET *, IPERF3_TCP4_TX_REQUEST *);
EFI_STATUS Iperf3Tcp4Read (IPERF3_TCP4_SOCKET *, VOID *, UINT32 *, UINT32);
EFI_STATUS Iperf3Tcp4ReadExact (IPERF3_TCP4_SOCKET *, VOID *, UINT32, UINT32);
EFI_STATUS Iperf3Tcp4Drain (IPERF3_TCP4_SOCKET *, UINT32);
VOID Iperf3Tcp4Close (IPERF3_TCP4_SOCKET *, BOOLEAN);
EFI_STATUS Iperf3Tcp6Connect (EFI_HANDLE, EFI_HANDLE, EFI_IPv6_ADDRESS *, UINT16, UINT32, IPERF3_TCP6_SOCKET *);
EFI_STATUS Iperf3Tcp6Write (IPERF3_TCP6_SOCKET *, CONST VOID *, UINT32, UINT32);
EFI_STATUS Iperf3Tcp6TxRequestInit (IPERF3_TCP6_TX_REQUEST *);
EFI_STATUS Iperf3Tcp6WriteAsync (IPERF3_TCP6_SOCKET *, IPERF3_TCP6_TX_REQUEST *, CONST VOID *, UINT32);
EFI_STATUS Iperf3Tcp6WriteWait (IPERF3_TCP6_SOCKET *, IPERF3_TCP6_TX_REQUEST *, UINT32);
VOID Iperf3Tcp6TxRequestFree (IPERF3_TCP6_SOCKET *, IPERF3_TCP6_TX_REQUEST *);
EFI_STATUS Iperf3Tcp6Read (IPERF3_TCP6_SOCKET *, VOID *, UINT32 *, UINT32);
EFI_STATUS Iperf3Tcp6ReadExact (IPERF3_TCP6_SOCKET *, VOID *, UINT32, UINT32);
EFI_STATUS Iperf3Tcp6Drain (IPERF3_TCP6_SOCKET *, UINT32);
VOID Iperf3Tcp6Close (IPERF3_TCP6_SOCKET *, BOOLEAN);
EFI_STATUS Iperf3Udp4Open (EFI_HANDLE, EFI_HANDLE, EFI_IPv4_ADDRESS *, UINT16, IPERF3_UDP4_SOCKET *);
EFI_STATUS Iperf3Udp4Write (IPERF3_UDP4_SOCKET *, CONST VOID *, UINT32, UINT32);
EFI_STATUS Iperf3Udp4Read (IPERF3_UDP4_SOCKET *, VOID *, UINT32 *, UINT32);
VOID Iperf3Udp4Close (IPERF3_UDP4_SOCKET *);
EFI_STATUS Iperf3Udp6Open (EFI_HANDLE, EFI_HANDLE, EFI_IPv6_ADDRESS *, UINT16, IPERF3_UDP6_SOCKET *);
EFI_STATUS Iperf3Udp6Write (IPERF3_UDP6_SOCKET *, CONST VOID *, UINT32, UINT32);
EFI_STATUS Iperf3Udp6Read (IPERF3_UDP6_SOCKET *, VOID *, UINT32 *, UINT32);
VOID Iperf3Udp6Close (IPERF3_UDP6_SOCKET *);

#endif
