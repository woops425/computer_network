#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <ctype.h>

#define PORT 80
#define BUFFER_SIZE 1024
#define MAX_PACKETS 25

typedef struct PacketResponse {
    int sid;
    int seq;
    char* payload;
} Packet;

static Packet res[MAX_PACKETS];
static int idx = 0;
static char* sid = "202010629";

int already_exists(int seq) {
    for (int i = 0; i < idx; i++) {
        if (res[i].seq == seq) return 1;
    }
    return 0;
}

int parse_json_response(const char* response) {
    const char* json_start = strchr(response, '{');
    if (!json_start) return -1;
    int student_id = 0, seq = 0;
    char payload[100] = {0};
    if (sscanf(json_start, "{\"sid\": %d, \"seq\": %d, \"data\": \"%[^\"]\"}", &student_id, &seq, payload) != 3) return -1;
    if (already_exists(seq)) return -2;
    res[idx].sid = student_id;
    res[idx].seq = seq;
    res[idx].payload = strdup(payload);
    printf("seq %d: {\"sid\": %d, \"data\": \"%s\"}\n", seq, student_id, payload);
    idx++;
    return seq + 1;
}

int initialize_socket(const char* host, struct sockaddr_in* serv_addr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    memset(serv_addr, 0, sizeof(*serv_addr));
    serv_addr->sin_family = AF_INET;
    serv_addr->sin_port = htons(PORT);
    struct hostent* he = gethostbyname(host);
    if (!he) return -1;
    memcpy(&serv_addr->sin_addr, he->h_addr_list[0], he->h_length);
    if (connect(sock, (struct sockaddr*)serv_addr, sizeof(*serv_addr)) < 0) return -1;
    return sock;
}

int send_request_and_receive_response(int sock, const char* request, char* buffer) {
    if (send(sock, request, strlen(request), 0) < 0) return -1;
    ssize_t bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0) return -1;
    buffer[bytes_received] = '\0';
    return 0;
}

void free_resources() {
    for (int i = 0; i < idx; i++) free(res[i].payload);
}

int is_alpha_word(const char* word) {
    for (int i = 0; word[i]; i++) if (!isalpha(word[i])) return 0;
    return 1;
}

void decode_and_submit(int sock) {
    for (int i = 0; i < idx - 1; i++) {
        for (int j = i + 1; j < idx; j++) {
            if (res[i].seq > res[j].seq) {
                Packet tmp = res[i]; res[i] = res[j]; res[j] = tmp;
            }
        }
    }

    char word[100] = {0};
    int best_parity = -1;
    for (int p = 0; p < 8; p++) {
        char temp[100] = {0};
        int valid = 1;
        for (int i = 0; i < idx; i++) {
            char bits[8] = {0}; int k = 0;
            for (int j = 0; j < 8; j++) {
                if (j == p) continue;
                bits[k++] = res[i].payload[j];
            }
            int ch = strtol(bits, NULL, 2);
            if (!isalpha(ch)) { valid = 0; break; }
            temp[i] = (char)ch;
        }
        temp[idx] = '\0';
        if (valid && strlen(temp) >= 5) {
            strcpy(word, temp);
            best_parity = p;
            break;
        }
    }

    if (best_parity == -1) {
        return;
    }

    printf("복호화된 단어: %s\n", word);
    char submit_request[BUFFER_SIZE];
    sprintf(submit_request,
        "GET /submit/?word=%s&sid=%s HTTP/1.1\r\n"
        "Host: nisl.smu.ac.kr\r\n"
        "Connection: close\r\n"
        "User-Agent: comnet/1.0\r\n\r\n",
        word, sid);

    if (send(sock, submit_request, strlen(submit_request), 0) < 0) {
        perror("정답 제출 실패");
    } else {
        char buffer[BUFFER_SIZE * 4] = {0};
        int total = 0, rlen;
        while ((rlen = recv(sock, buffer + total, BUFFER_SIZE * 4 - 1 - total, 0)) > 0) total += rlen;
        buffer[total] = '\0';
        const char* json_start = strchr(buffer, '{');
        if (json_start) printf("%s\n", json_start);
        else printf("JSON이 포함되어 있지 않음.\n");
    }
}

int main(int argc, char const* argv[]) {
    const char* host = "nisl.smu.ac.kr";
    char request[BUFFER_SIZE];
    sprintf(request,
        "GET /%s/ HTTP/1.1\r\n"
        "Host: nisl.smu.ac.kr\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: comnet/1.0\r\n\r\n",
        sid);
    char buffer[BUFFER_SIZE] = {0};
    struct sockaddr_in serv_addr;
    int sock = initialize_socket(host, &serv_addr);
    if (sock < 0) return -1;

    while (1) {
        if (send_request_and_receive_response(sock, request, buffer) < 0) break;
        int result = parse_json_response(buffer);
        if (result == -2) break;
        if (result < 0) break;
    }

    decode_and_submit(sock);
    free_resources();
    close(sock);
    return 0;
}
