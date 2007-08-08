#if defined(WIN32)
#define _CRTDBG_MAP_ALLOC
#endif

#include "ILibParsers.h"
#include "ILibAsyncServerSocket.h"
#include "ILibAsyncSocket.h"
#include "RedirectionConstants.h"
#include "Redirection.h"
#include "SBConstants.h"

#if defined(WIN32)
	#include <crtdbg.h>
#endif

#define DEBUGSTATEMENT_R(x) x
#define SOMEMORE_DEBUGSTATEMENT(x)

struct Redirection_StateModule
{
  void (*PreSelect)(void* object,fd_set *readset, fd_set *writeset, fd_set *errorset, int* blocktime);
  void (*PostSelect)(void* object,int slct, fd_set *readset, fd_set *writeset, fd_set *errorset);
  void (*Destroy)(void* object);

  int Port;
  void *Chain;
  void *ServerSocket;
  void *User;
  void *Tag;

  int EnableFlag;

  void (*OnSession)(struct Redirection_Session *rs, void *User);
};

static struct Redirection_StateModule *rsm = NULL;

void RedirectionClient_OnData(ILibAsyncSocket_SocketModule socketModule,char* buffer,int *p_beginPointer, int endPointer,void (**InterruptPtr)(void *socketModule, void *user), void **user, int *PAUSE);
void RedirectionClient_OnConnect(ILibAsyncSocket_SocketModule socketModule, int Connected, void *user);
void RedirectionClient_OnDisconnectSink(ILibAsyncSocket_SocketModule socketModule, void *user);
void RedirectionClient_OnSendOK(ILibAsyncSocket_SocketModule socketModule, void *user);
void RedirectionClient_OnInterrupt(ILibAsyncSocket_SocketModule socketModule, void *user);

void Redirection_OnConnect(void *AsyncServerSocketModule, void *ConnectionToken,void **user)
{
  //  struct Redirection_StateModule *rsm = (struct Redirection_StateModule*)ILibAsyncServerSocket_GetTag(AsyncServerSocketModule);

  if (rsm->EnableFlag == 1)
  {
    struct Redirection_Session *rs = (struct Redirection_Session*)malloc(sizeof(struct Redirection_Session));

    memset(rs,0,sizeof(struct Redirection_Session));
    rs->ServerSocket = ConnectionToken;
    rs->User = rsm->User;
    rs->ClientSocket = NULL;
    rs->SourceIP = ILibAsyncSocket_GetRemoteInterface(ConnectionToken);
    rs->SentBytes = 0;
    rs->ReceivedBytes = 0;

    *user = rs;

    if(rsm->OnSession!=NULL)
    {
      rsm->OnSession(rs,rsm->User);
    }
  }
  else
  {
    ILibAsyncSocket_Disconnect(ConnectionToken);  
  }
}

void Redirection_OnDisconnect(void *AsyncServerSocketModule, void *ConnectionToken, void *user)
{
  struct Redirection_Session *rs = (struct Redirection_Session*)user;

  if (rs != NULL)
  {
    rs->ServerSocket = NULL;
    if (rs->OnDisconnect != NULL)
      rs->OnDisconnect(rs);

    if (rs->ClientSocket == NULL)
      free(rs);
  }
  SOMEMORE_DEBUGSTATEMENT(printf("%d in %s\n", __LINE__, __FUNCTION__));
}

