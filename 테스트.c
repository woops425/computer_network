#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 80
#define BUFFER_SIZE 1024
#define MAX_PACKETS 25

typedef struct PacketResponse {
    int sid;        // 학생 ID
    int seq;        // 패킷 시퀀스 번호 (ex, 1, 2, 3) 1~단어길이
    char* payload;  // 패킷 데이터 (ex, "01000001")
} Packet;

static Packet res[MAX_PACKETS];
static int idx = 0;     // 패킷 응답 인덱스
static char* sid = "202010629"; // 학생 ID
static int parity_pos = 0;  // 사용자가 지정하는 parity 위치

int already_exists(int seq) {
    for (int i = 0; i < idx; i++) {
        if (res[i].seq == seq) return 1;
    }
    return 0;
}

// JSON 응답을 파싱하는 함수
int parse_json_response(const char* response) {
    const char* json_start = strchr(response, '{');
    if (!json_start)  {
        fprintf(stderr, "JSON 시작 부분을 찾지 못했습니다.\n");
        return -1;
    }

    int student_id = 0;
    int seq = 0;
    char payload[100] = { 0 };

    // JSON 파싱
    if (sscanf(json_start, "{\"sid\": %d, \"seq\": %d, \"data\": \"%[^\"]\"}", &student_id, &seq, payload) != 3) {
        fprintf(stderr, "JSON 파싱 실패\n");
        return -1;
    }

    if (already_exists(seq)) return seq + 1;

    printf("JSON 응답: {\"sid\": %d, \"seq\": %d, \"data\": \"%s\"}\n", student_id, seq, payload);

    // 메모리 할당 및 데이터 저장
    res[idx].sid = student_id;
    res[idx].seq = seq;
    res[idx].payload = strdup(payload);    // 문자열 복사
    if (!res[idx].payload) return -1;

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

// TODO: 서버에 요청을 보내고 응답을 받는 함수  - 구현해야함(들어오는 데이터를 읽어서 버퍼로 바꿔주는 역할)
int send_request_and_receive_response(int sock, const char* request, char* buffer) {
    // 요청 보내기
    if (send(sock, request, strlen(request), 0) < 0) {
        perror("요청 전송 실패");
        return -1;
    }

    // int received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    // if (received <= 0) {
    //     perror("응답 수신 실패");
    //     return -1;
    // }

    // buffer[received] = '\0';
    // static int response_printed = 0;
    // if (!response_printed) {
    //     printf("서버 응답:\n%s\n", buffer);
    //     response_printed = 1;
    // }


    // 응답 받기 (버퍼 초기화 후 읽기)
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_received = read(sock, buffer, BUFFER_SIZE - 1);  // null terminator 위해 -1
    if (bytes_received < 0) {
        perror("응답 수신 실패");
        return -1;
    }

    buffer[bytes_received] = '\0';  // 안전한 문자열 종료
    return 0;
    return 0;
}

// 리소스를 해제하는 함수
void free_resources() {
    for (int i = 0; i < idx; i++) {
        free(res[i].payload);
    }
}

// 단어 디코딩 함수
void decode_and_print_word() {
    for (int i = 0; i < idx - 1; i++) {
        for (int j = i + 1; j < idx; j++) {
            if (res[i].seq > res[j].seq) {
                Packet tmp = res[i];
                res[i] = res[j];
                res[j] = tmp;
            }
        }
    }

    printf("지정된 parity bit index: %d\n", parity_pos);

    printf("복원된 단어: ");
    for (int i = 0; i < idx; i++) {
        char* bin = res[i].payload;
        char data_bits[8] = {0};
        int k = 0;
        for (int j = 0; j < 8; j++) {
            if (j == parity_pos) continue;
            data_bits[k++] = bin[j];
        }
        char ascii = (char)strtol(data_bits, NULL, 2);
        printf("%c", ascii);
    }
    printf("\n");
}

int main(int argc, char const* argv[]) {
    const char* host = "nisl.smu.ac.kr";
    char request[BUFFER_SIZE];

    // stream 요청
    sprintf(request,
        "GET /%s/ HTTP/1.1\r\n"
        "Host: nisl.smu.ac.kr\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: comnet/1.0\r\n\r\n",
        sid);

    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = { 0 };

    // 소켓 초기화 및 서버 연결 
    int sock = initialize_socket(host, &serv_addr);
    if (sock < 0) return -1;

    while (1) {
        if (send_request_and_receive_response(sock, request, buffer) < 0) break;

        
        int next = parse_json_response(buffer);
        if (next < 0 || idx >= MAX_PACKETS) break;
    }

    printf("parity bit 위치 (0~7)를 입력하세요: ");
    scanf("%d", &parity_pos);

    decode_and_print_word();

    char answer[100];
    printf("제출할 단어를 입력하세요: ");
    scanf("%s", answer);

    char submit_request[BUFFER_SIZE];
    // 정답 제출
    sprintf(submit_request,
        "GET /submit/?word=%s&sid=%s HTTP/1.1\r\n"
        "Host: nisl.smu.ac.kr\r\n"
        "Connection: close\r\n"
        "User-Agent: comnet/1.0\r\n\r\n",
        answer, sid);

    if (send(sock, submit_request, strlen(submit_request), 0) < 0) {
        perror("정답 제출 실패");
    } else {
        int rlen = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (rlen > 0) {
            buffer[rlen] = '\0';
            printf("서버 응답:\n%s\n", buffer);
        }
    }

    free_resources();
    close(sock);

    return 0;
}   
