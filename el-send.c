#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include <sys/select.h>
#include <fcntl.h>

#include "cpapp_helper.h"

#define DEF_BOSS_HOST   "campal-api.everlink.net"
#define DEF_BOSS_PORT   80
//#define DEF_BOSS_HOST   "localhost"
//#define DEF_BOSS_PORT   8800
#define DEF_DEF_KEYCODE "132K73MY88888888"


#define SEND_BUFFSIZE   0xffff
#define IDLEN           16
#define HN_BUFFSIZE     127


char id[IDLEN + 1];
char hostname[HN_BUFFSIZE + 1];
int sock_fd;
struct sockaddr_in peer_addr;
void main_loop(void);

struct buffer_pool *p_bp;

int main(int argc, char* argv[])
{
  int option;
  int p_code = 0;
  struct sockaddr_in stun_addr;
  struct payload payload;
  int s_port = DEF_BOSS_PORT;
  struct hostinfo i;
  int ret = -1;

  strncpy(hostname, DEF_BOSS_HOST, HN_BUFFSIZE);

  init_buff_pool(&p_bp);

  while((option = getopt(argc, argv, "s:c:p:")) != -1) {
    switch(option) {
      case 's':
        strncpy(hostname, optarg, HN_BUFFSIZE);
        break;
      case 'p':
        s_port = atoi(optarg);
        break;
      case 'c':
        strncpy(id, optarg, IDLEN);
        p_code = 1;
        break;
    }
  }

  if(!p_code) {
    printf("Wrong parameters. Usage %s -c <PAIR_CODE>\n now use test key \n", argv[0]);
    strncpy(id, DEF_DEF_KEYCODE, IDLEN);
    p_code = 1;
  }

  ret = info_by_id(hostname, s_port, id, EL_SENDER_ROLE_TOWER, &i);
  if (ret != 0) {
    printf("get info error %d\n", ret);
    exit(2);
  }

  printf("scan.. %s %d %s %d %s %d %s %d\n", i.sip, i.spo, i.cip, i.cpo, i.nip, i.npo, i.hip, i.hpo);
  if (0 == inet_pton(AF_INET, i.nip, &peer_addr.sin_addr)) {
    perror("error");
  }
  peer_addr.sin_port = htons(i.npo);
  peer_addr.sin_family = AF_INET;
  printf("TARGET %s:%d\n", inet_ntoa(peer_addr.sin_addr), peer_addr.sin_port);

  if((sock_fd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("socket()");
    exit(2);
  }

  /* hi server, please get the island and ask knock me */
  payload.p.s.addr = peer_addr;
  payload.sender_role = EL_SENDER_ROLE_TOWER;

  if (0 == inet_pton(AF_INET, i.sip, &stun_addr.sin_addr))
  {
    perror("error");
  }
  stun_addr.sin_family = AF_INET;
  stun_addr.sin_port = htons(i.spo);
  if(sendto(sock_fd, (void*)&payload, sizeof(payload), 0, (struct sockaddr*)&stun_addr, sizeof(struct sockaddr)) < 0) {
    perror("sendto()");
  }

  /* I would like to know the island first, so that I can get from island 
   * this is need, or will not accept the HI headbeat ACK
   * TODO need some delay before knock or retry knocks ?? */
  if(sendto(sock_fd, "knock", 6, 0, (struct sockaddr*)&peer_addr, sizeof(struct sockaddr)) < 0) {
    perror("sendto()");
  }

  /* we are going to demo send files */
  main_loop();

  return (EXIT_SUCCESS);
}

static ssize_t dr_sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, int tolen)
{

#if 0
  static int drop_count = 0;
  drop_count++;

  if (drop_count % 7 == 0) {
    printf("drop\n");
    return 0; //-1;
  }
#endif
  return sendto(s,msg,len,flags,to,tolen);
}

void push_fileinfo(struct buffer_pool *bp)
{
  char* buff = malloc(SEND_BUFFSIZE);
  size_t header_size = sizeof(struct p2p_payload_hdr);
  struct p2p_payload_hdr *hdr = (void*)buff;
  hdr->seq = CPAPP_IOF;
  hdr->psize = sizeof(struct fileinfo); //sizeof(bp->f);
  printf("puts %d bytes of fileinfo\n", hdr->psize);
  hdr->fseq = bp->f.fseq;
  memcpy(&buff[header_size], &bp->f, hdr->psize);
  add_sum(buff, hdr->psize + header_size);
  if(dr_sendto(sock_fd, buff, hdr->psize + header_size, 0, (struct sockaddr*)&peer_addr, sizeof(struct sockaddr)) < 0) {
    perror("sendto()");
  }
  free(buff);
}

