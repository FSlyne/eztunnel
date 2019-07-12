
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> 
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <netinet/tcp.h>
#include <pthread.h>

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define BUFSIZE 2000   
#define CLIENT 0
#define SERVER 1
#define PORT 55555

int tap_fd;
int sockfd;
struct sockaddr_in localaddr, remoteaddr, remoteaddr2;

unsigned long int tap2net = 0, net2tap = 0;


int debug;
char *progname;

/**************************************************************************
 * tun_alloc: allocates or reconnects to a tun/tap device. The caller     *
 *            must reserve enough space in *dev.                          *
 **************************************************************************/
int tun_alloc(char *dev, int flags) {

  struct ifreq ifr;
  int fd, err;
  char *clonedev = "/dev/net/tun";

  if( (fd = open(clonedev , O_RDWR)) < 0 ) {
    perror("Opening /dev/net/tun");
    return fd;
  }

  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = flags;

  if (*dev) {
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
  }

  if( (err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
    perror("ioctl(TUNSETIFF)");
    close(fd);
    return err;
  }

  strcpy(dev, ifr.ifr_name);

  return fd;
}

/**************************************************************************
 * cread: read routine that checks for errors and exits if an error is    *
 *        returned.                                                       *
 **************************************************************************/
int cread(int fd, char *buf, int n){
  
  int nread;

  if((nread=read(fd, buf, n)) < 0){
    perror("Reading data");
    exit(1);
  }
  return nread;
}

/**************************************************************************
 * cwrite: write routine that checks for errors and exits if an error is  *
 *         returned.                                                      *
 **************************************************************************/
int cwrite(int fd, char *buf, int n){
  
  int nwrite;

  if((nwrite=write(fd, buf, n)) < 0){
    perror("Writing data");
    exit(1);
  }
  return nwrite;
}

/**************************************************************************
 * read_n: ensures we read exactly n bytes, and puts them into "buf".     *
 *         (unless EOF, of course)                                        *
 **************************************************************************/
int read_n(int fd, char *buf, int n) {

  int nread, left = n;

  while(left > 0) {
    if ((nread = cread(fd, buf, left)) == 0){
      return 0 ;      
    }else {
      left -= nread;
      buf += nread;
    }
  }
  return n;  
}

/**************************************************************************
 * do_debug: prints debugging stuff (doh!)                                *
 **************************************************************************/
void do_debug(char *msg, ...){
  
  va_list argp;
  
  if(debug) {
	va_start(argp, msg);
	vfprintf(stderr, msg, argp);
	va_end(argp);
  }
}

/**************************************************************************
 * my_err: prints custom error messages on stderr.                        *
 **************************************************************************/
void my_err(char *msg, ...) {

  va_list argp;
  
  va_start(argp, msg);
  vfprintf(stderr, msg, argp);
  va_end(argp);
}

/**************************************************************************
 * usage: prints usage and exits.                                         *
 **************************************************************************/
void usage(void) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "%s -i <ifacename> [-s|-c <serverIP>] [-p <port>] [-u|-a] [-d]\n", progname);
  fprintf(stderr, "%s -h\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "-i <ifacename>: Name of interface to use (mandatory)\n");
  fprintf(stderr, "-s|-c <serverIP>: run in server mode (-s), or specify server address (-c <serverIP>) (mandatory)\n");
  fprintf(stderr, "-p <port>: port to listen on (if run in server mode) or to connect to (in client mode), default 55555\n");
  fprintf(stderr, "-u|-a: use TUN (-u, default) or TAP (-a)\n");
  fprintf(stderr, "-d: outputs debug information while running\n");
  fprintf(stderr, "-h: prints this help text\n");
  exit(1);
}