void Redirection_OnReceive(void *AsyncServerSocketModule, void *ConnectionToken,char* buffer,int *p_beginPointer, int endPointer,void (**OnInterrupt)(void *AsyncServerSocketMoudle, void *ConnectionToken, void *user), void **user, int *PAUSE)
{
  char header[REDIRECT_BUFFER_SIZE];
  struct Redirection_Session *rs = (struct Redirection_Session*)(*user);

  if (*p_beginPointer != 0)
  {
    DEBUGSTATEMENT_R(printf("*p_beginPointer != 0 in %s, This has to be an error", __FUNCTION__));
    exit(0);
  }

  if (endPointer < 4)
    return;

  SOMEMORE_DEBUGSTATEMENT(printf("R %02x of length %d on %d in %s\n", buffer[0], endPointer, __LINE__, __FUNCTION__));

  switch (buffer[0])
  {
  case START_REDIRECTION_SESSION:
    {
      if (endPointer >= START_REDIRECTION_SESSION_LENGTH)
      {
        unsigned int sessionType;

        memcpy(&sessionType, buffer + 4, 4);

        if (sessionType == SOL_SESSION)
        {
          rs->SessionType = sessionType;        
          strcpy(rs->SessionTypeString, "SOL");
          strcpy(rs->SessionCategoryString, "Passthrough");
        }
        else if (sessionType == IDER_SESSION)
        {
          rs->SessionType = sessionType;        
          strcpy(rs->SessionTypeString, "IDER");
          strcpy(rs->SessionCategoryString, "Passthrough");
        }

        *p_beginPointer += START_REDIRECTION_SESSION_LENGTH;    

        //TODO: check whether the status is SUCCESS or FAILURE
        header[0] = START_REDIRECTION_SESSION_REPLY;
        //TODO: Test if "SOL " or "IDER"
        header[1] = STATUS_SUCCESS;
        header[2] = 0;
        header[3] = 0;
        header[4] = 0x01; //Protocol Rev Major
        header[5] = 0x00; //Protocol Rev Minor
        header[6] = 0x02; //Session Manager Firmware Rev Major
        header[7] = 0x00; //Session Manager Firmware Rev Minor
        memcpy(header + 8, &SESSION_MANAGER_OEM_IANA_NUMBER, 4);
        header[12] = 0;

        SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], START_REDIRECTION_SESSION_REPLY_LENGTH, __LINE__, __FUNCTION__));
        ILibAsyncSocket_Send(ConnectionToken, header, START_REDIRECTION_SESSION_REPLY_LENGTH,ILibAsyncSocket_MemoryOwnership_USER);
      }
      break;
    }
  case AUTHENTICATE_SESSION:
    {
      int messageLength = 0;
      unsigned int adLength = 0;

      if (endPointer < 9)
        break;

      memcpy(&adLength, buffer + 5, 4);
      messageLength = 9 + adLength;

      if (endPointer >= messageLength)
      {
        //TODO: check whether the status is SUCCESS or FAILURE
        header[0] = AUTHENTICATE_SESSION_REPLY;
        header[1] = STATUS_SUCCESS;
        header[2] = 0;
        header[3] = 0;
        if (buffer[4] == 0x00)  //AuthenticationType: Query;      
        {
          adLength = 1;
          header[4] = 0x00; //AuthenticationType: Query;
          memcpy(header + 5, &adLength, 4);
          header[9] = 0x01; //AuthenticationType Username/Password supported;          ;

          SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], 10, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(ConnectionToken, header, 10, ILibAsyncSocket_MemoryOwnership_USER);
        }
        else if (buffer[4] == 0x01) //AuthenticationType: Username/Password;
        {
          unsigned char uLength = buffer[9];
          unsigned char pLength = buffer[10 + uLength];
          char password[128];

          adLength = 0;

          strncpy(rs->SourceUsername, buffer + 10, uLength);
          rs->SourceUsername[uLength] = '\0';
          strncpy(password, buffer + 11 + uLength, pLength);
          password[pLength] = '\0';

          if (-1 == (rs->AuthenticateUserAndGetSystemDetails(rs, password)))
          {
            ILibAsyncSocket_Disconnect(ConnectionToken);
          }
          else
          {
            header[4] = 0x01; //AuthenticationType: Username/Password;      
            memset(header + 5, 0, 4);
            SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], 9, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(ConnectionToken, header, 9, ILibAsyncSocket_MemoryOwnership_USER);
          }
        }

        *p_beginPointer += messageLength;      
      }
      break;
    }  
  case START_SOL_REDIRECTION:
  case START_IDER_REDIRECTION:
    {
      int MESSAGE_LENGTH = (buffer[0] == START_SOL_REDIRECTION)? START_SOL_REDIRECTION_LENGTH : START_IDER_REDIRECTION_LENGTH;

      if (endPointer >= MESSAGE_LENGTH)
      {
        struct hostent *hostaddr = NULL;
        int remoteInterface = INADDR_NONE;

        //TODO: check whether the status is SUCCESS or FAILURE
        //TODO: check whether authorized
        ///        struct Redirection_StateModule *rsm = (struct Redirection_StateModule*)ILibAsyncServerSocket_GetTag(AsyncServerSocketModule);

        *p_beginPointer += MESSAGE_LENGTH;      

        rs->ClientSocket = ILibCreateAsyncSocketModule(
          rsm->Chain,
          REDIRECT_BUFFER_SIZE,
          &RedirectionClient_OnData,
          &RedirectionClient_OnConnect,
          &RedirectionClient_OnDisconnectSink,
          &RedirectionClient_OnSendOK);

        if ((remoteInterface = inet_addr(rs->AmtIP)) == INADDR_NONE)
        {            
          if ((hostaddr = gethostbyname(rs->AmtIP)) != NULL) 
            memcpy(&remoteInterface, hostaddr->h_addr, 4);
        }

        if (remoteInterface != INADDR_NONE)
        {
          ILibAsyncSocket_ConnectTo(
            rs->ClientSocket,
            0,
            remoteInterface,
            rs->AmtPort,
            NULL,
            rs);

          rs->consoleSequenceNo = 0;

          if (rs->AmtPort == AMT_REDIRECTION_TLS_PORT)
          {
            int ret1 = 0, ret2 = 0, ret3 = 0;

            rs->ssl_ctx = (void *)ssl_ctx_new(SSL_NO_DEFAULT_KEY, 1);
            ret1 = ssl_obj_load((SSL_CTX*)rs->ssl_ctx, SSL_OBJ_RSA_KEY,      SWITCHBOX_CLIENT_KEY_FILE, NULL);
            ret2 = ssl_obj_load((SSL_CTX*)rs->ssl_ctx, SSL_OBJ_X509_CERT,    SWITCHBOX_CLIENT_CERT_FILE, NULL);
            ret3 = ssl_obj_load((SSL_CTX*)rs->ssl_ctx, SSL_OBJ_X509_CACERT,  SWITCHBOX_CLIENT_CACERT_FILE, NULL);

            if (ret1 != SSL_OK || ret2 != SSL_OK || ret3 != SSL_OK)
            {
              DEBUGSTATEMENT_R(printf("Error: Private key '%s' = %d or Certificate '%s' = %d or CACertificate '%s' = %d is undefined.\n", SWITCHBOX_CLIENT_KEY_FILE, ret1, SWITCHBOX_CLIENT_CERT_FILE, ret2, SWITCHBOX_CLIENT_CACERT_FILE, ret3));

              ssl_ctx_free((SSL_CTX*)rs->ssl_ctx);
              rs->ssl_ctx = (void *)ssl_ctx_new(SSL_NO_DEFAULT_KEY | SSL_SERVER_VERIFY_LATER, 1);  
            }

            ILibAsyncSocket_SetSSLClient(rs->ClientSocket, (SSL_CTX *)rs->ssl_ctx);
          }
        }
      }
      break;
    }  
    //Forward it on the ClientSocket to Host
  case SOL_HEARTBEAT:
  case IDER_HEARTBEAT:
  case SOL_KEEP_ALIVE_PING:
  case IDER_KEEP_ALIVE_PING:
    {
      if (endPointer >= HEARTBEAT_LENGTH)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], HEARTBEAT_LENGTH, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, HEARTBEAT_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += HEARTBEAT_LENGTH;
        }
        *p_beginPointer += HEARTBEAT_LENGTH;      
      }
      break;
    }
  case SOL_DATA_TO_HOST:
    {
      int messageLength = 0;
      unsigned short dataLength = 0;

      if (endPointer < 10)
        break;

      memcpy(&dataLength, buffer + 8, 2);
      messageLength = 10 + dataLength;

      if (endPointer >= messageLength)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], messageLength, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, messageLength, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += messageLength;
        }
        *p_beginPointer += messageLength;      
      }
      break;
    }
  case IDER_RESET_OCCURED_RESPONSE:
    {
      if (endPointer >= IDER_RESET_OCCURED_RESPONSE_LENGTH)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], IDER_RESET_OCCURED_RESPONSE_LENGTH, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, IDER_RESET_OCCURED_RESPONSE_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += IDER_RESET_OCCURED_RESPONSE_LENGTH;
        }
        *p_beginPointer += IDER_RESET_OCCURED_RESPONSE_LENGTH;      
      }
      break;
    }
  case IDER_DISABLE_ENABLE_FEATURES:
    {
      int messageLength = 0;

      if (endPointer < 9)
        break;

      if (buffer[8] == 0x01)  //GetSupportedFeatures
        messageLength = 9;
      else if ((buffer[8] == 0x02) || (buffer[8] == 0x03)) //Disable/Enable IDER Registers related sub commands
        messageLength = 13;

      if (endPointer >= messageLength)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], messageLength, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, messageLength, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += messageLength;
        }
        *p_beginPointer += messageLength;      
      }
      break;
    }
  case IDER_COMMAND_END_RESPONSE:
    {
      if (endPointer >= IDER_COMMAND_END_RESPONSE_LENGTH)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], IDER_COMMAND_END_RESPONSE_LENGTH, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, IDER_COMMAND_END_RESPONSE_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += IDER_COMMAND_END_RESPONSE_LENGTH;
        }
        *p_beginPointer += IDER_COMMAND_END_RESPONSE_LENGTH;      
      }
      break;
    }
  case IDER_GET_DATA_FROM_HOST:
    {
      if (endPointer >= IDER_GET_DATA_FROM_HOST_LENGTH)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], IDER_GET_DATA_FROM_HOST_LENGTH, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, IDER_GET_DATA_FROM_HOST_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += IDER_GET_DATA_FROM_HOST_LENGTH;
        }
        *p_beginPointer += IDER_GET_DATA_FROM_HOST_LENGTH;      
      }
      break;
    }
  case IDER_DATA_TO_HOST:
    {
      int messageLength = 0;
      unsigned short dataLength = 0;

      if (endPointer < 11)
        break;

      memcpy(&dataLength, buffer + 9, 2);
      messageLength = 34 + dataLength;

      if (endPointer >= messageLength)
      {
        if (rs->ClientSocket != NULL)
        {
          SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], messageLength, __LINE__, __FUNCTION__));
          ILibAsyncSocket_Send(rs->ClientSocket, buffer, messageLength, ILibAsyncSocket_MemoryOwnership_USER);
          rs->SentBytes += messageLength;
        }
        *p_beginPointer += messageLength;      
      }
      break;
    }
  case END_SOL_REDIRECTION:
  case END_IDER_REDIRECTION:
    {
      if (endPointer >= END_SOL_REDIRECTION_LENGTH && rs->ClientSocket != NULL)
      {
        SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], END_SOL_REDIRECTION_LENGTH, __LINE__, __FUNCTION__));
        ILibAsyncSocket_Send(rs->ClientSocket, buffer, END_SOL_REDIRECTION_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
        *p_beginPointer += END_SOL_REDIRECTION_LENGTH;      
      }
      break;
    }
  case END_REDIRECTION_SESSION:
    {
      if (endPointer >= END_REDIRECTION_SESSION_LENGTH && rs->ClientSocket != NULL)
      {
        SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], END_REDIRECTION_SESSION_LENGTH, __LINE__, __FUNCTION__));
        ILibAsyncSocket_Send(rs->ClientSocket, buffer, END_REDIRECTION_SESSION_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
        *p_beginPointer += END_REDIRECTION_SESSION_LENGTH;      
        ILibAsyncSocket_Disconnect(ConnectionToken);
      }
      break;
    }
  default:
    {
      DEBUGSTATEMENT_R(printf("This is an error. Please report it. Reached Default on %d in %s\n", __LINE__, __FUNCTION__));
      ILibAsyncSocket_Disconnect(ConnectionToken);
      break;
    }
  }
}

