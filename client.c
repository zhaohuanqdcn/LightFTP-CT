#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define NUM_THREADS 4
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 21
#define BUFFER_SIZE 1024

int handle_pasv(char* pasv_resp) {
    int p1, p2;
    sscanf(pasv_resp, "227 Entering Passive Mode (%*d,%*d,%*d,%*d,%d,%d)", &p1, &p2);
    int dataPort = p1 * 256 + p2;

    int data_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(data_sockfd < 0) {
        perror("Error creating data socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(dataPort);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if(connect(data_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server for data transfer");
        exit(EXIT_FAILURE);
    }
    return data_sockfd;
}

void download_data(int data_sockfd) {
    FILE* file = fopen("temp.txt", "wb");
    char buffer[BUFFER_SIZE];
    int bytes_read;
    memset(buffer, 0, BUFFER_SIZE);
    while((bytes_read = recv(data_sockfd, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_read, file);
        memset(buffer, 0, BUFFER_SIZE);
    }
    fclose(file);
    close(data_sockfd);
}

void upload_data(int data_sockfd) {
    FILE* file = fopen("data.txt", "rb");
    if (!file) {
        perror("Failed to open file");
        close(data_sockfd);
        return;
    }
    char buffer[BUFFER_SIZE];
    int bytes_read;
    memset(buffer, 0, BUFFER_SIZE);
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        int bytes_sent = send(data_sockfd, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send data");
            break;
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
    fclose(file);
    close(data_sockfd);
}


void* client_thread(void *arg) {
    char* filename = (char*) arg;
    int id;
    sscanf(filename, "test%d.in", &id);
    
    int sockfd;
    int data_sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char rcv_buf[BUFFER_SIZE];
    FILE *file;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        pthread_exit((void*)-1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        pthread_exit((void*)-1);
    }

    if (recv(sockfd, rcv_buf, BUFFER_SIZE, 0) < 0) {
            perror("Error receiving response from server");
        }
    printf("#%d - Server: %s", id, rcv_buf);

    file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        close(sockfd);
        pthread_exit((void*)-1);
    }

    while (fgets(buffer, BUFFER_SIZE, file) != NULL) {
        
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("Error sending command to server");
            pthread_exit((void*)-1);
        }

        memset(rcv_buf, 0, BUFFER_SIZE);
        if (recv(sockfd, rcv_buf, BUFFER_SIZE, 0) < 0) {
            perror("Error receiving response from server");
            pthread_exit((void*)-1);
        }
        printf("#%d - Client: %s#%d - Server: %s", id, buffer, id, rcv_buf);
        
        if (strstr(buffer, "PASV") != NULL
         && strstr(rcv_buf, "227") != NULL) {
            data_sockfd = handle_pasv(rcv_buf);
        } else if ((strstr(buffer, "LIST") != NULL 
                 || strstr(buffer, "RETR") != NULL
                 || strstr(buffer, "NLST") != NULL)
                 && strstr(rcv_buf, "150") != NULL) {
            download_data(data_sockfd);
            
            memset(rcv_buf, 0, BUFFER_SIZE);
            if (recv(sockfd, rcv_buf, BUFFER_SIZE, 0) < 0) {
                perror("Error receiving response from server");
                pthread_exit((void*)-1);
            }
            printf("#%d - Server: %s", id, rcv_buf);
        } else if (strstr(buffer, "STOR") != NULL
                && strstr(rcv_buf, "150") != NULL) {
            upload_data(data_sockfd);
            
            memset(rcv_buf, 0, BUFFER_SIZE);
            if (recv(sockfd, rcv_buf, BUFFER_SIZE, 0) < 0) {
                perror("Error receiving response from server");
                pthread_exit((void*)-1);
            }
            printf("#%d - Server: %s", id, rcv_buf);
        }
    }

    fclose(file);
    close(sockfd);

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    char *filenames[NUM_THREADS] = {"test1.in", "test2.in", "test3.in", "test4.in"};

    for(int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, client_thread, (void *)filenames[i]);
        if (rc) {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    /* wait for all threads to complete */
    for(int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        // printf("Client #%d joined\n", i + 1);
    }

    return 0;
}
