#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>

#define PORT 80                  // HTTP 포트 번호
#define BUFFER_SIZE 1024         // 버퍼 크기 (받을수 있는 패킷의 최대 크기)
#define MAX_PACKETS 25           // 최대 패킷 수 (단어의 길이)


typedef struct PacketResponse {
    int sid;        // 학생 ID
    int seq;        // 패킷 시퀀스 번호 (ex, 1, 2, 3) 1~단어길이
    char* payload;  // 패킷 데이터 (ex, "01000001")  
} Packet;

static Packet res[MAX_PACKETS];     
static int idx = 0;                 // 패킷 응답 인덱스
static char* sid = "202010629";     // 학생 ID
static int parity_pos = 0;          // 탐지된 parity bit 위치

// seq 중복 여부 확인
int already_exists(int seq) {
    for (int i = 0; i < idx; i++) {
        if (res[i].seq == seq) return 1;
    }
    return 0;
}

// JSON 응답을 파싱하는 함수
int parse_json_response(const char* response) {
    const char* json_start = strchr(response, '{');
    if (!json_start) {
        fprintf(stderr, "JSON 시작 부분을 찾지 못했습니다.\n");
        return -1;
    }
    int student_id = 0;
    int seq = 0;
    char payload[100] = {0};

    // JSON 파싱
    if (sscanf(json_start, "{\"sid\": %d, \"seq\": %d, \"data\": \"%[^\"]\"}", &student_id, &seq, payload) != 3) {
        fprintf(stderr, "JSON 파싱 실패\n");
        return -1;
    }

    if (already_exists(seq)) return -2; // 중복 감지

    // 메모리 할당 및 데이터 저장
    res[idx].sid = student_id;
    res[idx].seq = seq;
    res[idx].payload = strdup(payload);  // 문자열 복사
    if (!res[idx].payload) {
        fprintf(stderr, "메모리 할당 실패\n");
        return -1;
    }

    printf("seq %d: {\"sid\": %d, \"data\": \"%s\"}\n", seq, student_id, payload);

    idx++;
    return seq + 1;
}

// 소켓을 초기화하고 서버에 연결하는 함수
int initialize_socket(const char* host, struct sockaddr_in* serv_addr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("소켓 생성 실패");
        return -1;
    }

    memset(serv_addr, 0, sizeof(*serv_addr));
    serv_addr->sin_family = AF_INET;
    serv_addr->sin_port = htons(PORT);

    struct hostent* he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "호스트 이름 변환 실패\n");
        close(sock);
        return -1;
    }
    memcpy(&serv_addr->sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)serv_addr, sizeof(*serv_addr)) < 0) {
        perror("서버 연결 실패");
        close(sock);
        return -1;
    }

    return sock;
}

// TODO: 서버에 요청을 보내고 응답을 받는 함수
int send_request_and_receive_response(int sock, const char* request, char* buffer) {
    if (send(sock, request, strlen(request), 0) < 0) {
        perror("요청 전송 실패");
        return -1;
    }

    ssize_t bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0) {
        perror("응답 수신 실패");
        return -1;
    }

    buffer[bytes_received] = '\0';
    return 0;
}

// 리소스를 해제하는 함수
void free_resources() {
    for (int i = 0; i < idx; i++) {
        free(res[i].payload);
    }
}

// 문자열이 알파벳으로만 이루어져 있는지 확인
int is_alpha_word(const char* word) {
    for (int i = 0; word[i]; i++) {
        if (!isalpha(word[i])) return 0;
    }
    return 1;
}

// 패킷 정렬, 복호화, 정답 제출
void decode_and_submit(int sock) {
    // seq 순서대로 정렬
    for (int i = 0; i < idx - 1; i++) {
        for (int j = i + 1; j < idx; j++) {
            if (res[i].seq > res[j].seq) {
                Packet tmp = res[i];
                res[i] = res[j];
                res[j] = tmp;
            }
        }
    }

    char word[100] = {0};
    int found = 0;

    // 0~7까지 모든 위치에 대해 parity bit 후보로 탐색
    for (int p = 0; p < 8; p++) {
        int valid = 1;
        for (int i = 0; i < idx; i++) {
            char data_bits[8] = {0};
            int k = 0;
            for (int j = 0; j < 8; j++) {
                if (j == p) continue;
                data_bits[k++] = res[i].payload[j];
            }
            int ch = strtol(data_bits, NULL, 2);
            if (!isalpha(ch)) {
                valid = 0;
                break;
            }
            word[i] = (char)ch;
        }
        word[idx] = '\0';

        // 조건에 맞는 영단어 발견 시
        if (valid && strlen(word) >= 10) {
            parity_pos = p;
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("알파벳으로만 이루어진 10글자 이상의 영단어를 찾을 수 없음\n");
        return;
    }

    printf("유추한 영단어: %s\n", word);

    // 정답 제출 요청
    char submit_request[BUFFER_SIZE];
    sprintf(submit_request,
        "GET /submit/?word=%s&sid=%s HTTP/1.1\r\n"
        "Host: nisl.smu.ac.kr\r\n"
        "Connection: close\r\n"
        "User-Agent: comnet/1.0\r\n\r\n",
        word, sid);

    // 정답 전송 및 응답 수신
    if (send(sock, submit_request, strlen(submit_request), 0) < 0) {
        perror("정답 제출 실패");
    } else {
        shutdown(sock, SHUT_WR);
        char buffer[BUFFER_SIZE * 4] = {0};
        int total = 0, rlen;
        while ((rlen = recv(sock, buffer + total, BUFFER_SIZE * 4 - 1 - total, 0)) > 0) {
            total += rlen;
        }
        buffer[total] = '\0';

        const char* json_start = strchr(buffer, '{');
        if (json_start) {
            printf("%s\n", json_start);
        } else {
            printf("JSON이 포함되어 있지 않음.\n");
        }
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

    // 소켓 초기화 및 서버 연결
    int sock = initialize_socket(host, &serv_addr);
    if (sock < 0) return -1;

    // 서버와 통신
    // 단어에 들어있는 알파뱃 받아오는 단계 1
    while (1) {
        if (send_request_and_receive_response(sock, request, buffer) < 0) break;
        int result = parse_json_response(buffer);
        if (result == -2) break;  // 중복 감지 시 탈출
        if (result < 0) {
            fprintf(stderr, "응답 파싱 실패\n");
            break;
        }
    }

    // 복호화 및 정답 제출
    decode_and_submit(sock);
    free_resources();
    close(sock);
    return 0;
}
