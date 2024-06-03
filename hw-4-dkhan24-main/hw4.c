#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define exit(N)                                                                \
  {                                                                            \
    fflush(stdout);                                                            \
    fflush(stderr);                                                            \
    _exit(N);                                                                  \
  }
#define SPACE " "

struct transfer {
  int socket;
  char *response;
};
void *transfer_thread(void *args) {
  struct transfer *transfer = (struct transfer *)args;
  int new_socket = transfer->socket;
  char *response = transfer->response;
  int response_size = strlen(response);
  int chunk_size = 4096;
  int num_chunks = response_size / chunk_size;
  int remainder = response_size % chunk_size;
  int chunk_count = 0;
  char chunk[chunk_size + 1];
  while (chunk_count < num_chunks) {
    strncpy(chunk, response + chunk_count * chunk_size, chunk_size);
    chunk[chunk_size] = '\0';
    write(new_socket, chunk, strlen(chunk));
    chunk_count++;
  }
  if (remainder != 0) {
    strncpy(chunk, response + chunk_count * chunk_size, remainder);
    chunk[remainder] = '\0';
    write(new_socket, chunk, strlen(chunk));
  }
  close(new_socket);
  pthread_exit(NULL);
}
static int get_port(void) {
  int fd = open("port.txt", O_RDONLY);
  if (fd < 0) {
    perror("Could not open port.txt");
    exit(1);
  }

  char buffer[32];
  int r = read(fd, buffer, sizeof(buffer));
  if (r < 0) {
    perror("Could not read port.txt");
    exit(1);
  }

  return atoi(buffer);
}

