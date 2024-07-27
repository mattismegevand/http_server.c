#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#define REQ_SIZE 4096
#define BODY_SIZE 2048
#define STR_SIZE 128
#define MAX_THREADS 16
#define MAX_ENCODINGS 16

typedef enum {
    METHOD_GET,
    METHOD_POST,
    METHOD_UNKNOWN
} http_method_t;

typedef struct {
    char req[REQ_SIZE];
    http_method_t method;
    char target[STR_SIZE];
    char http_version[STR_SIZE];
    char host[STR_SIZE];
    char user_agent[STR_SIZE];
    char encoding[MAX_ENCODINGS][STR_SIZE];
    char content_type[STR_SIZE];
    int content_length;
    char body[BODY_SIZE];
    FILE *fp;
} req_t;

typedef struct {
    int len;
    char resp[REQ_SIZE];
    char http_version[STR_SIZE];
    int status_code;
    char opt_response[STR_SIZE];
    char encoding[STR_SIZE];
    char content_type[STR_SIZE];
    int content_length;
    char body[BODY_SIZE];
} resp_t;

char directory[4096] = {'\0'};

int gzip(const char *input, int inputSize, char *output, int outputSize) {
    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = (uInt)inputSize;
    zs.next_in = (Bytef *)input;
    zs.avail_out = (uInt)outputSize;
    zs.next_out = (Bytef *)output;

    deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    return zs.total_out;
}

int home_route(req_t *req, resp_t *resp) {
    if (strcmp(req->target, "/") == 0) {
        resp->status_code = 200;
        strcpy(resp->opt_response, "OK");
        return 1;
    }
    return 0;
}

int echo_route(req_t *req, resp_t *resp) {
    char *start;
    if ((start = strstr(req->target, "/echo/"))) {
        start += strlen("/echo/");
        resp->status_code = 200;
        strcpy(resp->opt_response, "OK");
        strcpy(resp->content_type, "text/plain");
        resp->content_length = strlen(start);
        strncpy(resp->body, start, resp->content_length);
        return 1;
    }
    return 0;
}

int useragent_route(req_t *req, resp_t *resp) {
    char *start;
    if ((start = strstr(req->target, "/user-agent"))) {
        start += strlen("/user-agent");
        resp->status_code = 200;
        strcpy(resp->opt_response, "OK");
        strcpy(resp->content_type, "text/plain");
        resp->content_length = strlen(req->user_agent);
        strncpy(resp->body, req->user_agent, resp->content_length);
        return 1;
    }
    return 0;
}

int files_route(req_t *req, resp_t *resp) {
    char *start;
    if ((start = strstr(req->target, "/files/"))) {
        char *filepath;
        start += strlen("/files/");
        asprintf(&filepath, "%s%s", directory, start);
        if (req->method == METHOD_GET) {
            if ((req->fp = fopen(filepath, "r")) == NULL) {
                resp->status_code = 404;
                strcpy(resp->opt_response, "Not Found");
            } else {
                fseek(req->fp, 0, SEEK_END);
                long length = ftell(req->fp);
                fseek(req->fp, 0, SEEK_SET);
                char *buffer = malloc(length);
                if (buffer) {
                    fread(buffer, 1, length, req->fp);
                    resp->status_code = 200;
                    strcpy(resp->opt_response, "OK");
                    strcpy(resp->content_type, "application/octet-stream");
                    resp->content_length = length;
                    strncpy(resp->body, buffer, resp->content_length);
                }
                free(buffer);
                fclose(req->fp);
            }
        } else {
            if ((req->fp = fopen(filepath, "w")) == NULL) {
                resp->status_code = 404;
                strcpy(resp->opt_response, "Not Found");
            } else {
                if (fwrite(req->body, 1, req->content_length, req->fp) != req->content_length) {
                    resp->status_code = 404;
                    strcpy(resp->opt_response, "Not Found");
                } else {
                    resp->status_code = 201;
                    strcpy(resp->opt_response, "Created");
                }
                fclose(req->fp);
            }
        }
        free(filepath);
        return 1;
    }
    return 0;
}

resp_t handle_req(req_t *req) {
    resp_t resp = {.http_version = "HTTP/1.1", .status_code = -1, .content_length = -1};
    if (!(home_route(req, &resp) || echo_route(req, &resp) || useragent_route(req, &resp) || files_route(req, &resp))) {
        resp.status_code = 404;
        strcpy(resp.opt_response, "Not Found");
    }
    return resp;
}

