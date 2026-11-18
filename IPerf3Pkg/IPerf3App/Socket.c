#include "IPerf3.h"

STATIC
EFI_STATUS
Iperf3GetIpv6ControllerConfig (
  IN EFI_HANDLE                 Controller,
  OUT EFI_IP6_CONFIG_PROTOCOL   **Ip6Config,
  OUT EFI_IP6_CONFIG_POLICY     *Policy
  )
{
  EFI_STATUS Status;
  UINTN Size;
  if (Controller == NULL || Ip6Config == NULL || Policy == NULL)
    return EFI_INVALID_PARAMETER;
  *Ip6Config = NULL;
  Status = gBS->HandleProtocol (Controller, &gEfiIp6ConfigProtocolGuid, (VOID **)Ip6Config);
  if (EFI_ERROR (Status))
    return Status;
  Size = sizeof (*Policy);
  Status = (*Ip6Config)->GetData (*Ip6Config, Ip6ConfigDataTypePolicy, &Size, Policy);
  if (EFI_ERROR (Status))
    return Status;
  if (Size != sizeof (*Policy) || (*Policy != Ip6ConfigPolicyManual && *Policy != Ip6ConfigPolicyAutomatic))
    return EFI_PROTOCOL_ERROR;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3Ipv6ConfigDataIsEmpty (
  IN EFI_IP6_CONFIG_PROTOCOL *Ip6Config,
  IN EFI_IP6_CONFIG_DATA_TYPE DataType,
  OUT BOOLEAN                *Empty
  )
{
  EFI_STATUS Status;
  UINTN Size;
  if (Ip6Config == NULL || Empty == NULL)
    return EFI_INVALID_PARAMETER;
  *Empty = FALSE;
  Size = 0;
  Status = Ip6Config->GetData (Ip6Config, DataType, &Size, NULL);
  if ((Status == EFI_NOT_FOUND || Status == EFI_SUCCESS) && Size == 0) {
    *Empty = TRUE;
    return EFI_SUCCESS;
  }
  if (Status == EFI_BUFFER_TOO_SMALL && Size != 0)
    return EFI_SUCCESS;
  if (Status == EFI_NOT_FOUND || Status == EFI_BUFFER_TOO_SMALL || !EFI_ERROR (Status))
    return EFI_PROTOCOL_ERROR;
  return Status;
}

EFI_STATUS
Iperf3PrepareIpv6Controller (
  IN EFI_HANDLE Controller
  )
{
  EFI_IP6_CONFIG_PROTOCOL *Ip6Config;
  EFI_IP6_CONFIG_POLICY Policy;
  EFI_STATUS Status;
  BOOLEAN Empty;
  Status = Iperf3GetIpv6ControllerConfig (Controller, &Ip6Config, &Policy);
  if (EFI_ERROR (Status) || Policy == Ip6ConfigPolicyAutomatic)
    return Status;
  Status = Iperf3Ipv6ConfigDataIsEmpty (
    Ip6Config, Ip6ConfigDataTypeManualAddress, &Empty
  );
  if (EFI_ERROR (Status) || !Empty)
    return Status;
  Status = Iperf3Ipv6ConfigDataIsEmpty (
    Ip6Config, Ip6ConfigDataTypeGateway, &Empty
  );
  if (EFI_ERROR (Status) || !Empty)
    return Status;
  Status = Iperf3Ipv6ConfigDataIsEmpty (
    Ip6Config, Ip6ConfigDataTypeDnsServer, &Empty
  );
  if (EFI_ERROR (Status) || !Empty)
    return Status;
  Policy = Ip6ConfigPolicyAutomatic;
  return Ip6Config->SetData (
    Ip6Config, Ip6ConfigDataTypePolicy, sizeof (Policy), &Policy
  );
}

BOOLEAN
Iperf3Ipv6ControllerIsAutomatic (
  IN EFI_HANDLE Controller
  )
{
  EFI_STATUS Status;
  EFI_IP6_CONFIG_PROTOCOL *Ip6Config;
  EFI_IP6_CONFIG_POLICY Policy;
  Status = Iperf3GetIpv6ControllerConfig (Controller, &Ip6Config, &Policy);
  if (EFI_ERROR (Status))
    return FALSE;
  return Policy == Ip6ConfigPolicyAutomatic;
}

STATIC
EFI_STATUS
Iperf3Ipv4InterfaceNameMatches (
  IN EFI_HANDLE  Controller,
  IN CHAR16      *Name,
  OUT BOOLEAN    *Matches
  )
{
  EFI_IP4_CONFIG2_PROTOCOL       *Ip4Config;
  EFI_IP4_CONFIG2_INTERFACE_INFO *IfInfo;
  EFI_STATUS Status;
  UINTN Size;
  *Matches = FALSE;
  Status = gBS->HandleProtocol (
    Controller, &gEfiIp4Config2ProtocolGuid, (VOID **)&Ip4Config
    );
  if (EFI_ERROR (Status))
    return Status;
  Size = 0;
  Status = Ip4Config->GetData (
    Ip4Config, Ip4Config2DataTypeInterfaceInfo, &Size, NULL
    );
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_SUCCESS;
  IfInfo = AllocatePool (Size);
  if (IfInfo == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = Ip4Config->GetData (
    Ip4Config, Ip4Config2DataTypeInterfaceInfo, &Size, IfInfo
    );
  if (!EFI_ERROR (Status))
    *Matches = (BOOLEAN)(StrCmp (Name, IfInfo->Name) == 0);
  FreePool (IfInfo);
  return Status;
}

STATIC
EFI_STATUS
Iperf3Ipv6InterfaceNameMatches (
  IN EFI_HANDLE  Controller,
  IN CHAR16      *Name,
  OUT BOOLEAN    *Matches
  )
{
  EFI_IP6_CONFIG_PROTOCOL       *Ip6Config;
  EFI_IP6_CONFIG_INTERFACE_INFO *IfInfo;
  EFI_STATUS Status;
  UINTN Size;
  *Matches = FALSE;
  Status = gBS->HandleProtocol (
    Controller, &gEfiIp6ConfigProtocolGuid, (VOID **)&Ip6Config
    );
  if (EFI_ERROR (Status))
    return Status;
  Size = 0;
  Status = Ip6Config->GetData (
    Ip6Config, Ip6ConfigDataTypeInterfaceInfo, &Size, NULL
    );
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_SUCCESS;
  IfInfo = AllocatePool (Size);
  if (IfInfo == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = Ip6Config->GetData (
    Ip6Config, Ip6ConfigDataTypeInterfaceInfo, &Size, IfInfo
    );
  if (!EFI_ERROR (Status))
    *Matches = (BOOLEAN)(StrCmp (Name, IfInfo->Name) == 0);
  FreePool (IfInfo);
  return Status;
}

EFI_STATUS
Iperf3ControllerMatchesInterface (
  IN EFI_HANDLE         Controller,
  IN IPERF3_IP_VERSION  Version,
  IN CHAR16             *Name,
  OUT BOOLEAN           *Matches
  )
{
  EFI_STATUS Status;
  BOOLEAN Matched;
  if (Controller == NULL || Name == NULL || Matches == NULL)
    return EFI_INVALID_PARAMETER;
  *Matches = FALSE;
  if (Version == Iperf3IpVersion4)
    Status = Iperf3Ipv4InterfaceNameMatches (Controller, Name, Matches);
  else if (Version == Iperf3IpVersion6)
    Status = Iperf3Ipv6InterfaceNameMatches (Controller, Name, Matches);
  else
    return EFI_INVALID_PARAMETER;
  if (EFI_ERROR (Status) || *Matches)
    return Status;
  if (Version == Iperf3IpVersion4)
    Status = Iperf3Ipv6InterfaceNameMatches (Controller, Name, &Matched);
  else
    Status = Iperf3Ipv4InterfaceNameMatches (Controller, Name, &Matched);
  if (!EFI_ERROR (Status) && Matched)
    *Matches = TRUE;
  return Status;
}

EFI_STATUS
Iperf3FindControllerByName (
  IN IPERF3_IP_VERSION Version,
  IN CHAR16            *Name,
  OUT EFI_HANDLE       *Controller
  )
{
  EFI_GUID *BindingGuid;
  EFI_HANDLE *Handles;
  EFI_STATUS Status;
  UINTN Count, Index;
  BOOLEAN Matches;
  if (Name == NULL || Controller == NULL)
    return EFI_INVALID_PARAMETER;
  *Controller = NULL;
  if (Version == Iperf3IpVersion4)
    BindingGuid = &gEfiIp4ServiceBindingProtocolGuid;
  else if (Version == Iperf3IpVersion6)
    BindingGuid = &gEfiIp6ServiceBindingProtocolGuid;
  else
    return EFI_INVALID_PARAMETER;
  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (
    ByProtocol, BindingGuid, NULL, &Count, &Handles
    );
  if (EFI_ERROR (Status))
    return Status;
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (
          Iperf3ControllerMatchesInterface (
            Handles[Index], Version, Name, &Matches
            )
          ))
      continue;
    if (Matches) {
      *Controller = Handles[Index];
      Status = EFI_SUCCESS;
      break;
    }
  }
  FreePool (Handles);
  return Status;
}

EFI_STATUS
Iperf3TcpConnect (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_HANDLE         Controller OPTIONAL,
  IN IPERF3_ADDRESS     *RemoteAddress,
  IN UINT16             RemotePort,
  IN UINT32             WindowSize,
  OUT IPERF3_TCP_SOCKET *Socket
  )
{
  EFI_STATUS Status;
  if (RemoteAddress == NULL || Socket == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem (Socket, sizeof (*Socket));
  Socket->Version = RemoteAddress->Version;
  if (RemoteAddress->Version == Iperf3IpVersion4)
    Status = Iperf3Tcp4Connect (
      ImageHandle, Controller, &RemoteAddress->Address.V4,
      RemotePort, WindowSize, &Socket->Socket.V4
    );
  else if (RemoteAddress->Version == Iperf3IpVersion6)
    Status = Iperf3Tcp6Connect (
      ImageHandle, Controller, &RemoteAddress->Address.V6,
      RemotePort, WindowSize, &Socket->Socket.V6
    );
  else
    return EFI_INVALID_PARAMETER;
  if (EFI_ERROR (Status)) {
    ZeroMem (Socket, sizeof (*Socket));
    return Status;
  }
  if (Socket->Version == Iperf3IpVersion4) {
    Socket->Controller = Socket->Socket.V4.Controller;
    CopyMem (&Socket->Endpoint, &Socket->Socket.V4.Endpoint, sizeof (Socket->Endpoint));
  } else {
    Socket->Controller = Socket->Socket.V6.Controller;
    CopyMem (&Socket->Endpoint, &Socket->Socket.V6.Endpoint, sizeof (Socket->Endpoint));
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Iperf3ConnectResolvedFamily (
  IN EFI_HANDLE          ImageHandle,
  IN OUT IPERF3_CONFIG   *Config,
  IN IPERF3_IP_VERSION   Version,
  OUT IPERF3_TCP_SOCKET  *Socket
  )
{
  EFI_GUID *BindingGuid;
  EFI_HANDLE *Handles;
  IPERF3_ADDRESS *Addresses;
  EFI_STATUS Status, LastStatus, MediaState;
  UINTN Count, Index, AddressCount, AddressIndex;
  BOOLEAN Matches;
  BindingGuid = Version == Iperf3IpVersion4 ?
    &gEfiTcp4ServiceBindingProtocolGuid :
    &gEfiTcp6ServiceBindingProtocolGuid;
  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, BindingGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status))
    return Status;
  LastStatus = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (Config->InterfaceName != NULL) {
      Status = Iperf3ControllerMatchesInterface (
        Handles[Index], Version, Config->InterfaceName, &Matches
        );
      if (EFI_ERROR (Status) || !Matches) {
        LastStatus = EFI_NOT_FOUND;
        continue;
      }
    }
    Status = NetLibDetectMediaWaitTimeout (Handles[Index], 0, &MediaState);
    if (!EFI_ERROR (Status) && MediaState != EFI_SUCCESS) {
      LastStatus = MediaState;
      continue;
    }
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    Addresses = NULL;
    AddressCount = 0;
    Status = Iperf3ResolveOnController (
      Handles[Index], Version, Config->ServerName,
      &Addresses, &AddressCount
    );
    if (EFI_ERROR (Status)) {
      LastStatus = Status;
      continue;
    }
    for (AddressIndex = 0; AddressIndex < AddressCount; AddressIndex++) {
      Status = Iperf3TcpConnect (
        ImageHandle, Handles[Index], &Addresses[AddressIndex],
        Config->ServerPort, Config->WindowSize, Socket
      );
      if (!EFI_ERROR (Status)) {
        CopyMem (&Config->ServerAddress, &Addresses[AddressIndex], sizeof (Config->ServerAddress));
        FreePool (Addresses);
        FreePool (Handles);
        return EFI_SUCCESS;
      }
      LastStatus = Status;
    }
    FreePool (Addresses);
  }
  FreePool (Handles);
  return LastStatus;
}

EFI_STATUS
Iperf3TcpConnectServer (
  IN EFI_HANDLE          ImageHandle,
  IN OUT IPERF3_CONFIG   *Config,
  OUT IPERF3_TCP_SOCKET  *Socket
  )
{
  EFI_STATUS Status;
  EFI_HANDLE Controller;
  if (Config->ServerAddress.Version != Iperf3IpVersionAny) {
    if (Config->InterfaceName != NULL) {
      Status = Iperf3FindControllerByName (
        Config->ServerAddress.Version, Config->InterfaceName, &Controller
        );
      if (EFI_ERROR (Status))
        return Status;
      return Iperf3TcpConnect (
        ImageHandle, Controller, &Config->ServerAddress,
        Config->ServerPort, Config->WindowSize, Socket
        );
    }
    return Iperf3TcpConnect (
      ImageHandle, NULL, &Config->ServerAddress,
      Config->ServerPort, Config->WindowSize, Socket
      );
  }
  if (Config->RequestedVersion != Iperf3IpVersion6) {
    Status = Iperf3ConnectResolvedFamily (ImageHandle, Config, Iperf3IpVersion4, Socket);
    if (!EFI_ERROR (Status))
      return EFI_SUCCESS;
    if (Config->RequestedVersion == Iperf3IpVersion4)
      return Status;
  }
  return Iperf3ConnectResolvedFamily (ImageHandle, Config, Iperf3IpVersion6, Socket);
}

EFI_STATUS
Iperf3TcpWrite (
  IN IPERF3_TCP_SOCKET *Socket,
  IN CONST VOID        *Buffer,
  IN UINT32            Length,
  IN UINT32            TimeoutMs
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Tcp4Write (&Socket->Socket.V4, Buffer, Length, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Tcp6Write (&Socket->Socket.V6, Buffer, Length, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
Iperf3TcpTxRequestInit (
  IN IPERF3_TCP_SOCKET      *Socket,
  OUT IPERF3_TCP_TX_REQUEST *Request
  )
{
  EFI_STATUS Status;
  if (Socket == NULL || Request == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem (Request, sizeof (*Request));
  Request->Version = Socket->Version;
  if (Socket->Version == Iperf3IpVersion4)
    Status = Iperf3Tcp4TxRequestInit (&Request->Request.V4);
  else if (Socket->Version == Iperf3IpVersion6)
    Status = Iperf3Tcp6TxRequestInit (&Request->Request.V6);
  else
    Status = EFI_INVALID_PARAMETER;
  if (EFI_ERROR (Status))
    ZeroMem (Request, sizeof (*Request));
  return Status;
}

EFI_STATUS
Iperf3TcpWriteAsync (
  IN IPERF3_TCP_SOCKET         *Socket,
  IN OUT IPERF3_TCP_TX_REQUEST *Request,
  IN CONST VOID                *Buffer,
  IN UINT32                    Length
  )
{
  if (Socket->Version != Request->Version)
    return EFI_INVALID_PARAMETER;
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Tcp4WriteAsync (
      &Socket->Socket.V4, &Request->Request.V4, Buffer, Length
    );
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Tcp6WriteAsync (
      &Socket->Socket.V6, &Request->Request.V6, Buffer, Length
    );
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
Iperf3TcpWriteWait (
  IN IPERF3_TCP_SOCKET         *Socket,
  IN OUT IPERF3_TCP_TX_REQUEST *Request,
  IN UINT32                    TimeoutMs
  )
{
  if (Socket->Version != Request->Version)
    return EFI_INVALID_PARAMETER;
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Tcp4WriteWait (&Socket->Socket.V4, &Request->Request.V4, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Tcp6WriteWait (&Socket->Socket.V6, &Request->Request.V6, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

VOID
Iperf3TcpTxRequestFree (
  IN IPERF3_TCP_SOCKET         *Socket,
  IN OUT IPERF3_TCP_TX_REQUEST *Request
  )
{
  if (Socket == NULL || Request == NULL || Socket->Version != Request->Version)
    return;
  if (Socket->Version == Iperf3IpVersion4)
    Iperf3Tcp4TxRequestFree (&Socket->Socket.V4, &Request->Request.V4);
  else if (Socket->Version == Iperf3IpVersion6)
    Iperf3Tcp6TxRequestFree (&Socket->Socket.V6, &Request->Request.V6);
  ZeroMem (Request, sizeof (*Request));
}

EFI_STATUS
Iperf3TcpRead (
  IN IPERF3_TCP_SOCKET *Socket,
  OUT VOID             *Buffer,
  IN OUT UINT32        *Length,
  IN UINT32            TimeoutMs
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Tcp4Read (&Socket->Socket.V4, Buffer, Length, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Tcp6Read (&Socket->Socket.V6, Buffer, Length, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
Iperf3TcpReadExact (
  IN IPERF3_TCP_SOCKET *Socket,
  OUT VOID             *Buffer,
  IN UINT32            Length,
  IN UINT32            TimeoutMs
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Tcp4ReadExact (&Socket->Socket.V4, Buffer, Length, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Tcp6ReadExact (&Socket->Socket.V6, Buffer, Length, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
Iperf3TcpDrain (
  IN IPERF3_TCP_SOCKET *Socket,
  IN UINT32            TimeoutMs
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Tcp4Drain (&Socket->Socket.V4, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Tcp6Drain (&Socket->Socket.V6, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
Iperf3TcpPoll (
  IN IPERF3_TCP_SOCKET *Socket
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Socket->Socket.V4.Protocol->Poll (Socket->Socket.V4.Protocol);
  if (Socket->Version == Iperf3IpVersion6)
    return Socket->Socket.V6.Protocol->Poll (Socket->Socket.V6.Protocol);
  return EFI_INVALID_PARAMETER;
}

VOID
Iperf3TcpClose (
  IN OUT IPERF3_TCP_SOCKET *Socket,
  IN BOOLEAN               AbortOnClose
  )
{
  if (Socket == NULL)
    return;
  if (Socket->Version == Iperf3IpVersion4)
    Iperf3Tcp4Close (&Socket->Socket.V4, AbortOnClose);
  else if (Socket->Version == Iperf3IpVersion6)
    Iperf3Tcp6Close (&Socket->Socket.V6, AbortOnClose);
  ZeroMem (Socket, sizeof (*Socket));
}

EFI_STATUS
Iperf3UdpOpen (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_HANDLE         Controller,
  IN IPERF3_ADDRESS     *RemoteAddress,
  IN UINT16             RemotePort,
  OUT IPERF3_UDP_SOCKET *Socket
  )
{
  EFI_STATUS Status;
  if (RemoteAddress == NULL || Socket == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem (Socket, sizeof (*Socket));
  Socket->Version = RemoteAddress->Version;
  Socket->Controller = Controller;
  if (RemoteAddress->Version == Iperf3IpVersion4)
    Status = Iperf3Udp4Open (
      ImageHandle, Controller, &RemoteAddress->Address.V4,
      RemotePort, &Socket->Socket.V4
    );
  else if (RemoteAddress->Version == Iperf3IpVersion6)
    Status = Iperf3Udp6Open (
      ImageHandle, Controller, &RemoteAddress->Address.V6,
      RemotePort, &Socket->Socket.V6
    );
  else
    return EFI_INVALID_PARAMETER;
  if (EFI_ERROR (Status)) {
    ZeroMem (Socket, sizeof (*Socket));
    return Status;
  }
  if (Socket->Version == Iperf3IpVersion4)
    CopyMem (&Socket->Endpoint, &Socket->Socket.V4.Endpoint, sizeof (Socket->Endpoint));
  else
    CopyMem (&Socket->Endpoint, &Socket->Socket.V6.Endpoint, sizeof (Socket->Endpoint));
  return EFI_SUCCESS;
}

EFI_STATUS
Iperf3UdpWrite (
  IN IPERF3_UDP_SOCKET *Socket,
  IN CONST VOID        *Buffer,
  IN UINT32            Length,
  IN UINT32            TimeoutMs
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Udp4Write (&Socket->Socket.V4, Buffer, Length, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Udp6Write (&Socket->Socket.V6, Buffer, Length, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
Iperf3UdpRead (
  IN IPERF3_UDP_SOCKET *Socket,
  OUT VOID             *Buffer,
  IN OUT UINT32        *Length,
  IN UINT32            TimeoutMs
  )
{
  if (Socket->Version == Iperf3IpVersion4)
    return Iperf3Udp4Read (&Socket->Socket.V4, Buffer, Length, TimeoutMs);
  if (Socket->Version == Iperf3IpVersion6)
    return Iperf3Udp6Read (&Socket->Socket.V6, Buffer, Length, TimeoutMs);
  return EFI_INVALID_PARAMETER;
}

VOID
Iperf3UdpClose (
  IN OUT IPERF3_UDP_SOCKET *Socket
  )
{
  if (Socket == NULL)
    return;
  if (Socket->Version == Iperf3IpVersion4)
    Iperf3Udp4Close (&Socket->Socket.V4);
  else if (Socket->Version == Iperf3IpVersion6)
    Iperf3Udp6Close (&Socket->Socket.V6);
  ZeroMem (Socket, sizeof (*Socket));
}