void Redirection_OnInterrupt(ILibAsyncServerSocket_ServerModule AsyncServerSocketModule, ILibAsyncServerSocket_ConnectionToken ConnectionToken, void *user)
{
  SOMEMORE_DEBUGSTATEMENT(printf("%d in %s\n", __LINE__, __FUNCTION__));
}

void Redirection_OnSendOK(ILibAsyncServerSocket_ServerModule AsyncServerSocketModule, ILibAsyncServerSocket_ConnectionToken ConnectionToken, void *user)
{
  SOMEMORE_DEBUGSTATEMENT(printf("%d in %s\n", __LINE__, __FUNCTION__));
}

void RedirectionClient_OnConnect(ILibAsyncSocket_SocketModule socketModule, int Connected, void *user)
{
  struct Redirection_Session *rs = (struct Redirection_Session*)user;
  char header[START_REDIRECTION_SESSION_LENGTH];

  header[0] = START_REDIRECTION_SESSION;
  header[1] = 0;
  header[2] = 0;
  header[3] = 0;

  memcpy(header + 4, &(rs->SessionType), 4);

  SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], START_REDIRECTION_SESSION_LENGTH, __LINE__, __FUNCTION__));
  ILibAsyncSocket_Send(rs->ClientSocket, header, START_REDIRECTION_SESSION_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
}

