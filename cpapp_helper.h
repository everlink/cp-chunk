#ifndef __CPAPP_HELPER_H
#define __CPAPP_HELPER_H

#include <stdio.h>

#include "cpapp_config.h"

#define IN
#define OUT

#define CPAPP_EOF           0xFFFF
#define CPAPP_IOF           0xFFFE
#define CPAPP_IGN           0xFFFC
#define CPAPP_TMO           0xFFFB


#define EL_SIGNAL_KNOCK         0x01
#define EL_SENDER_ROLE_ISLAND   0x04
#define EL_SENDER_ROLE_TOWER    0x08
#define EL_SENDER_ROLE_SERVER   0x10

#define IP_BUFFSIZE     63
#define HTTP_BUFFSIZE   1023

#define PROC_EOF      0
#define PROC_NORMAL   1
#define PROC_STARTIOF 2
#define PROC_INRETRY  3
#define PROC_DROP     4


struct payload {
  char payload_version;
  char signal;
  char sender_role;
  char reserved;
  union {
    struct {
      char id[16];
      int host;
      short port;
    } c;
    struct {
      struct sockaddr_in addr;
    } s;
  } p;
};

struct p2p_payload_hdr {
  unsigned short sum;
  unsigned short seq;
  unsigned short psize;
  unsigned short fseq;
};

struct ack_pack {
  unsigned short sum;     // not use yet checksum
  unsigned short flag;    // not use yet 0xac
  unsigned short seq;
  unsigned short fseq;
};

struct hostinfo {
  int spo;                          // port of stun server
  int cpo;                          // port of client/internal, copy from payload
  int npo;                          // port of nat/external
  int hpo;                          // port of heartbeat server 
  char sip[IP_BUFFSIZE];            // ip of stun server
  char cip[IP_BUFFSIZE];            // ip of client/internal, copy from payload
  char nip[IP_BUFFSIZE];            // ip of nat/external
  char hip[IP_BUFFSIZE];            // ip of heartbeat server
};

struct buffer_node {
  unsigned short datalen;
  void *buff;
};

struct fileinfo {
  char    filename[LEN_FILENAME + 1];
  size_t  filesize;
  unsigned short fseq;
  char    in_retry;
} f;

struct buffer_pool {
  struct fileinfo f;
  struct buffer_node node[CPAPP_BUFF_NUM];
};

int     info_by_id(IN const char *hostname, IN int s_port, IN const char *id, IN int role, OUT struct hostinfo *i);
int     self_ip(void);
unsigned short calcsum(void *data, int len);
void    add_sum(void *buf, ssize_t len);
int     check_sum(void *buf, ssize_t len);
void    init_buff_pool(struct buffer_pool  **bp);              // param : pointer to the pointer of buffer pool 
void    clear_buff_pool(struct buffer_pool *bp);               // TODO
void    free_buff_pool(struct buffer_pool *bp);                // TODO
int     finalize_buff_pool(struct buffer_pool  *bp);            // write to file
int     load_buff_pool(struct buffer_pool  *bp, const char *fn);            // load from file
//int     find_first_failed_unit(struct buffer_pool  *bp);        // find which needs resend. -1 : no more failed unit;      0 ~ N-1 : the first unit wait retrieving
int     process_input(struct buffer_pool  *bp, void *buf, size_t datalen);        // copy the data to buffer pool;      return 0 one EOF ==> to try finalizing
size_t  get_filesize(const char *fn);

#endif