void *tap2netThread(void *vargp) {
      char buffer[BUFSIZE];
      uint16_t nread, nwrite, plength;

      socklen_t slen=sizeof(remoteaddr);

      while(1) {
      nread = cread(tap_fd, buffer, BUFSIZE);

      tap2net++;
      do_debug("TAP2NET %lu: Read %d bytes from the tap interface\n", tap2net, nread);

      /* write length + packet */
      sendto(sockfd, buffer, BUFSIZE, 0, (struct sockaddr*) &remoteaddr, slen);

      do_debug("TAP2NET %lu: Written %d bytes to the network\n", tap2net, nwrite);
      }
      return 0;
}

void *net2tapThread(void *vargp) {
      char buffer[BUFSIZE];
      uint16_t nread, nwrite, plength;

      socklen_t slen=sizeof(remoteaddr);

      while(1) {
      nread = recvfrom(sockfd,buffer,BUFSIZE, 0, (struct sockaddr*) &remoteaddr2, &slen);

      net2tap++;
      do_debug("NET2TAP %lu: Read %d bytes from the network\n", net2tap, nread);

      /* now buffer[] contains a full packet or frame, write it into the tun/tap interface */
      nwrite = cwrite(tap_fd, buffer, nread);
      do_debug("NET2TAP %lu: Written %d bytes to the tap interface\n", net2tap, nwrite);
      }
      return 0;
}

int main(int argc, char *argv[]) {
  
  int option;
  int flags = IFF_TUN;
  char if_name[IFNAMSIZ] = "";
  struct sockaddr_in local, remote;
  char remote_ip[16] = "";            /* dotted quad IP string */
  unsigned short int port = PORT;
  int optval = 1;
  socklen_t remotelen;
  int cliserv = -1;    /* must be specified on cmd line */

  progname = argv[0];
  
  /* Check command line options */
  while((option = getopt(argc, argv, "i:r:p:uahd")) > 0) {
    switch(option) {
      case 'd':
        debug = 1;
        break;
      case 'h':
        usage();
        break;
      case 'i':
        strncpy(if_name,optarg, IFNAMSIZ-1);
        break;
      case 'r':
        strncpy(remote_ip,optarg,15);
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 'u':
        flags = IFF_TUN;
        break;
      case 'a':
        flags = IFF_TAP;
        break;
      default:
        my_err("Unknown option %c\n", option);
        usage();
    }
  }

  argv += optind;
  argc -= optind;

  if(argc > 0) {
    my_err("Too many options!\n");
    usage();
  }

  if(*if_name == '\0') {
    my_err("Must specify interface name!\n");
    usage();
  }

  /* initialize tun/tap interface */
  if ( (tap_fd = tun_alloc(if_name, flags | IFF_NO_PI)) < 0 ) {
    my_err("Error connecting to tun/tap interface %s!\n", if_name);
    exit(1);
  }

  do_debug("Successfully connected to interface %s\n", if_name);

  memset(&localaddr, 0, sizeof(localaddr));
  memset(&remoteaddr, 0, sizeof(remoteaddr));

  remoteaddr.sin_family=AF_INET;
  remoteaddr.sin_port=htons(65001);
  inet_aton(remote_ip,&remoteaddr.sin_addr);

  localaddr.sin_family=AF_INET;
  localaddr.sin_addr.s_addr = INADDR_ANY;
  localaddr.sin_port=htons(65001);

  sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); 
  if (bind(sockfd,(struct sockaddr*) &localaddr,sizeof(localaddr))<0)
  {
     perror("bind failed");
     exit(EXIT_FAILURE);
  }

  fd_set rd_set;
  FD_ZERO(&rd_set);
  FD_SET(tap_fd, &rd_set); FD_SET(sockfd, &rd_set);
  int ret, maxfd;
  
  /* use select() to handle two descriptors at once */
  maxfd = (tap_fd > sockfd)?tap_fd:sockfd;

  ret = select(maxfd + 1, &rd_set, NULL, NULL, NULL);

    if (ret < 0) {
      perror("select()");
      exit(1);
    }


  pthread_t tid1,tid2;
  pthread_create(&tid1, NULL, tap2netThread, NULL);
  pthread_create(&tid2, NULL, net2tapThread, NULL);

  while(1){
    sleep(1);
  }

  return(0);
}
