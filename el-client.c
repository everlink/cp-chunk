
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <netdb.h>
#include <getopt.h>

#include "cpapp_helper.h"

#define PORT            9930
#define DEF_DEVICEID    "132K73MY88888888"
#define DEF_BOSS_HOST   "campal-api.everlink.net"
#define DEF_BOSS_PORT   80

#define IDLEN           16
#define HN_BUFFSIZE     127

#define RECV_BUFSIZE    0xffff


char id[IDLEN + 1];
char hostname[HN_BUFFSIZE + 1];
struct buffer_pool* p_bp;

void peer_data_process(int s, struct sockaddr *addr, struct buffer_pool *bp, char *data, size_t datalen);
void send_ack(int s, struct sockaddr *addr, struct buffer_pool *bp, unsigned short ack);


int main(int argc, char* argv[])
{
  struct sockaddr_in si_other;
  unsigned int server_host;
  short server_port;
  socklen_t slen=sizeof(si_other);
  char *buf;
  int s;

  int option;
  int p_code = 0;
  struct payload payload;
  struct hostinfo i;
  int ret = -1;
  int s_port;

  buf = malloc(RECV_BUFSIZE);
  init_buff_pool(&p_bp);

  strncpy(hostname, DEF_BOSS_HOST, HN_BUFFSIZE);
  s_port = DEF_BOSS_PORT;

  while((option = getopt(argc, argv, "s:c:p:")) != -1) {
    switch(option) {
      case 's':
        strncpy(hostname, optarg, HN_BUFFSIZE);
        break;
      case 'p':
        s_port = atoi(optarg);
        break;
      case 'c':
        /* at least should have an ID as parameter */
        strncpy(id, optarg, IDLEN);
        p_code = 1;
        break;
    }
  }

  if(!p_code) {
    printf("Wrong parameters. Usage %s -c <PAIR_CODE> , use default\n", argv[0]);
    strncpy(id, DEF_DEVICEID, IDLEN);
  }

  // going to enquire boss server (with host/port) for hosts' information
  // the CONSTANT EL_SENDER_ROLE_ISLAND indicates the remote side (ex. camera)
  ret = info_by_id(hostname, s_port, id, EL_SENDER_ROLE_ISLAND, &i);
  if (ret != 0) {
    printf("get info error %d\n", ret);
    exit(2);
  }

  printf("BOSS RESP: %s %d %s %d %s %d %s %d\n", i.sip, i.spo, i.cip, i.cpo, i.nip, i.npo, i.hip, i.hpo);

  // Prepare si_other as server's endpoint
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  inet_pton(AF_INET, i.sip, &si_other.sin_addr);
  si_other.sin_port = htons(i.spo);

  // save the server host/port for future use
  server_host = si_other.sin_addr.s_addr;
  server_port = si_other.sin_port;

  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
    perror("socket");
    exit(1);
  }

  // packing a payload structure
  // PAYLOAD
  // c.id   ==> my user ID, so that server can loolup database and indentify
  // c.host ==> my local address, so that peer side can try to access me by this local address
  //            when remote address fails (in same local network)
  // c.port ==> my local port for local accessing (should bind to this port. TODO)
  // c.role ==> tell server this is a remote side or local site, use EL_SENDER_ROLE_ISLAND for remote site/camera/wifi-switch
  strncpy(payload.p.c.id, id, 16);
  payload.p.c.host = self_ip();
  payload.p.c.port = PORT;
  payload.sender_role = EL_SENDER_ROLE_ISLAND;
  

  // send a datagram to stun server with payload
  if (sendto(s, &payload, sizeof(payload), 0, (struct sockaddr*)(&si_other), slen)==-1) {
    perror("sendto");
    exit(1);
  }

  // listening
  while (1) {
    ssize_t datalen = 0;
    memset(buf, 0, RECV_BUFSIZE);
    if ((datalen = recvfrom(s, buf, RECV_BUFSIZE, 0, (struct sockaddr*)(&si_other), (socklen_t*)&slen)) == -1) {
      perror("recvfrom");
      exit(1);
    }

    printf("[RECV] %d bytes\n", (int)datalen);

    // if datagram from stun server
    // decode the payload from stun sever, and fork a process for sending HEARTBEAT every 30 seconds
    if (server_host == si_other.sin_addr.s_addr && server_port == (short)(si_other.sin_port)) {
      memcpy(&payload, buf, sizeof(payload));
      printf("Decoded from BOSS role:0x%x, signal:0x%x\n", payload.sender_role, payload.signal);

      switch(fork()) {
        case -1:
          perror("fork()");
          exit(EXIT_FAILURE);
        case 0:
          // child process, keep sending headbeat
          while (1) {
            sleep (10);
            if (sendto(s, "HEARTBEAT", 10, 0, (struct sockaddr*)(&payload.p.s.addr), slen)==-1) {
              printf("calling peer.. FAILED\n");
            }
            sleep (20);
          }
          break;
        default:
          break;
      }
    }
    else {
      peer_data_process(s, (struct sockaddr*)(&payload.p.s.addr), p_bp, buf, datalen);
    }
  }

  // Actually, we never reach this point...
  close(s);
  return 0;
}


