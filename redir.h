#include "RedirectionConstants.h"

enum redir_state {
    REDIR_NONE      =  0,
    REDIR_CONNECT   =  1,
    REDIR_INIT      =  2,
    REDIR_AUTH      =  3,
    REDIR_INIT_SOL  = 10,
    REDIR_RUN_SOL   = 11,
    REDIR_INIT_IDER = 20,
    REDIR_RUN_IDER  = 21,
    REDIR_CLOSING   = 30,
    REDIR_CLOSED    = 31,
    REDIR_ERROR     = 40,
};

struct redir {
    int               sock;
    int               verbose;
    unsigned char     host[64];
    unsigned char     port[16];
    unsigned char     type[4];
    unsigned char     user[16];
    unsigned char     pass[16];

    unsigned char     buf[64];
    unsigned int      blen;
    enum redir_state  state;

    /* callbacks */
    void *cb_data;
    void (*cb_state)(void *cb_data, enum redir_state old, enum redir_state new);
    int (*cb_recv)(void *cb_data, unsigned char *buf, int len);
};

const char *redir_state_name(enum redir_state state);
const char *redir_state_desc(enum redir_state state);

int redir_connect(struct redir *r);
int redir_start(struct redir *r);
int redir_stop(struct redir *r);
int redir_auth(struct redir *r);
int redir_sol_start(struct redir *r);
int redir_sol_stop(struct redir *r);
int redir_sol_send(struct redir *r, unsigned char *buf, int blen);
int redir_sol_recv(struct redir *r);
int redir_data(struct redir *r);
