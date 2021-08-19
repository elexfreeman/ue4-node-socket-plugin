// Fill out your copyright notice in the Description page of Project Settings.

#include "NodeSocketAC.h"

// Sets default values for this component's properties
UNodeSocketAC::UNodeSocketAC()
{
    // Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
    // off to improve performance if you don't need them.
    PrimaryComponentTick.bCanEverTick = true;

    // ...

    // set the name of the socket
    ClientSocketName = FString(TEXT("ue4-tcp-client"));

    // reset the socket
    ClientSocket = nullptr;

    // maximum buffer size
    nBufferMaxSize = 2 * 1024 * 1024; // 2 Mb
}

// Called when the game starts
void UNodeSocketAC::BeginPlay()
{
    Super::BeginPlay();

    // ...
}

// Called every frame
void UNodeSocketAC::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    fMsgEvent();
    // ...
}

void UNodeSocketAC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CloseSocket();
}

/**
 * Connects to server
 */
void UNodeSocketAC::ConnectToServer(const FString &InIP, const int32 InPort)
{

    bool bIsValid =false;
    // we form the connection address
    this->RemoteAdress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

    UE_LOG(LogTemp, Log, TEXT("TCP address try to connect <%s:%d>"), *InIP, InPort);

    this->RemoteAdress->SetIp(*InIP, bIsValid);
    this->RemoteAdress->SetPort(InPort);

    // we check the validity of the connection address
    if (!bIsValid)
    {
        UE_LOG(LogTemp, Error, TEXT("TCP address is invalid <%s:%d>"), *InIP, InPort);
        return;
    }

    // Get the socket subsystem
    this->ClientSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, ClientSocketName, false);

    // Set the size of the send / receive buffer
    this->ClientSocket->SetSendBufferSize(this->nBufferMaxSize, this->nBufferMaxSize);
    this->ClientSocket->SetReceiveBufferSize(this->nBufferMaxSize, this->nBufferMaxSize);

    bIsConnected = this->ClientSocket->Connect(*this->RemoteAdress);

    // if you connected Broadcast event
    if (bIsConnected)
    {
        this->OnConnected.Broadcast();
        // we say that we are ready to receive data
        bShouldReceiveData = true;
    }

    // Data listener
    ClientConnectionFinishedFuture =
        Async(EAsyncExecution::Thread,
              [&]()
              {
                  uint32 BufferSize = 0;
                  TArray<uint8> ReceiveBuffer;
                  FString ResultString;

                  // we start an endless loop for receiving data
                  while (bShouldReceiveData)
                  {
                      // if there is data
                      if (this->ClientSocket->HasPendingData(BufferSize))
                      {
                          // set buffer size
                          ReceiveBuffer.SetNumUninitialized(BufferSize);

                          int32 Read = 0;
                          this->ClientSocket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), Read);

                          // send buffer to event
                          this->OnReceivedBytes.Broadcast(ReceiveBuffer);

                          // send string to event
                          // FString sMsg = fBytesToString(ReceiveBuffer);
                          // OnReceivedStr.Broadcast(msgStr);
                          this->aReserveQ.Enqueue(fBytesToString(ReceiveBuffer));

                          // log message
                      }
                      // skip 1 tick
                      this->ClientSocket->Wait(ESocketWaitConditions::WaitForReadOrWrite, FTimespan(1));
                  }
              });
}

/**
 * Closes socket
 * */
void UNodeSocketAC::CloseSocket()
{
    // if there is a socket
    if (ClientSocket)
    {
        // stop receiving data
        bShouldReceiveData = false;

        // end the connection
        ClientConnectionFinishedFuture.Get();
        this->ClientSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(this->ClientSocket);
        this->ClientSocket = nullptr;
        UE_LOG(LogTemp, Log, TEXT("UNodeSocketAC disconnect"));
    }
}

/**
 * Sends bytes to the server
 * */
bool UNodeSocketAC::Emit(const TArray<uint8> &Bytes)
{
    bool resp = false;

    // check if there is a connection
    if (this->ClientSocket && this->ClientSocket->GetConnectionState() == SCS_Connected)
    {
        int32 BytesSent = 0; // how many bytes sent
        resp = this->ClientSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
    }
    return resp;
}

/**
 * Sends bytes to the server
 * */
bool UNodeSocketAC::EmitStr(const FString &str)
{
    bool resp = false;
    UE_LOG(LogTemp, Log, TEXT("Try send msg: %s"), *str);

    // check if there is a connection
    if (ClientSocket && ClientSocket->GetConnectionState() == SCS_Connected)
    {
        int32 BytesSent = 0; // how many bytes sent

        // convert string to any charset
        FTCHARToUTF8 Converted(*str);
        (uint8 *)Converted.Get(), Converted.Length();

        resp = this->ClientSocket->Send((uint8 *)Converted.Get(), Converted.Length(), BytesSent);
    }
    return resp;
}

/**
* Converts bytes to strings
*/
FString UNodeSocketAC::fBytesToString(const TArray<uint8> &InArray)
{
    FString ResultString;
    FFileHelper::BufferToString(ResultString, InArray.GetData(), InArray.Num());
    return ResultString;
}

/**
* Converts strings to bytes
*/
TArray<uint8> UNodeSocketAC::fStringToBytes(FString InString)
{
    TArray<uint8> ResultBytes;
    ResultBytes.Append((uint8 *)TCHAR_TO_UTF8(*InString), InString.Len());

    UE_LOG(LogTemp, Log, TEXT("str len: %d"), InString.Len());
    UE_LOG(LogTemp, Log, TEXT("array len: %d"), ResultBytes.Num());

    return ResultBytes;
}

void UNodeSocketAC::fMsgEvent()
{
    FString s;
    if (!this->aReserveQ.IsEmpty())
    {
        this->aReserveQ.Dequeue(s);
        this->OnReceivedStr.Broadcast(s);
        UE_LOG(LogTemp, Log, TEXT("Broadcast data: %s"), *(s));
    }
}