void peer_data_process(int s, struct sockaddr *addr, struct buffer_pool *bp, char *data, size_t datalen)
{
  int i = process_input(bp, data, datalen);
  unsigned short seq;
 
  switch (i) {
    default:
      printf("loop, i = %d\n", i); while (1);
      break;
    case PROC_NORMAL:
      break;
    case PROC_DROP:
      break;
    case PROC_EOF:
    case PROC_INRETRY:
      seq = finalize_buff_pool(bp);
      send_ack(s, addr, bp, seq);
      if (seq == CPAPP_EOF) {
        clear_buff_pool(p_bp);
      }
      break;  
    case PROC_STARTIOF:
      send_ack(s, addr, bp, CPAPP_IOF);
      break;  
  }
}


int process_input(struct buffer_pool  *bp, void *buff, size_t datalen)
{
  struct p2p_payload_hdr *p;
  unsigned short seq;
  
  if (datalen < sizeof(struct p2p_payload_hdr)) {
    printf("DROP - ignoring package(size=%d)\n", (int)datalen);
    // not my package
    return PROC_DROP;
  }

  if (check_sum(buff, datalen)) {
    printf("DROP - WRONG DATA! NEED RESEND!");
    return PROC_DROP;
  }

  p = buff;
  seq = p->seq;

  printf("processing chunk %04x, %04x => %04x\n", p->seq, p->fseq, bp->f.fseq);

  if (bp->f.fseq == 0 && seq != CPAPP_IOF) {
    printf("DROP - NO FILE INFO YET!");
    return PROC_DROP;
  }

  if (CPAPP_EOF == seq) {
    return PROC_EOF;
  }
  else if (CPAPP_IOF == seq) {
    memcpy(&bp->f, (unsigned char*)buff + sizeof(struct p2p_payload_hdr), p->psize);
    return PROC_STARTIOF;
  }
  else {
    bp->node[p->seq].datalen = datalen - sizeof(struct p2p_payload_hdr);
    memcpy(bp->node[p->seq].buff, (unsigned char*)buff + sizeof(struct p2p_payload_hdr), datalen - sizeof(struct p2p_payload_hdr));
  }
  if (bp->f.in_retry) {
    return PROC_INRETRY;
  }
  return PROC_NORMAL;
}


int finalize_buff_pool(struct buffer_pool *bp)
{
  FILE  *recv_file = NULL;
  char fn[128];
  unsigned int i;

  for (i = 0; i < (bp->f.filesize + CPAPP_MAX_CHUNKSIZE - 1)/CPAPP_MAX_CHUNKSIZE; i++) {
    if (bp->node[i].datalen == 0) {
      printf("finalize missing chunk %d / %d\n", i, (int)bp->f.filesize);
      bp->f.in_retry = 1;
      return i;
    }
  }

  snprintf(fn, 128, "/tmp/__%s", bp->f.filename);
  printf("WRITES: %s\n", fn);
  recv_file = fopen(fn, "w");

  for (i = 0; i < (bp->f.filesize + CPAPP_MAX_CHUNKSIZE - 1)/CPAPP_MAX_CHUNKSIZE; i++) {
    fwrite(bp->node[i].buff, bp->node[i].datalen, 1, recv_file);
  }
  fclose(recv_file);
  return CPAPP_EOF;
}

void send_ack(int s, struct sockaddr *addr, struct buffer_pool *bp, unsigned short seq)
{
  struct ack_pack ack;
  ack.seq = seq;
  ack.fseq = bp->f.fseq;
  ack.flag = 0xacac;
  printf("Client ACK:  fseq seq = %04x, %04x\n", ack.fseq, ack.seq);
  if (sendto(s, &ack, sizeof(struct ack_pack), 0, addr, sizeof(struct sockaddr_in))==-1) {
    printf("SEND ACK FAILED\n");
  }
}