void RedirectionClient_OnDisconnectSink(ILibAsyncSocket_SocketModule socketModule, void *user)
{
  struct Redirection_Session *rs = (struct Redirection_Session*)user;

  if (rs != NULL)
  {
    rs->ClientSocket = NULL;

    if (rsm->Port == AMT_REDIRECTION_TLS_PORT)
    {
      ssl_ctx_free((SSL_CTX *)rs->ssl_ctx);
    }

    if (rs->ServerSocket == NULL)
      free(rs);
  }
  SOMEMORE_DEBUGSTATEMENT(printf("%d in %s\n", __LINE__, __FUNCTION__));
}

void RedirectionClient_OnData(ILibAsyncSocket_SocketModule  socketModule,char* buffer,int *p_beginPointer, int endPointer,void (**InterruptPtr)(void *socketModule, void *user), void **user, int *PAUSE)
{
  if (endPointer - *p_beginPointer != 0)  //required for TLS ..
  {
    char header[REDIRECT_BUFFER_SIZE];
    struct Redirection_Session *rs = (struct Redirection_Session*)(*user);

    if (*p_beginPointer != 0)
    {
      DEBUGSTATEMENT_R(printf("*p_beginPointer != 0 in %s, This has to be an error", __FUNCTION__));
      exit(0);
    }

    SOMEMORE_DEBUGSTATEMENT(printf("R %02x of length %d on %d in %s\n", buffer[0], endPointer, __LINE__, __FUNCTION__));

    if (endPointer < 4)
      return;

    switch (buffer[0])
    {
    case START_REDIRECTION_SESSION_REPLY:
      {
        if (endPointer >= START_REDIRECTION_SESSION_REPLY_LENGTH)
        {
          if ((rs != NULL) && (rs->ClientSocket != NULL))
          {
            //TODO: check whether the status is SUCCESS or FAILURE
            unsigned char uLength = (unsigned char)strlen(rs->AmtUsername);
            unsigned char pLength = (unsigned char)strlen(rs->AmtPassword);
            unsigned int adLength = 2 + uLength + pLength;

            header[0] = AUTHENTICATE_SESSION;
            header[1] = 0;
            header[2] = 0;
            header[3] = 0;
            header[4] = 0x01;
            memcpy(header + 5, &adLength, 4);
            memcpy(header + 9, &uLength, 1);
            memcpy(header + 10, rs->AmtUsername, uLength);
            memcpy(header + 10 + uLength, &pLength, 1);
            memcpy(header + 11 + uLength, rs->AmtPassword, pLength);

            SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], adLength + 9, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ClientSocket,header, adLength + 9,ILibAsyncSocket_MemoryOwnership_USER);
          }
          *p_beginPointer += START_REDIRECTION_SESSION_REPLY_LENGTH;      
        }
        break;
      }
    case AUTHENTICATE_SESSION_REPLY:
      {
        int messageLength = 0;
        unsigned int adLength = 0;

        if (endPointer < 9)
          break;

        memcpy(&adLength, buffer + 5, 4);
        messageLength = 9 + adLength;

        if (endPointer >= messageLength)
        {
          if ((rs != NULL) && (rs->ClientSocket != NULL))
          {
            char s[128];
            //TODO: check whether the status is SUCCESS or FAILURE
            if (rs->SessionType == SOL_SESSION)
            {
              header[0] = START_SOL_REDIRECTION;
              header[1] = 0;
              header[2] = 0;
              header[3] = 0;
              memset(header + 4, 0, 4); //Console Sequence Number = 0
              memcpy(header + 8, &MAX_TRANSMIT_BUFFER, 2);
              memcpy(header + 10, &TRANSMIT_BUFFER_TIMEOUT, 2);    
              memcpy(header + 12, &TRANSMIT_OVERFLOW_TIMEOUT, 2);    
              memcpy(header + 14, &HOST_SESSION_RX_TIMEOUT, 2);    
              memcpy(header + 16, &HOST_FIFO_RX_FLUSH_TIMEOUT, 2);    
              memcpy(header + 18, &HEARTBEAT_INTERVAL, 2);    
              memset(header + 20, 0, 4);    

              SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], START_SOL_REDIRECTION_LENGTH, __LINE__, __FUNCTION__));
              ILibAsyncSocket_Send(rs->ClientSocket,header, START_SOL_REDIRECTION_LENGTH,ILibAsyncSocket_MemoryOwnership_USER);
              sprintf(s, "SOL started for %s", rs->AmtUsername);
              rs->LogThis(s);
            }
            else if (rs->SessionType == IDER_SESSION)
            {
              header[0] = START_IDER_REDIRECTION;
              header[1] = 0;
              header[2] = 0;
              header[3] = 0;
              memset(header + 4, 0, 4);
              memcpy(header + 8, &HOST_SESSION_RX_TIMEOUT, 2);
              memcpy(header + 10, &TRANSMIT_OVERFLOW_TIMEOUT, 2);    
              memcpy(header + 12, &HEARTBEAT_INTERVAL, 2);    
              memset(header + 14, 0, 4);

              SOMEMORE_DEBUGSTATEMENT(printf("S %02x of length %d on %d in %s\n", header[0], START_IDER_REDIRECTION_LENGTH, __LINE__, __FUNCTION__));
              ILibAsyncSocket_Send(rs->ClientSocket,header, START_IDER_REDIRECTION_LENGTH,ILibAsyncSocket_MemoryOwnership_USER);
              sprintf(s, "IDER started for %s", rs->AmtUsername);
              rs->LogThis(s);
            }
          }
          *p_beginPointer += messageLength;      
        }
        break;
      }  
      //Forward it on the ServerSocket to Console
    case START_SOL_REDIRECTION_REPLY:
      {
        if (endPointer >= START_SOL_REDIRECTION_REPLY_LENGTH)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], START_SOL_REDIRECTION_REPLY_LENGTH, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, START_SOL_REDIRECTION_REPLY_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
          }
          *p_beginPointer += START_SOL_REDIRECTION_REPLY_LENGTH;      
        }
        break;
      }
    case START_IDER_REDIRECTION_REPLY:
      {
        if (endPointer >= START_IDER_REDIRECTION_REPLY_LENGTH)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], START_IDER_REDIRECTION_REPLY_LENGTH, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, START_IDER_REDIRECTION_REPLY_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
          }
          *p_beginPointer += START_IDER_REDIRECTION_REPLY_LENGTH;      
        }
        break;
      }
    case SOL_HEARTBEAT:
    case IDER_HEARTBEAT:
    case SOL_KEEP_ALIVE_PONG:
    case IDER_KEEP_ALIVE_PONG:
      {
        if (endPointer >= HEARTBEAT_LENGTH)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], HEARTBEAT_LENGTH, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, HEARTBEAT_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
            rs->ReceivedBytes += HEARTBEAT_LENGTH;
          }
          *p_beginPointer += HEARTBEAT_LENGTH;      
        }
        break;
      }
    case SOL_DATA_FROM_HOST:
      {
        int messageLength = 0;
        unsigned short dataLength = 0;

        if (endPointer < 10)
          break;

        memcpy(&dataLength, buffer + 8, 2);
        messageLength = 10 + dataLength;

        if (endPointer >= messageLength)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], messageLength, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, messageLength, ILibAsyncSocket_MemoryOwnership_USER);
            rs->ReceivedBytes += messageLength;
          }
          *p_beginPointer += messageLength;      
        }
        break;
      }
    case IDER_RESET_OCCURED:
      {
        if (endPointer >= IDER_RESET_OCCURED_LENGTH)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], IDER_RESET_OCCURED_LENGTH, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, IDER_RESET_OCCURED_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
            rs->ReceivedBytes += IDER_RESET_OCCURED_LENGTH;
          }
          *p_beginPointer += IDER_RESET_OCCURED_LENGTH;      
        }    
        break;
      }
    case IDER_DISABLE_ENABLE_FEATURES_REPLY:
      {
        if (endPointer >= IDER_DISABLE_ENABLE_FEATURES_REPLY_LENGTH)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], IDER_DISABLE_ENABLE_FEATURES_REPLY_LENGTH, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, IDER_DISABLE_ENABLE_FEATURES_REPLY_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
            rs->ReceivedBytes += IDER_DISABLE_ENABLE_FEATURES_REPLY_LENGTH;
          }
          *p_beginPointer += IDER_DISABLE_ENABLE_FEATURES_REPLY_LENGTH;      
        }    
        break;
      }
    case IDER_COMMAND_WRITTEN:
      {
        if (endPointer >= 28)
        {
          int messageLength = 28;

          //TODO: check if the last 4 bytes are the start of a new message type
          if (endPointer == 32)
            messageLength = 32;

          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], messageLength, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, messageLength, ILibAsyncSocket_MemoryOwnership_USER);
            rs->ReceivedBytes += messageLength;
          }
          *p_beginPointer += messageLength;      
        }    
        break;
      }
    case IDER_DATA_FROM_HOST:
      {
        int messageLength = 0;
        unsigned short dataLength = 0;

        if (endPointer < 11)
          break;

        memcpy(&dataLength, buffer + 9, 2);
        messageLength = 14 + dataLength;

        if (endPointer >= messageLength)
        {
          if ((rs != NULL) && (rs->ServerSocket != NULL))
          {
            SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], messageLength, __LINE__, __FUNCTION__));
            ILibAsyncSocket_Send(rs->ServerSocket, buffer, messageLength, ILibAsyncSocket_MemoryOwnership_USER);
            rs->ReceivedBytes += messageLength;
          }
          *p_beginPointer += messageLength;      
        }
        break;
      }
    case END_SOL_REDIRECTION_REPLY:
    case END_IDER_REDIRECTION_REPLY:
      {
        if (endPointer >= END_SOL_REDIRECTION_REPLY_LENGTH)
        {
          if (rs != NULL)
          {
            if(rs->ServerSocket != NULL)
            {
              SOMEMORE_DEBUGSTATEMENT(printf("F %02x of length %d on %d in %s\n", buffer[0], END_SOL_REDIRECTION_REPLY_LENGTH, __LINE__, __FUNCTION__));
              ILibAsyncSocket_Send(rs->ServerSocket, buffer, END_SOL_REDIRECTION_REPLY_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
            }
            else 
            {
              header[0] = END_REDIRECTION_SESSION;
              header[1] = 0;
              header[2] = 0;
              header[3] = 0;

              ILibAsyncSocket_Send(rs->ClientSocket, header, END_REDIRECTION_SESSION_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
            }
          }

          *p_beginPointer += END_SOL_REDIRECTION_REPLY_LENGTH;      
        }
        break;
      }
    default:
      {
        DEBUGSTATEMENT_R(printf("This is an error. Please report it. Reached Default on %d in %s\n", __LINE__, __FUNCTION__));
        ILibAsyncSocket_Disconnect(rs->ClientSocket);
        break;
      }  
    }
  }
}

