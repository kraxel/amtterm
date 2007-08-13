#include "RedirectionConstants.h"

enum redir_state {
    REDIR_NONE      =  0,
    REDIR_INIT      =  1,
    REDIR_AUTH      =  2,
    REDIR_INIT_SOL  = 10,
    REDIR_CONN_SOL  = 11,
    REDIR_INIT_IDER = 20,
    REDIR_CONN_IDER = 21,
    REDIR_CLOSING   = 30,
    REDIR_CLOSED    = 31,
    REDIR_ERROR     = 99,
};

struct redir {
    int               sock;
    int               verbose;
    unsigned char     type[4];
    unsigned char     user[16];
    unsigned char     pass[16];
    enum redir_state  state;

    /* callbacks */
    void *cb_data;
    void (*cb_state)(void *cb_data, enum redir_state old, enum redir_state new);
    int (*cb_recv)(void *cb_data, unsigned char *buf, int len);
};

const char *redir_strstate(enum redir_state state);

int redir_start(struct redir *r);
int redir_stop(struct redir *r);
int redir_auth(struct redir *r);
int redir_sol_start(struct redir *r);
int redir_sol_stop(struct redir *r);
int redir_sol_send(struct redir *r, unsigned char *buf, int blen);
int redir_sol_recv(struct redir *r, unsigned char *buf, int blen);
int redir_data(struct redir *r);
