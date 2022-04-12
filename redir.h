#include "RedirectionConstants.h"
#include <stdint.h>

enum redir_state {
    REDIR_NONE      =  0,
    REDIR_CONNECT   =  1,
    REDIR_INIT      =  2,
    REDIR_AUTH      =  3,
    REDIR_INIT_SOL  = 10,
    REDIR_RUN_SOL   = 11,
    REDIR_INIT_IDER = 20,
    REDIR_CFG_IDER  = 21,
    REDIR_RUN_IDER  = 22,
    REDIR_CLOSING   = 30,
    REDIR_CLOSED    = 31,
    REDIR_ERROR     = 40,
};

struct redir {
    /* host connection */
    unsigned char     host[64];
    unsigned char     port[16];
    unsigned char     user[64];
    unsigned char     pass[64];

    /* serial-over-lan */
    unsigned char     type[4];
    int               verbose;
    int               trace;
    int               legacy;
    enum redir_state  state;
    unsigned char     err[128]; // state == REDIR_ERROR

    /* ide-redirection */
    unsigned char     filename[256];
    unsigned int      tx_bufsize;
    unsigned int      rx_bufsize;
    unsigned int      enable_options;
    unsigned int      tx_length;
    unsigned int      tx_offset;

    int               sock;
    unsigned char     buf[64];
    unsigned int      blen;
    unsigned int      seqno;

    void              *cacert;
    void              *ctx;

    /* callbacks */
    void *cb_data;
    void (*cb_state)(void *cb_data, enum redir_state old, enum redir_state new);
    int (*cb_recv)(void *cb_data, unsigned char *buf, int len);
};

struct __attribute__ ((__packed__)) controls_from_host_message {
    unsigned char type; // 0x29
    unsigned char reserved[3];
    uint32_t      host_sequence_number;
    unsigned char control; 
    unsigned char status;
};

struct __attribute__ ((__packed__)) ider_command_written_message {
    unsigned char type;
    unsigned char reserved[3];
    uint32_t      sequence_number;
    unsigned char cable_sel;          /* 8 */
    unsigned char feature;            /* 9 */
    unsigned char sector_count;       /* 10 */
    unsigned char sector_number;      /* 11 */
    uint16_t      byte_count;         /* 12 */
    unsigned char drive_select;       /* 14 */
    unsigned char command;            /* 15 */
    unsigned char packet_data[12];    /* 16 */
};

struct __attribute__ ((__packed__)) ider_data_regs {
    unsigned char mask;
    unsigned char error;
    unsigned char sector_count;
    unsigned char sector_num;
    unsigned char byte_count_lsb;
    unsigned char byte_count_msb;
    unsigned char drive_select;
    unsigned char status;
};

struct __attribute__ ((__packed__)) ider_command_response_message {
    unsigned char type;
    unsigned char reserved[2];
    unsigned char attributes;
    uint32_t      sequence_number;
    unsigned char cable_sel;         /* unused */
    uint16_t      transfer_bytes;
    unsigned char packet_num;        /* unused */
    unsigned char input_regs[8];     /* unused */
    struct ider_data_regs output;
    unsigned char sense;
    unsigned char asc;
    unsigned char asq;
};

struct __attribute__ ((__packed__)) ider_data_to_host_message {
    unsigned char type;
    unsigned char reserved[2];
    unsigned char attributes;
    uint32_t      sequence_number;
    unsigned char cable_sel;         /* unused */
    uint16_t      transfer_bytes;
    unsigned char packet_num;        /* unused */
    struct ider_data_regs input;
    struct ider_data_regs output;
    unsigned char sense;
    unsigned char asc;
    unsigned char asq;
    unsigned char rsvd2[3];
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
int redir_ider_start(struct redir *r);
int redir_ider_config(struct redir *r);
int redir_ider_reset(struct redir *r, unsigned int seqno);
int redir_ider_stop(struct redir *r);
int redir_ider_send(struct redir *r, unsigned char *buf, int blen);
int redir_ider_recv(struct redir *r);
int redir_data(struct redir *r);
ssize_t redir_write(struct redir *r, const char *buf, size_t count);

int ider_handle_command(struct redir *r, unsigned int seqno,
			unsigned char *cdb);