int main(int argc, char **argv) {
  int port = get_port();

  printf("Using port %d\n", port);
  printf("PID: %d\n", getpid());

  int sockfd, new_socket;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);
  if (((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0)) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;         // IPV4
  address.sin_addr.s_addr = INADDR_ANY; // Any IP
  address.sin_port = htons(port);       // Port

  if (bind(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  int listenreturn = listen(sockfd, 10);
  if (listenreturn < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  char data[4096] = {'<', 'e', 'm', 'p', 't', 'y', '>', '\0'};
  int data_size = 7;

  int no_of_requests = 0;
  int total_header_size = 0;
  int total_body_size = 0;
  int no_of_errors = 0;
  int total_error_size = 0;
  while (1) {

    char buffer[4096] = {0};
    char buffer_copy[4096] = {0};
    int num_lines_header = 0;
    char *body = NULL;
    char header[100][1024];
    char *response = (char *)malloc(100000000);
    int request_header_size = 0;
    if ((new_socket = accept(sockfd, (struct sockaddr *)&address,
                             (socklen_t *)&addrlen)) < 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }

    int r = read(new_socket, buffer, sizeof(buffer));
    if (r < 0) {
      perror("Could not read from socket");
      close(new_socket);
      continue;
    }
    strcpy(buffer_copy, buffer);

    char *method = strtok(buffer, " ");
    if (method == NULL ||
        (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0)) {
      sprintf(response, "HTTP/1.1 400 Bad Request\r\n\r\n");
      strcat(response, "\0");
      write(new_socket, response, strlen(response));
      close(new_socket);
      if (strcmp(buffer_copy, "") == 0) {
        continue;
      }
      no_of_errors++;
      total_error_size += strlen(response) - 2;
      continue;
    }
    request_header_size += strlen(method) + 1;

    char *path = strtok(NULL, " ");
    if (path == NULL) {
      sprintf(response, "HTTP/1.1 400 Bad Request\r\n\r\n");
      strcat(response, "\0");
      write(new_socket, response, strlen(response));
      close(new_socket);
      if (strcmp(buffer_copy, "") == 0) {
        continue;
      }
      no_of_errors++;
      total_error_size += strlen(response) - 2;
      continue;
    }
    request_header_size += strlen(path) + 1;

    char *http_version = strtok(NULL, "\n");
    if (http_version == NULL || strncmp(http_version, "HTTP/1.1", 8) != 0) {
      sprintf(response, "HTTP/1.1 400 Bad Request\r\n\r\n");
      strcat(response, "\0");
      write(new_socket, response, strlen(response));
      close(new_socket);
      if (strcmp(buffer_copy, "") == 0) {
        continue;
      }
      no_of_errors++;
      total_error_size += strlen(response) - 2;
      continue;
    }
    request_header_size += strlen(http_version) + 1;

    char *request = strtok(buffer_copy, "\n");
    request = strtok(NULL, "\n");
    while (request != NULL) {
      if (strncmp(request, "\r", 1) == 0 || strncmp(request, "\n", 1) == 0) {
        body = strtok(NULL, "\0");
        break;
      }
      strcpy(header[num_lines_header], request);
      num_lines_header++;
      request = strtok(NULL, "\n");
    }
    int header_size = request_header_size;
    for (int i = 0; i < num_lines_header; i++) {
      header_size += strlen(header[i]);
      if (header_size > 1024) {
        int diff = header_size - 1024;
        header_size -= diff;
        header[i][strlen(header[i]) - diff] = '\0';
        num_lines_header = i + 1;
        break;
      }
      if (i != num_lines_header - 1) {
        header_size++;
      }
    }
    if (body != NULL) {
      while (body[strlen(body) - 1] == '\n' || body[strlen(body) - 1] == '\r') {
        body[strlen(body) - 1] = '\0';
      }
    }
    if (body != NULL && strlen(body) > 1024) {
      int diff = strlen(body) - 1024;
      body[strlen(body) - diff] = '\0';
    }

    printf("\033[0;32m");
    printf("---------------------\n");
    printf("request structure:\n");
    printf("method: %s\n", method);
    printf("path: %s\n", path);
    printf("http_version: %s\n", http_version);
    printf("request_header_size: %d\n", request_header_size);
    printf("Headers: \n");
    for (int i = 0; i < num_lines_header; i++) {
      printf("%s\n", header[i]);
    }
    printf("header_size: %d\n", header_size);
    printf("header_count: %d\n", num_lines_header);
    printf("body: %s\n", body);
    if (body != NULL) {
      printf("body_size: %ld\n", strlen(body));
    } else {
      printf("body_size: 0\n");
    }
    printf("--------------------\n");
    printf("\033[0m");

    if (strcmp(method, "GET") == 0 && strcmp(path, "/ping") == 0) { // PING
      char *message = "pong";
      int message_length = strlen(message);
      sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s",
              message_length, message);
      strcat(response, "\0");
      no_of_requests++;
      total_header_size += strlen(response) - strlen(message);
      total_body_size += strlen(message);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/echo") == 0) { // ECHO
      int size = 0;
      for (int i = 0; i < num_lines_header; i++) {
        size += strlen(header[i]);
      }
      size += num_lines_header - 2;
      char *header_concat = malloc(size + 2);
      strcpy(header_concat, "");
      for (int i = 0; i < num_lines_header; i++) {
        strcat(header_concat, header[i]);
        if (i != num_lines_header - 1) {
          strcat(header_concat, "\n");
        } else {
          strcat(header_concat, "\0");
        }
      }
      while (header_concat[strlen(header_concat) - 1] == '\n' ||
             header_concat[strlen(header_concat) - 1] == '\r') {
        header_concat[strlen(header_concat) - 1] = '\0';
      }

      int header_value_len = strlen(header_concat);

      sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n",
              header_value_len);
      strncat(response, header_concat, header_value_len);
      strcat(response, "\0");
      no_of_requests++;
      total_header_size += strlen(response) - strlen(header_concat);
      total_body_size += strlen(header_concat);
    } else if (strcmp(method, "POST") == 0 &&
               strcmp(path, "/write") == 0) { // WRITE

      strcpy(data, body);
      if (body != NULL) {
        data_size = strlen(body);
        strcpy(data, body);
      } else {
        data_size = 7;
        strcpy(data, "<empty>");
      }

      sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n",
              data_size);
      strcat(response, data);
      strcat(response, "\0");
      no_of_requests++;
      total_header_size += strlen(response) - strlen(data);
      total_body_size += strlen(data);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/read") == 0) { // READ
      sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n",
              data_size);
      strcat(response, data);
      strcat(response, "\0");
      no_of_requests++;
      total_header_size += strlen(response) - strlen(data);
      total_body_size += strlen(data);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/stats") == 0) { // STATS {TODO}

      char *message = (char *)malloc(1000);
      sprintf(message,
              "Requests: %d\nHeader bytes: %d\nBody bytes: %d\nErrors: "
              "%d\nError bytes: %d",
              no_of_requests, total_header_size, total_body_size, no_of_errors,
              total_error_size);
      int message_length = strlen(message);
      sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s",
              message_length, message);
      strcat(response, "\0");
      no_of_requests++;
      total_header_size += strlen(response) - strlen(message);
      total_body_size += strlen(message);
    }
    


    else if (strcmp(method, "GET") == 0) { // GET <read file>
      char *file_path = path + 1;
      char cwd[1024];
      getcwd(cwd, sizeof(cwd));
      strcat(cwd, "/");
      strcat(cwd, file_path);
      strcpy(file_path, cwd);
      printf("file_path: %s\n", file_path);
      FILE *fp = fopen(file_path, "r");
      if (fp == NULL) {
        sprintf(response, "HTTP/1.1 404 Not Found\r\n\r\n");
        strcat(response, "\0");
        no_of_errors++;
        total_error_size += strlen(response);
      } else {
        fseek(fp, 0L, SEEK_END);
        int file_size = ftell(fp);
        fseek(fp, 0L, SEEK_SET);
        printf("file_size: %d\n", file_size);
        char *file_buffer = (char *)malloc(file_size + 1);
        int count = 0;
        int c = fgetc(fp);
        while ((char)c != EOF && count < file_size) {
          file_buffer[count++] = (char)c;
          c = fgetc(fp);
        }
        file_buffer[count] = '\0';
        response = (char *)malloc(file_size + 100);
        fclose(fp);
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n",
                file_size);
        strcat(response, file_buffer);
        strcat(response, "\0");
        no_of_requests++;
        total_header_size += strlen(response) - file_size;
        total_body_size += file_size;

        pthread_t thread;
        struct transfer *transfer =
            (struct transfer *)malloc(sizeof(struct transfer));
        transfer->socket = new_socket;
        transfer->response = response;
        pthread_create(&thread, NULL, transfer_thread, (void *)transfer);

        continue;
      }
    } else { // BAD REQUEST
      sprintf(response, "HTTP/1.1 400 Bad Request\r\n\r\n");
      strcat(response, "\0");
      no_of_errors++;
      total_error_size += strlen(response) - 2;
    }
    write(new_socket, response, strlen(response));
    close(new_socket);
  }

  return 0;
}
