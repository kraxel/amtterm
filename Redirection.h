#ifndef __REDIRECTION__
#define __REDIRECTION__

typedef void* Redirection_ServerToken;

struct Redirection_Session;

typedef void* (*Redirection_Session_AuthenticateUserOnly)(char *username, char *password);
typedef int (*Redirection_Session_GetSystemDetailsOnly)(struct Redirection_Session *rs);
typedef int (*Redirection_Session_AuthenticateUserAndGetSystemDetails)(struct Redirection_Session *rs, char *password);
typedef void (*Redirection_Session_OnDisconnect)(struct Redirection_Session *rs);
typedef void (*Redirect_Session_LogThis)(char *s);

/*! \struct Redirection_Session
\brief A structure representing the state of an REDIRECT Session
*/
struct Redirection_Session
{
  Redirection_Session_AuthenticateUserOnly AuthenticateUserOnly;
  Redirection_Session_GetSystemDetailsOnly GetSystemDetailsOnly;
  Redirection_Session_AuthenticateUserAndGetSystemDetails AuthenticateUserAndGetSystemDetails;
  Redirection_Session_OnDisconnect OnDisconnect;
  Redirect_Session_LogThis LogThis;

  int SourceIP;
  char SourceUsername[32];
  char SourcePassword[32];
  char AmtIP[32];
  char AmtID[32];
  char AmtUsername[32];
  char AmtPassword[32];
  int AmtPort;

  char *KeyFile;
  char *CertFile;
  char *CACertFile;

  unsigned long ClientID;

  void *ServerSocket;
  void *ClientSocket;

  /*! \var User
  \brief A reserved pointer that you can use for your own use
  */
  void *User;
  void *mappings;

  void *ssl_ctx;

  int state;
  unsigned int consoleSequenceNo;

  unsigned int SessionType;
  char SessionTypeString[128];
  char SessionCategoryString[128];
  unsigned long SentBytes;
  unsigned long ReceivedBytes;

  char solCommand[128];
  int solCommandLength;
};


/*! \typedef Redirection_Session_OnSession
\brief New Session Handler
\param SessionToken The new Session
\param User The \a User object specified in \a Redirection_Create
*/
typedef void (*Redirection_Session_OnSession)(struct Redirection_Session *SessionToken, void *User);

Redirection_ServerToken Redirection_Create(void *Chain, int MaxConnections, int PortNumber,Redirection_Session_OnSession OnSession, void *User);
Redirection_ServerToken Telnet_Redirection_Create(void *Chain, int MaxConnections, int PortNumber,Redirection_Session_OnSession OnSession, void *User);

int Redirection_Toggle();
int TelnetRedirection_Toggle();

void Redirection_Stop(struct Redirection_Session *rs);
void TelnetRedirection_Stop(struct Redirection_Session *rs);

void GetRedirectionsInXML(void *redirections, char **buffer);
void Redirection_SetTLS(void *ConnectionToken, char *KeyFile, char *CertFile);

#endif