void format_resp(resp_t *resp, req_t *req) {
    if (resp->http_version[0] != '\0') {
        strcat(resp->resp, resp->http_version);
        strcat(resp->resp, " ");
    }
    if (resp->status_code != -1) {
        char s[STR_SIZE];
        snprintf(s, STR_SIZE, "%d ", resp->status_code);
        strcat(resp->resp, s);
    }
    if (resp->opt_response[0] != '\0') {
        strcat(resp->resp, resp->opt_response);
    }

    strcat(resp->resp, "\r\n");

    for (int i = 0; i < MAX_ENCODINGS; i++) {
        if (req->encoding[i][0] == '\0') {
            break;
        }
        if (strcmp(req->encoding[i], "gzip") == 0) {
            strcpy(resp->encoding, req->encoding[i]);
            strcat(resp->resp, "Content-Encoding: ");
            strcat(resp->resp, resp->encoding);
            strcat(resp->resp, "\r\n");

            char body_copy[BODY_SIZE];
            strcpy(body_copy, resp->body);
            resp->content_length = gzip(body_copy, resp->content_length, resp->body, BODY_SIZE);
        }
    }
    if (resp->content_type[0] != '\0') {
        strcat(resp->resp, "Content-Type: ");
        strcat(resp->resp, resp->content_type);
        strcat(resp->resp, "\r\n");
    }
    if (resp->content_length != -1) {
        char s[STR_SIZE];
        snprintf(s, STR_SIZE, "%d\r\n", resp->content_length);
        strcat(resp->resp, "Content-Length: ");
        strcat(resp->resp, s);
    }

    strcat(resp->resp, "\r\n");

    resp->len = strlen(resp->resp);
    if (resp->body[0] != '\0') {
        memcpy(resp->resp + strlen(resp->resp), resp->body, resp->content_length);
        resp->len += resp->content_length;
    }
}

resp_t make_resp(req_t *req) {
    resp_t resp = handle_req(req);
    format_resp(&resp, req);
    return resp;
}

http_method_t parse_method(const char *method_str) {
    if (strcasecmp(method_str, "GET") == 0) return METHOD_GET;
    if (strcasecmp(method_str, "POST") == 0) return METHOD_POST;
    return METHOD_UNKNOWN;
}

void parse_req(req_t *req) {
    char *part, *part_saveptr, *token_saveptr;
    part = strtok_r(req->req, "\r\n", &part_saveptr);
    int i = 0;
    while (part) {
        int encoding_count = 0;
        char part_copy[BODY_SIZE];
        strncpy(part_copy, part, BODY_SIZE);

        char *token = strtok_r(part_copy, " ", &token_saveptr);
        int j = 0;
        char header_field[STR_SIZE];
        while (token) {
            if (i == 0) {
                switch (j) {
                case 0:
                    req->method = parse_method(token);
                    break;
                case 1:
                    strncpy(req->target, token, STR_SIZE);
                    break;
                case 2:
                    strncpy(req->http_version, token, STR_SIZE);
                    break;
                default:
                    break;
                }
            } else {
                if (j == 0) {
                    if (token[strlen(token) - 1] == ':') {
                        strncpy(header_field, token, STR_SIZE);
                    } else {
                        memcpy(req->body, part, req->content_length);
                        header_field[0] = '\0';
                    }
                } else {
                    if (strcmp(header_field, "Host:") == 0) {
                        strncpy(req->host, token, STR_SIZE);
                    } else if (strcmp(header_field, "User-Agent:") == 0) {
                        strncpy(req->user_agent, token, STR_SIZE);
                    } else if (strcmp(header_field, "Accept-Encoding:") == 0) {
                        int end = strlen(token) - 1;
                        if (token[end] == ',') {
                            token[end] = '\0';
                        }
                        strncpy(req->encoding[encoding_count++], token, STR_SIZE);
                    } else if (strcmp(header_field, "Content-Type:") == 0) {
                        strncpy(req->content_type, token, STR_SIZE);
                    } else if (strcmp(header_field, "Content-Length:") == 0) {
                        req->content_length = (int)strtol(token, NULL, 10);
                    }
                }
            }
            token = strtok_r(NULL, " ", &token_saveptr);
            j++;
        }
        part = strtok_r(NULL, "\r\n", &part_saveptr);
        i++;
    }
}

void *thread_fn(void *arg) {
    int client_fd = (int)(intptr_t)arg;

    req_t req = {};
    recv(client_fd, req.req, REQ_SIZE, 0);

    parse_req(&req);

    resp_t resp = make_resp(&req);
    printf("Sending: %s\n", resp.resp);
    send(client_fd, resp.resp, resp.len, 0);

    close(client_fd);
    return NULL;
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    int server_fd, client_addr_len;
    struct sockaddr_in client_addr;

    if (argc == 3 && strcmp(argv[1], "--directory") == 0) {
        strncpy(directory, argv[2], REQ_SIZE);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEADDR failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
    }

    printf("Waiting for a client to connect...\n");
    client_addr_len = sizeof(client_addr);

    pthread_t thread_ids[MAX_THREADS];
    int thread_count = 0;

    int client_fd;
    while ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len)) >= 0) {
        printf("Client connected\n");
        if (thread_count >= MAX_THREADS) {
            printf("Max thread limit reached. Exiting...\n");
            break;
        }
        pthread_create(&thread_ids[thread_count], NULL, thread_fn, (void *)(intptr_t)client_fd);
        thread_count++;
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    close(server_fd);

    return 0;
}