void RedirectionClient_OnSendOK(ILibAsyncSocket_SocketModule socketModule, void *user)
{
  SOMEMORE_DEBUGSTATEMENT(printf("%d in %s\n", __LINE__, __FUNCTION__));
}
void RedirectionClient_OnInterrupt(ILibAsyncSocket_SocketModule socketModule, void *user)
{
  SOMEMORE_DEBUGSTATEMENT(printf("%d in %s\n", __LINE__, __FUNCTION__));
}

Redirection_ServerToken Redirection_Create(void *Chain, int MaxConnections, int PortNumber,Redirection_Session_OnSession OnSession, void *User)
{
  rsm = (struct Redirection_StateModule*)malloc(sizeof(struct Redirection_StateModule));

  memset(rsm,0,sizeof(struct Redirection_StateModule));

  rsm->Destroy = NULL;
  rsm->Chain = Chain;
  rsm->OnSession = OnSession;
  rsm->Port = PortNumber;
  rsm->EnableFlag = 1;

  //
  // Create the underling ILibAsyncServerSocket
  //
  rsm->ServerSocket = ILibCreateAsyncServerSocketModule(
    Chain,
    MaxConnections,
    PortNumber,
    REDIRECT_BUFFER_SIZE,
    &Redirection_OnConnect,			  // OnConnect
    &Redirection_OnDisconnect,		// OnDisconnect
    &Redirection_OnReceive,			  // OnReceive
    &Redirection_OnInterrupt,			// OnInterrupt
    &Redirection_OnSendOK         // OnSendOK
    );

  //
  // Set ourselves in the User tag of the underlying ILibAsyncServerSocket
  //
  //  ILibAsyncServerSocket_SetTag(rsm->ServerSocket,rsm);
  rsm->User = User;
  ILibAddToChain(Chain,rsm);

  return(rsm);
}

