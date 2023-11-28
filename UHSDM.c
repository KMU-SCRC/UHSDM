#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>

#define MAX_PORT_NAME 20
#define INI_FILE_NAME "SETTINGS.INI"

typedef struct {
    char portName[MAX_PORT_NAME];
    int baudRate;
    int byteSize;
    int stopBits;
    int parity;
    HANDLE hSerial;
} ModemConfig;

ModemConfig acousticModem = { "COM4", CBR_115200, 8, ONESTOPBIT, NOPARITY, INVALID_HANDLE_VALUE };
ModemConfig lightModem = { "COM6", CBR_115200, 8, ONESTOPBIT, NOPARITY, INVALID_HANDLE_VALUE };

typedef struct {
    ModemConfig acousticModem;
    ModemConfig lightModem;
} ModemSettings;

volatile bool keepRunning = true;

void GetIniFilePath(char* iniFilePath);
void CreateDefaultSettingsIfNotExists();
void LoadSettings(ModemConfig* modem, const char* modemName);
void ReadFullSettings(ModemSettings* settings);
bool IsValidBaudRate(int baudRate);
void ValidateModemConfig(ModemConfig* modem, const char* modemName);
void WriteFullSettings(const ModemSettings* settings);
void SaveSettings(const ModemConfig* modem, const char* modemName);
void UpdateModemSettings(ModemConfig* modem, const char* modemName);
void SendMessageToModem(const ModemConfig* modem);
void DisplayHelp();
void OpenSerialPort(ModemConfig* modem);
void CloseSerialPort(HANDLE* hSerial);
void DisplayMenu();
void HandleUserInput();
DWORD WINAPI ReadThread(LPVOID param);
void SignalHandler(int signal);

int main() {
    signal(SIGINT, SignalHandler);

    CreateDefaultSettingsIfNotExists();

    LoadSettings(&acousticModem, "AcousticModem");
    LoadSettings(&lightModem, "LightModem");

    OpenSerialPort(&acousticModem);
    OpenSerialPort(&lightModem);

    HANDLE readThreadAcoustic = CreateThread(NULL, 0, ReadThread, &acousticModem, 0, NULL);
    HANDLE readThreadLight = CreateThread(NULL, 0, ReadThread, &lightModem, 0, NULL);

    while (keepRunning) {
        DisplayMenu();
        HandleUserInput();
    }

    WaitForSingleObject(readThreadAcoustic, INFINITE);
    WaitForSingleObject(readThreadLight, INFINITE);

    CloseSerialPort(&acousticModem.hSerial);
    CloseSerialPort(&lightModem.hSerial);

    return 0;
}

void GetIniFilePath(char* iniFilePath) {
    GetModuleFileName(NULL, iniFilePath, MAX_PATH);
    char* lastBackslash = strrchr(iniFilePath, '\\');
    if (lastBackslash) {
        *(lastBackslash + 1) = '\0'; // 경로의 마지막에 파일 이름을 제거
    }

    // 문자열 끝이 'C'인 경우 제거
    size_t length = strlen(iniFilePath);
    if (length > 0 && iniFilePath[length - 1] == 'C') {
        iniFilePath[length - 1] = '\0';
    }

    strcat(iniFilePath, INI_FILE_NAME);
    //printf("INI File Path: %s\n", iniFilePath); // 경로 출력
}

void CreateDefaultSettingsIfNotExists() {
    char iniFilePath[MAX_PATH];
    GetIniFilePath(iniFilePath);

    FILE* file = fopen(iniFilePath, "r");
    if (!file) {
        // 파일이 없으면 디폴트 설정으로 생성
        file = fopen(iniFilePath, "w");
        if (file) {
            fprintf(file, "[AcousticModem]\n");
            fprintf(file, "Port=COM4\nBaudRate=115200\nByteSize=8\nStopBits=1\nParity=0\n");
            fprintf(file, "[LightModem]\n");
            fprintf(file, "Port=COM6\nBaudRate=115200\nByteSize=8\nStopBits=1\nParity=0\n");
            fclose(file);
        }
    }
    else {
        fclose(file);
    }
}