void push_chunk(struct buffer_pool *bp, unsigned short i)
{
  char* buff = malloc(SEND_BUFFSIZE);
  size_t header_size = sizeof(struct p2p_payload_hdr);
  struct p2p_payload_hdr *hdr = (void*)buff;
  hdr->seq = i;
  hdr->psize = bp->node[i].datalen;
  printf("puts %d 0x%04x, %d, bytes of chunk\n", bp->f.fseq, i, hdr->psize);
  hdr->fseq = bp->f.fseq;
  memcpy(&buff[header_size], bp->node[i].buff, hdr->psize);
  add_sum(buff, hdr->psize + header_size);
  if(dr_sendto(sock_fd, buff, hdr->psize + header_size, 0, (struct sockaddr*)&peer_addr, sizeof(struct sockaddr)) < 0) {
    perror("sendto()");
  }
  free(buff);
}

void push_eof(struct buffer_pool *bp)
{
  char* buff = malloc(SEND_BUFFSIZE);
  size_t header_size = sizeof(struct p2p_payload_hdr);
  struct p2p_payload_hdr *hdr = (void*)buff;
  hdr->seq = CPAPP_EOF;
  hdr->psize = 0;
  hdr->fseq = bp->f.fseq;
  printf("puts %d eof\n", hdr->psize);
  add_sum(buff, header_size);
  if(dr_sendto(sock_fd, buff, header_size, 0, (struct sockaddr*)&peer_addr, sizeof(struct sockaddr)) < 0) {
    perror("sendto()");
  }
  free(buff);
}

unsigned short wait_ack(int sec, unsigned short *fseq)
{
  struct ack_pack ack;
  ssize_t datalen = 0;
  struct sockaddr_in si_other;
  socklen_t slen = sizeof(si_other);

  datalen = recvfrom(sock_fd, &ack, sizeof(struct ack_pack), MSG_DONTWAIT, (struct sockaddr*)(&si_other), (socklen_t*)&slen);
  if (datalen == -1) {    // NO DATE. perror("recvfrom");
    if (sec) sleep(sec);
    *fseq = CPAPP_TMO;
    return CPAPP_TMO;
  } else if (datalen == sizeof(struct ack_pack)) {
    if (ack.flag == 0xacac) {
      *fseq = ack.fseq;
      return ack.seq;
    }
  }
  *fseq = CPAPP_IGN;
  return CPAPP_IGN;       // Other data
}

void main_loop(void)
{
  const char *s[] = {
    "001.jpg",
    "002.jpg",
    "003.jpg",
    "004.jpg",
    "005.jpg",
    "006.jpg",
    "007.jpg",
    "008.jpg",
    "009.jpg",
    "010.jpg",
    "011.jpg",
    "012.jpg",
    0
  };
  char **p = (char**)s;
  unsigned short fseq = 1;

  while (*p) {
    unsigned short seq;
    unsigned short client_fseq;
    
    load_buff_pool(p_bp, *p);
    p_bp->f.fseq = fseq;

    /* send filename, send chunks, send EOF */

    do {
      push_fileinfo(p_bp);
    } while (CPAPP_IOF != wait_ack(0, &client_fseq));

    for (unsigned int i = 0; i <= p_bp->f.filesize / CPAPP_MAX_CHUNKSIZE; i++) {
      push_chunk(p_bp, i);
    }
    push_eof(p_bp);

    while (((seq = wait_ack(0, &client_fseq)) != CPAPP_EOF) || (client_fseq != fseq)) {
      printf("ACK LOOP:  ack, cliseq, seq  = %04x, %04x, %04x\n", seq, client_fseq, fseq);
      switch (seq) {
        case CPAPP_TMO:
          push_eof(p_bp);
          break;
        case CPAPP_IGN:
          break;
        case CPAPP_EOF:
          break;
        case CPAPP_IOF:
          break;
        default:
          push_chunk(p_bp, seq);
          break;
      }
    }
    printf("Step to next file. (%d finished)\n", client_fseq);
    p++;
    fseq++;
  }
}