int Redirection_Toggle()
{
  rsm->EnableFlag = -1 * rsm->EnableFlag;
  return rsm->EnableFlag ;
}


void Redirection_Stop(struct Redirection_Session *rs)
{
  char header[SMALL_TOKEN_SIZE];

  header[0] = END_REDIRECTION_SESSION;
  header[1] = 0;
  header[2] = 0;
  header[3] = 0;

  ILibAsyncSocket_Send(rs->ClientSocket, header, END_REDIRECTION_SESSION_LENGTH, ILibAsyncSocket_MemoryOwnership_USER);
  ILibAsyncSocket_Disconnect(rs->ServerSocket);
}

void GetRedirectionsInXML(void *redirections, char **buffer)
{
  struct Redirection_Session *rs = NULL;
  struct HashNodeEnumerator *en = NULL;
  char *key = NULL;
  int keyLength = 0;
  struct in_addr temp;
  char myline[MAX_LINE_SIZE];

  *buffer = (char *)malloc(MAX_BUFFER_SIZE);
  sprintf(*buffer,"<?xml version=\"1.0\" encoding=\"utf-8\"?><redirections>");
  en = ILibHashTree_GetEnumerator(redirections);

  while(ILibHashTree_MoveNext(en)==0)
  {
    ILibHashTree_GetValue(en,&key,&keyLength,(void**)&rs);
    if (rs != NULL)
    {
      temp.s_addr = rs->SourceIP;
      sprintf(myline, "<redirect><sessiontype>%s</sessiontype><category>%s</category><sourceip>%s</sourceip><user>%s</user><targetid>%s</targetid><targetip>%s</targetip><sentbytes>%lu</sentbytes><receivedbytes>%lu</receivedbytes></redirect>", rs->SessionTypeString, rs->SessionCategoryString, inet_ntoa(temp), rs->SourceUsername, rs->AmtID, rs->AmtIP, rs->SentBytes, rs->ReceivedBytes);
      strcat(*buffer, myline);
    }
  }
  ILibHashTree_DestroyEnumerator(en);

  strcat(*buffer,"</redirections>");
}

void Redirection_SetTLS(void *ConnectionToken, char *KeyFile, char *CertFile)
{
	struct Redirection_StateModule *rsm = (struct Redirection_StateModule*)ConnectionToken;
  ILibAsyncServerSocket_SetSSL_CTX(rsm->ServerSocket, KeyFile, CertFile);
}