void LoadSettings(ModemConfig* modem, const char* modemName) {
    char iniFilePath[MAX_PATH];
    GetIniFilePath(iniFilePath);

    FILE* file = fopen(iniFilePath, "r");
    bool settingsChanged = false;  // 설정이 변경되었는지 추적
    if (file != NULL) {
        char sectionName[50];
        sprintf(sectionName, "[%s]", modemName);
        char line[100];
        bool foundSection = false;
        bool settings[5] = { false }; // Port, BaudRate, ByteSize, StopBits, Parity

        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, sectionName)) {
                foundSection = true;
                continue;
            }

            if (foundSection) {
                if (strstr(line, "Port=") && !settings[0]) {
                    sscanf(line, "Port=%19s", modem->portName);
                    settings[0] = true;
                }
                else if (strstr(line, "BaudRate=") && !settings[1]) {
                    int baudRate;
                    sscanf(line, "BaudRate=%d", &baudRate);
                    if (IsValidBaudRate(baudRate)) {
                        modem->baudRate = baudRate;
                    }
                    else {
                        modem->baudRate = CBR_115200; // 기본값
                    }
                    settings[1] = true;
                }
                else if (strstr(line, "ByteSize=") && !settings[2]) {
                    int byteSize;
                    sscanf(line, "ByteSize=%d", &byteSize);
                    modem->byteSize = byteSize > 0 ? byteSize : 8; // 기본값
                    settings[2] = true;
                }
                else if (strstr(line, "StopBits=") && !settings[3]) {
                    int stopBits;
                    sscanf(line, "StopBits=%d", &stopBits);
                    modem->stopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS; // 기본값
                    settings[3] = true;
                }
                else if (strstr(line, "Parity=") && !settings[4]) {
                    int parity;
                    sscanf(line, "Parity=%d", &parity);
                    modem->parity = (parity == 1 || parity == 2) ? parity : NOPARITY; // 기본값
                    settings[4] = true;
                }
            }
        }
        fclose(file);

        // 누락된 설정을 기본값으로 채움
        if (!settings[0]){ 
            strcpy(modem->portName, modemName[0] == 'A' ? "COM4" : "COM6"); // 기본 포트
            settingsChanged = true;
        }
        if (!settings[1]){ 
            modem->baudRate = CBR_115200; // 기본 보레이트
            settingsChanged = true;
        }
        if (!settings[2]){ 
            modem->byteSize = 8; // 기본 바이트 크기
            settingsChanged = true;
        }
        if (!settings[3]){ 
            modem->stopBits = ONESTOPBIT; // 기본 스톱 비트
            settingsChanged = true;
        }
        if (!settings[4]){ 
            modem->parity = NOPARITY; // 기본 패리티
            settingsChanged = true;
        }

        // 변경된 설정이 있으면 INI 파일 업데이트
        if (settingsChanged) {
            SaveSettings(modem, modemName);
        }
    }
    else {
        // 파일이 없을 경우 기본값 설정
        strcpy(modem->portName, modemName[0] == 'A' ? "COM4" : "COM6");
        modem->baudRate = CBR_115200;
        modem->byteSize = 8;
        modem->stopBits = ONESTOPBIT;
        modem->parity = NOPARITY;
        SaveSettings(modem, modemName);  // 변경된 설정을 INI 파일에 저장
    }
}

void ReadFullSettings(ModemSettings* settings) {
    // 기존 설정을 읽어오는 함수
    // LoadSettings 함수를 활용하여 각 모뎀 설정을 읽어옵니다.
    LoadSettings(&settings->acousticModem, "AcousticModem");
    LoadSettings(&settings->lightModem, "LightModem");
}

bool IsValidBaudRate(int baudRate) {
    // 유효한 보레이트 값들을 확인하는 함수
    // 예: 9600, 19200, 38400, 57600, 115200 등
    switch (baudRate) {
    case CBR_9600:
    case CBR_19200:
    case CBR_38400:
    case CBR_57600:
    case CBR_115200:
        return true;
    default:
        return false;
    }
}

