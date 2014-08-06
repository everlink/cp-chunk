#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cpapp_helper.h"

/* this function is to enumerate the local IP
 * with passing the local IP/PORT to server, the client side will be able to
 * try local IP/PORT when external IP/PORT failes
 * */
int self_ip(void)
{
  struct ifaddrs *ifAddrStruct = NULL;
  struct ifaddrs *ifa = NULL;
  struct in_addr *s = NULL;
  int ret = 0;

  getifaddrs(&ifAddrStruct);
  for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa ->ifa_addr->sa_family == AF_INET) { // check it is IP4
      char mask[INET_ADDRSTRLEN];
      void* mask_ptr = &((struct sockaddr_in*) ifa->ifa_netmask)->sin_addr;
      inet_ntop(AF_INET, mask_ptr, mask, INET_ADDRSTRLEN);
      if (strcmp(mask, "255.0.0.0") != 0) {
        // is a valid IP4 Address
        s = &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
        // printf("ip = %x \n", s->s_addr);
        ret = s->s_addr;
      } else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
        // is a valid IP6 Address
        // do something
      }
    }
  }
  if (ifAddrStruct != NULL) freeifaddrs(ifAddrStruct);
  return ret;
}

unsigned short calcsum(void *_data, int len)
{
  unsigned short *data = _data;
  unsigned long sum = 0;

  for(; len>1; len -= 2) {
    sum += *data ++;
    if(sum & 0x80000000)
    sum=(sum & 0xffff) + (sum >>16);
  }

  if(len ==1) {
    unsigned short i = 0;
    *(unsigned char*) (&i) = *(unsigned char*)data;
    sum+=i;
  }

  while(sum >>16) {
      sum=(sum & 0xffff)  +(sum>>16);
  }

  return (sum == 0xffff) ? sum: ~sum;
}

/* assume the buffer lead with p2p_payload_hdr, follow by payload data; and giving the len as total size */
void add_sum(void *buf, ssize_t len)
{
  int sum_len = len - ((char*)&((struct p2p_payload_hdr *)0)->seq - (char*)0);
  ((struct p2p_payload_hdr *)buf)->sum = calcsum(&((struct p2p_payload_hdr *)buf)->seq, sum_len);
}

int check_sum(void *buf, ssize_t len)
{
  int sum_len = len - ((char*)&((struct p2p_payload_hdr *)0)->seq - (char*)0);
  return ((struct p2p_payload_hdr *)buf)->sum - calcsum(&((struct p2p_payload_hdr *)buf)->seq, sum_len);
}

/* customer id, enquire with sales@everlink.net */
const char *cstm_id =   "t0001";                                // for DEMO and pilot production

int info_by_id(IN const char *hostname, IN int s_port, IN const char *id, IN int role, OUT struct hostinfo *i)
{
  static char req[HTTP_BUFFSIZE + 1];
  static char respbuf[HTTP_BUFFSIZE + 1];
  struct sockaddr_in boss_addr;
  struct hostent *hptr;
  int sock_tcp = 0;
  size_t n;
  char* ptr = NULL;

  // Get the ip address of BOSS server (server should be campal-api.everlink.net:80)
  // TODO IPV6 not handled
  if( (hptr = gethostbyname(hostname) ) == NULL ) {
    printf("Unknown host:%s\n", hostname);
    return -1;
  }

  boss_addr.sin_addr = *(struct in_addr*)hptr->h_addr_list[0];
  boss_addr.sin_port = htons(s_port);
  boss_addr.sin_family = AF_INET;
  printf("STUN BOSS -> %s:%d\n", inet_ntoa(boss_addr.sin_addr), s_port);

  // create a STREAM/TCP socket for HTTP request
  if((sock_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("tcp socket()");
    return -2;
  }

  // connect to BOSS server
  if(connect(sock_tcp, (struct sockaddr*)&boss_addr, sizeof(boss_addr)) < 0) {
    perror("tcp connect()");
    close(sock_tcp);
    return -3;
  }

  // Form the HTTP request
  snprintf(req, HTTP_BUFFSIZE,
    "GET /?cid=%s&id=%s&role=%d&fmt=plain HTTP/1.0\r\n"
    "Host: %s:%d\r\n"
    "\r\n",
    cstm_id, id, role, hostname, s_port);
  printf("HTTP REQUEST:\n%s", req);            // LOG

  // Send out HTTP request and read the response
  if (write(sock_tcp, req, strlen(req))>= 0) {
    while ((n = read(sock_tcp, respbuf, HTTP_BUFFSIZE)) > 0) {
      respbuf[n] = '\0';
      printf("\nRESP:\n%s\n", respbuf);    // LOG
      // Moving to response body
      ptr = strstr(respbuf, "\r\n\r\n");
      printf("\nPAYLOAD:\n%s\n", ptr + 4);  // LOG
      break;
    }
  }

  close(sock_tcp);

  // Scan the result from response body
  if (!ptr || 8 != sscanf(ptr + 4, "%s %d %s %d %s %d %s %d",
        i->sip, &i->spo, i->cip, &i->cpo, i->nip, &i->npo, i->hip, &i->hpo)) {
    printf("Invalid ID.\n");
    return -4;
  }

  return 0;
}

void init_buff_pool(struct buffer_pool **p)
{
  struct buffer_pool *bp = malloc(sizeof(struct buffer_pool));

  bp->f.filename[0] = 0;
  bp->f.filesize = 0;
  bp->f.fseq = 0;
  bp->f.in_retry = 0;

  for (int i = 0; i < CPAPP_BUFF_NUM; i++ ) {
    bp->node[i].datalen = 0;
    bp->node[i].buff = malloc(CPAPP_MAX_CHUNKSIZE);
  }

  *p = bp;
}


void clear_buff_pool(struct buffer_pool *bp)
{
  bp->f.filename[0] = 0;
  bp->f.filesize = 0;
  bp->f.fseq = 0;
  bp->f.in_retry = 0;

  for (int i = 0; i < CPAPP_BUFF_NUM; i++ ) {
    bp->node[i].datalen = 0;
  }
}



int load_buff_pool(struct buffer_pool *bp, const char *fn)
{
  FILE *f = fopen(fn, "r");
  strncpy(&bp->f.filename[0], fn, LEN_FILENAME);
  bp->f.filesize = get_filesize(fn);
  int i = 0; size_t bs;
  while ((bs = fread(bp->node[i].buff, 1, CPAPP_MAX_CHUNKSIZE, f)) > 0) {
    bp->node[i].datalen = bs;
    i++;
  }
  return 0;
}



size_t get_filesize(const char *fn)
{
  struct stat finfo;
  if (-1 == stat(fn, &finfo)) {
    printf("error stating file!\n");
  }
  return finfo.st_size;
}
