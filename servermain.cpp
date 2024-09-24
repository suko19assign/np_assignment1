#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <math.h>       // Needed for fabs (floating-point comparison)
#include "calcLib.h"

#define BACKLOG 5  // Max clients in queue
#define BUFFER_SIZE 1024
#define TIMEOUT 5   // 5 seconds timeout for client response

using namespace std;

int main(int argc, char *argv[]){
  
  /*
    Read first input, assumes <ip>:<port> syntax, convert into one string (Desthost) and one integer (port). 
     Atm, works only on dotted notation, i.e. IPv4 and DNS. IPv6 does not work if its using ':'. 
  */
  char delim[]=":";
  char *Desthost=strtok(argv[1],delim);
  char *Destport=strtok(NULL,delim);
  // *Desthost now points to a sting holding whatever came before the delimiter, ':'.
  // *Destport points to whatever string came after the delimiter. 

  int port = atoi(Destport);
#ifdef DEBUG  
  printf("Host %s, and port %d.\n",Desthost,port);
#endif

  // Resolve the host to an IP address
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;

  int status = getaddrinfo(Desthost, Destport, &hints, &res);
  if (status != 0) {
      fprintf(stderr, "ERROR: RESOLVE ISSUE\n");
      return 1;
  }

  // Create the server socket
  int server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (server_fd < 0) {
      perror("ERROR: CANT CREATE SOCKET");
      freeaddrinfo(res);
      return 1;
  }

  // Bind the socket to the specified port
  if (bind(server_fd, res->ai_addr, res->ai_addrlen) < 0) {
      perror("ERROR: CANT BIND SOCKET");
      close(server_fd);
      freeaddrinfo(res);
      return 1;
  }

  freeaddrinfo(res);  // No longer needed after binding

  // Start listening for connections
  if (listen(server_fd, BACKLOG) < 0) {
      perror("ERROR: LISTEN");
      close(server_fd);
      return 1;
  }

  printf("Server listening on %s:%d\n", Desthost, port);

  while (1) {
      struct sockaddr_storage client_addr;
      socklen_t addr_size = sizeof(client_addr);
      int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
      if (client_fd < 0) {
          perror("ERROR: ACCEPT");
          continue;
      }

#ifdef DEBUG
      printf("Accepted connection from client.\n");
#endif

      // Handle client interaction
      char buffer[BUFFER_SIZE];
      memset(buffer, 0, sizeof(buffer));

      // Send supported protocol
      const char *protocol_msg = "TEXT TCP 1.0\n\n";
      if (send(client_fd, protocol_msg, strlen(protocol_msg), 0) < 0) {
          perror("ERROR: SEND");
          close(client_fd);
          continue;
      }

      // Receive OK response
      if (recv(client_fd, buffer, sizeof(buffer) - 1, 0) <= 0 || strcmp(buffer, "OK\n") != 0) {
          perror("ERROR: PROTOCOL MISMATCH OR TIMEOUT");
          close(client_fd);
          continue;
      }

      initCalcLib();  // Initialize the calc library for operation generation

      // Generate random assignment
      char *operation = randomType();
      double fv1, fv2;
      int iv1, iv2;
      char msg[BUFFER_SIZE];
      memset(msg, 0, sizeof(msg));

      if (operation[0] == 'f') { // Floating-point operation
          fv1 = randomFloat();
          fv2 = randomFloat();
          snprintf(msg, sizeof(msg), "%s %8.8g %8.8g\n", operation, fv1, fv2);
      } else { // Integer operation
          iv1 = randomInt();
          iv2 = randomInt();
          snprintf(msg, sizeof(msg), "%s %d %d\n", operation, iv1, iv2);
      }

      // Send the assignment to the client
      if (send(client_fd, msg, strlen(msg), 0) < 0) {
          perror("ERROR: SEND ASSIGNMENT");
          close(client_fd);
          continue;
      }

      // Set timeout for client response
      struct timeval tv;
      tv.tv_sec = TIMEOUT;
      tv.tv_usec = 0;
      setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

      // Receive the client's response
      memset(buffer, 0, sizeof(buffer));
      if (recv(client_fd, buffer, sizeof(buffer) - 1, 0) <= 0) {
          const char *timeout_msg = "ERROR TO\n";
          send(client_fd, timeout_msg, strlen(timeout_msg), 0);
          perror("ERROR: TIMEOUT");
          close(client_fd);
          continue;
      }

      // Validate client's response
      if (operation[0] == 'f') { // Floating-point operation
          double client_result;
          sscanf(buffer, "%lg", &client_result);
          double server_result;
          if (strcmp(operation, "fadd") == 0) server_result = fv1 + fv2;
          else if (strcmp(operation, "fsub") == 0) server_result = fv1 - fv2;
          else if (strcmp(operation, "fmul") == 0) server_result = fv1 * fv2;
          else if (strcmp(operation, "fdiv") == 0) server_result = fv1 / fv2;

          double difference = fabs(client_result - server_result);
          if (difference < 0.0001) {
              send(client_fd, "OK\n", 3, 0);
          } else {
              send(client_fd, "ERROR\n", 6, 0);
          }
      } else { // Integer operation
          int client_result;
          sscanf(buffer, "%d", &client_result);
          int server_result;
          if (strcmp(operation, "add") == 0) server_result = iv1 + iv2;
          else if (strcmp(operation, "sub") == 0) server_result = iv1 - iv2;
          else if (strcmp(operation, "mul") == 0) server_result = iv1 * iv2;
          else if (strcmp(operation, "div") == 0) server_result = iv1 / iv2;

          if (client_result == server_result) {
              send(client_fd, "OK\n", 3, 0);
          } else {
              send(client_fd, "ERROR\n", 6, 0);
          }
      }

      // Close the connection after handling the client
      close(client_fd);
  }

  close(server_fd);
  return 0;
}