// 각 설정을 검증하고 필요한 경우 디폴트 값으로 설정하는 함수
void ValidateModemConfig(ModemConfig* modem, const char* modemName) {
    // 포트 설정 검증
    if (strlen(modem->portName) == 0 || strstr(modem->portName, "COM") == NULL) {
        strcpy(modem->portName, strcmp(modemName, "AcousticModem") == 0 ? "COM4" : "COM6");
    }

    // 보레이트 설정 검증
    if (!IsValidBaudRate(modem->baudRate)) {
        modem->baudRate = CBR_115200;
    }

    // 나머지 설정에 대한 검증 및 디폴트 값 설정
    modem->byteSize = modem->byteSize > 0 ? modem->byteSize : 8;
    modem->stopBits = modem->stopBits == ONESTOPBIT || modem->stopBits == TWOSTOPBITS ? modem->stopBits : ONESTOPBIT;
    modem->parity = modem->parity == NOPARITY || modem->parity == ODDPARITY || modem->parity == EVENPARITY ? modem->parity : NOPARITY;
}

void WriteFullSettings(const ModemSettings* settings) {
    // 전체 설정을 파일에 쓰는 함수
    char iniFilePath[MAX_PATH];
    GetIniFilePath(iniFilePath);

    FILE* file = fopen(iniFilePath, "w");
    if (file != NULL) {
        // AcousticModem 설정 작성
        fprintf(file, "[AcousticModem]\n");
        fprintf(file, "Port=%s\n", settings->acousticModem.portName);
        fprintf(file, "BaudRate=%d\n", settings->acousticModem.baudRate);
        fprintf(file, "ByteSize=%d\n", settings->acousticModem.byteSize);
        fprintf(file, "StopBits=%d\n", settings->acousticModem.stopBits);
        fprintf(file, "Parity=%d\n", settings->acousticModem.parity);

        // LightModem 설정 작성
        fprintf(file, "[LightModem]\n");
        fprintf(file, "Port=%s\n", settings->lightModem.portName);
        fprintf(file, "BaudRate=%d\n", settings->lightModem.baudRate);
        fprintf(file, "ByteSize=%d\n", settings->lightModem.byteSize);
        fprintf(file, "StopBits=%d\n", settings->lightModem.stopBits);
        fprintf(file, "Parity=%d\n", settings->lightModem.parity);

        fclose(file);
    }
}

void SaveSettings(const ModemConfig* modem, const char* modemName) {
    // 전체 설정을 읽어온 후 특정 모뎀 설정을 업데이트하고, 다시 파일에 씁니다.
    ModemSettings settings;
    ReadFullSettings(&settings);

    // 특정 모뎀 설정 업데이트
    if (strcmp(modemName, "AcousticModem") == 0) {
        settings.acousticModem = *modem;
    }
    else if (strcmp(modemName, "LightModem") == 0) {
        settings.lightModem = *modem;
    }

    // 설정 검증 및 필요한 경우 디폴트 값으로 설정
    ValidateModemConfig(&settings.acousticModem, "AcousticModem");
    ValidateModemConfig(&settings.lightModem, "LightModem");

    WriteFullSettings(&settings);
}

void UpdateModemSettings(ModemConfig* modem, const char* modemName) {
    printf("Update settings for %s\n", modemName);

    ModemConfig newModemConfig = *modem;

    printf("Enter COM port name (e.g., COM3): ");
    scanf("%19s", newModemConfig.portName);

    printf("Enter baud rate (e.g., 9600): ");
    scanf("%d", &newModemConfig.baudRate);

    printf("Enter byte size (e.g., 8): ");
    scanf("%d", &newModemConfig.byteSize);

    printf("Enter stop bits (1 = One, 2 = Two): ");
    scanf("%d", &newModemConfig.stopBits);

    int parityInput;
    printf("Enter parity (0 = None, 1 = Odd, 2 = Even): ");
    scanf("%d", &parityInput);
    newModemConfig.parity = parityInput == 1 ? ODDPARITY : parityInput == 2 ? EVENPARITY : NOPARITY;

    // 새로운 설정으로 모뎀 열기 시도
    newModemConfig.hSerial = CreateFile(newModemConfig.portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (newModemConfig.hSerial != INVALID_HANDLE_VALUE) {
        // OPEN에 성공한 경우에만 기존 모뎀 연결을 닫고 새로운 설정 적용
        // 기존 모뎀 연결을 먼저 닫음
        CloseSerialPort(&modem->hSerial);
        // OPEN에 성공한 경우에만 설정 변경
        *modem = newModemConfig;
        SetCommState(modem->hSerial, &modem->baudRate, &modem->byteSize, &modem->stopBits, &modem->parity);
        SaveSettings(modem, modemName);
    }
    else {
        printf("Failed to open modem with new settings.\n");
    }
}

void SendMessageToModem(const ModemConfig* modem) {
    if (modem->hSerial == INVALID_HANDLE_VALUE) {
        printf("Modem is not connected.\n");
        return;
    }

    char message[30];
    printf("Enter message (up to 8 characters): ");
    scanf("%s", message);
    DWORD bytesWritten;
    if (!WriteFile(modem->hSerial, message, strlen(message), &bytesWritten, NULL)) {
        printf("Failed to send message.\n");
    }
    else {
        printf("Message sent.\n");
    }
}

void DisplayHelp() {
    printf("Help:\n");
    printf("1. Acoustic Modem Settings - Update settings for the Acoustic Modem.\n");
    printf("2. Light Modem Settings - Update settings for the Light Modem.\n");
    printf("3. Send a message to an Acoustic Modem - Send a message through the Acoustic Modem.\n");
    printf("4. Send a message to a Light Modem - Send a message through the Light Modem.\n");
    printf("5. Help - Display this help message.\n");
    printf("6. Exit - Exit the program.\n");
}

void OpenSerialPort(ModemConfig* modem) {
    modem->hSerial = CreateFile(modem->portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (modem->hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error opening serial port %s\n", modem->portName);
        return;
    }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(modem->hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Error getting comm state for %s\n", modem->portName);
        CloseSerialPort(&modem->hSerial);
        return;
    }

    dcbSerialParams.BaudRate = modem->baudRate;
    dcbSerialParams.ByteSize = modem->byteSize;
    dcbSerialParams.StopBits = modem->stopBits;
    dcbSerialParams.Parity = modem->parity;

    if (!SetCommState(modem->hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Error setting comm state for %s\n", modem->portName);
        CloseSerialPort(&modem->hSerial);
        return;
    }
}

void CloseSerialPort(HANDLE* hSerial) {
    if (*hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(*hSerial);
        *hSerial = INVALID_HANDLE_VALUE;
    }
}

void DisplayMenu() {
    printf("\n============\n");
    printf("Acoustic Modem : %s\n", acousticModem.hSerial != INVALID_HANDLE_VALUE ? "ON" : "OFF");
    printf("Light Modem : %s\n", lightModem.hSerial != INVALID_HANDLE_VALUE ? "ON" : "OFF");
    printf("1. Acoustic Modem Settings\n");
    printf("2. Light Modem Settings\n");
    printf("3. Send a message to an Acoustic Modem\n");
    printf("4. Send a message to a Light Modem\n");
    printf("5. Help\n");
    printf("6. Exit\n");
    printf("============\n");
}

void HandleUserInput() {
    int choice;
    printf("Enter your choice: ");
    scanf("%d", &choice);

    switch (choice) {
    case 1:
        UpdateModemSettings(&acousticModem, "AcousticModem");
        break;
    case 2:
        UpdateModemSettings(&lightModem, "LightModem");
        break;
    case 3:
        SendMessageToModem(&acousticModem);
        break;
    case 4:
        SendMessageToModem(&lightModem);
        break;
    case 5:
        DisplayHelp();
        break;
    case 6:
        keepRunning = false;
        break;
    default:
        printf("Please enter a number between 1 and 6.\n");
        getchar();
        break;
    }
}

DWORD WINAPI ReadThread(LPVOID param) {
    ModemConfig* modem = (ModemConfig*)param;
    char buffer[9];
    DWORD bytesRead;

    while (keepRunning) {
        if (ReadFile(modem->hSerial, buffer, 8, &bytesRead, NULL)) {
            buffer[bytesRead] = '\0';
            printf("Received Message(%s) >> %s\n", modem->portName, buffer);
        }
    }

    return 0;
}

void SignalHandler(int signal) {
    if (signal == SIGINT) {
        keepRunning = false;
    }
}
