#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <tchar.h>
#include <ntddser.h>

#define MAX_PORT_NAME 20
#define INI_FILE_NAME TEXT("SETTINGS.INI")

typedef struct {
    TCHAR portName[MAX_PORT_NAME];
    int baudRate;
    int byteSize;
    int stopBits;
    int parity;
    HANDLE hSerial;
} ModemConfig;

ModemConfig acousticModem = { TEXT("COM4"), CBR_115200, 8, ONESTOPBIT, NOPARITY, INVALID_HANDLE_VALUE };
ModemConfig lightModem = { TEXT("COM6"), CBR_115200, 8, ONESTOPBIT, NOPARITY, INVALID_HANDLE_VALUE };

typedef struct {
    ModemConfig acousticModem;
    ModemConfig lightModem;
} ModemSettings;

volatile bool keepRunning = true;
void FlushStdInBuffer();
void GetIniFilePath(TCHAR* iniFilePath);
void CreateDefaultSettingsIfNotExists();
void LoadSettings(ModemConfig* modem, const TCHAR* modemName);
void ReadFullSettings(ModemSettings* settings);
bool IsValidBaudRate(int baudRate);
void ValidateModemConfig(ModemConfig* modem, const TCHAR* modemName);
void WriteFullSettings(const ModemSettings* settings);
void SaveSettings(const ModemConfig* modem, const TCHAR* modemName);
void ListSerialPorts();
void UpdateModemSettings(ModemConfig* modem, const TCHAR* modemName);
void SendMessageToModem(const ModemConfig* modem);
void DisplayHelp();
bool OpenSerialPort(ModemConfig* modem);
void CloseSerialPort(HANDLE* hSerial);
void DisplayMenu();
void HandleUserInput();
DWORD WINAPI ReadThread(LPVOID param);
void SignalHandler(int signal);

int main() {
    signal(SIGINT, SignalHandler);
    CreateDefaultSettingsIfNotExists();
    LoadSettings(&acousticModem, TEXT("AcousticModem"));
    LoadSettings(&lightModem, TEXT("LightModem"));
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

// 표준 입력 버퍼를 비우는 함수
void FlushStdInBuffer() {
    int c;
    while ((c = _gettchar()) != '\n' && c != EOF) {
        // 버퍼에서 남아 있는 문자를 계속 읽어서 무시
    }
}

void GetIniFilePath(TCHAR* iniFilePath) {
    GetModuleFileName(NULL, iniFilePath, MAX_PATH);
    TCHAR* lastBackslash = _tcsrchr(iniFilePath, TEXT('\\'));
    if (lastBackslash) {
        *(lastBackslash + 1) = TEXT('\0'); // 경로의 마지막에 파일 이름을 제거
    }

    // 문자열 끝이 'C'인 경우 제거
    size_t length = _tcslen(iniFilePath);
    if (length > 0 && iniFilePath[length - 1] == TEXT('C')) {
        iniFilePath[length - 1] = TEXT('\0');
    }

    _tcscat(iniFilePath, INI_FILE_NAME);
}

void CreateDefaultSettingsIfNotExists() {
    TCHAR iniFilePath[MAX_PATH];
    GetIniFilePath(iniFilePath);

    FILE* file = _tfopen(iniFilePath, TEXT("r"));
    if (!file) {
        // 파일이 없으면 디폴트 설정으로 생성
        file = _tfopen(iniFilePath, TEXT("w"));
        if (file) {
            _ftprintf(file, TEXT("[AcousticModem]\n"));
            _ftprintf(file, TEXT("Port=COM4\nBaudRate=115200\nByteSize=8\nStopBits=1\nParity=0\n"));
            _ftprintf(file, TEXT("[LightModem]\n"));
            _ftprintf(file, TEXT("Port=COM6\nBaudRate=115200\nByteSize=8\nStopBits=1\nParity=0\n"));
            fclose(file);
        }
    }
    else {
        fclose(file);
    }
}

void LoadSettings(ModemConfig* modem, const TCHAR* modemName) {
    TCHAR iniFilePath[MAX_PATH];
    GetIniFilePath(iniFilePath);

    FILE* file = _tfopen(iniFilePath, TEXT("r"));
    bool settingsChanged = false;  // 설정이 변경되었는지 추적
    if (file != NULL) {
        TCHAR sectionName[50];
        _stprintf(sectionName, TEXT("[%s]"), modemName);
        TCHAR line[100];
        bool foundSection = false;
        bool settings[5] = { false }; // Port, BaudRate, ByteSize, StopBits, Parity

        while (_fgetts(line, sizeof(line), file)) {
            if (_tcsstr(line, sectionName)) {
                foundSection = true;
                continue;
            }

            if (foundSection) {
                if (_tcsstr(line, TEXT("Port=")) && !settings[0]) {
                    _stscanf(line, TEXT("Port=%19s"), modem->portName);
                    settings[0] = true;
                }
                else if (_tcsstr(line, TEXT("BaudRate=")) && !settings[1]) {
                    int baudRate;
                    _stscanf(line, TEXT("BaudRate=%d"), &baudRate);
                    if (IsValidBaudRate(baudRate)) {
                        modem->baudRate = baudRate;
                    }
                    else {
                        modem->baudRate = CBR_115200; // 기본값
                    }
                    settings[1] = true;
                }
                else if (_tcsstr(line, TEXT("ByteSize=")) && !settings[2]) {
                    int byteSize;
                    _stscanf(line, TEXT("ByteSize=%d"), &byteSize);
                    modem->byteSize = byteSize > 0 ? byteSize : 8; // 기본값
                    settings[2] = true;
                }
                else if (_tcsstr(line, TEXT("StopBits=")) && !settings[3]) {
                    int stopBits;
                    _stscanf(line, TEXT("StopBits=%d"), &stopBits);
                    modem->stopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS; // 기본값
                    settings[3] = true;
                }
                else if (_tcsstr(line, TEXT("Parity=")) && !settings[4]) {
                    int parity;
                    _stscanf(line, TEXT("Parity=%d"), &parity);
                    modem->parity = (parity == 1 || parity == 2) ? parity : NOPARITY; // 기본값
                    settings[4] = true;
                }
            }
        }
        fclose(file);

        // 누락된 설정을 기본값으로 채움
        if (!settings[0]){ 
            _tcscpy(modem->portName, modemName[0] == TEXT('A') ? TEXT("COM4") : TEXT("COM6")); // 기본 포트
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
        _tcscpy(modem->portName, modemName[0] == TEXT('A') ? TEXT("COM4") : TEXT("COM6"));
        modem->baudRate = CBR_115200;
        modem->byteSize = 8;
        modem->stopBits = ONESTOPBIT;
        modem->parity = NOPARITY;
        SaveSettings(modem, modemName);  // 변경된 설정을 INI 파일에 저장
    }
}

    // 기존 설정을 읽어오는 함수
void ReadFullSettings(ModemSettings* settings) {
    // LoadSettings 함수를 활용하여 각 모뎀 설정을 읽음
    LoadSettings(&settings->acousticModem, TEXT("AcousticModem"));
    LoadSettings(&settings->lightModem, TEXT("LightModem"));
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
void ValidateModemConfig(ModemConfig* modem, const TCHAR* modemName) {
    // 포트 설정 검증
    if (_tcslen(modem->portName) == 0 || _tcsstr(modem->portName, TEXT("COM")) == NULL) {
        _tcscpy(modem->portName, _tcscmp(modemName, TEXT("AcousticModem")) == 0 ? TEXT("COM4") : TEXT("COM6"));
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
    TCHAR iniFilePath[MAX_PATH];
    GetIniFilePath(iniFilePath);

    FILE* file = _tfopen(iniFilePath, TEXT("w"));
    if (file != NULL) {
        // AcousticModem 설정 작성
        _ftprintf(file, TEXT("[AcousticModem]\n"));
        _ftprintf(file, TEXT("Port=%s\n"), settings->acousticModem.portName);
        _ftprintf(file, TEXT("BaudRate=%d\n"), settings->acousticModem.baudRate);
        _ftprintf(file, TEXT("ByteSize=%d\n"), settings->acousticModem.byteSize);
        _ftprintf(file, TEXT("StopBits=%d\n"), settings->acousticModem.stopBits);
        _ftprintf(file, TEXT("Parity=%d\n"), settings->acousticModem.parity);

        // LightModem 설정 작성
        _ftprintf(file, TEXT("[LightModem]\n"));
        _ftprintf(file, TEXT("Port=%s\n"), settings->lightModem.portName);
        _ftprintf(file, TEXT("BaudRate=%d\n"), settings->lightModem.baudRate);
        _ftprintf(file, TEXT("ByteSize=%d\n"), settings->lightModem.byteSize);
        _ftprintf(file, TEXT("StopBits=%d\n"), settings->lightModem.stopBits);
        _ftprintf(file, TEXT("Parity=%d\n"), settings->lightModem.parity);

        fclose(file);
    }
}

void SaveSettings(const ModemConfig* modem, const TCHAR* modemName) {
    // 전체 설정을 읽어온 후 특정 모뎀 설정을 업데이트하고, 다시 파일에 씀
    ModemSettings settings;
    ReadFullSettings(&settings);

    // 특정 모뎀 설정 업데이트
    if (_tcscmp(modemName, TEXT("AcousticModem")) == 0) {
        settings.acousticModem = *modem;
    }
    else if (_tcscmp(modemName, TEXT("LightModem")) == 0) {
        settings.lightModem = *modem;
    }

    // 설정 검증 및 필요한 경우 디폴트 값으로 설정
    ValidateModemConfig(&settings.acousticModem, TEXT("AcousticModem"));
    ValidateModemConfig(&settings.lightModem, TEXT("LightModem"));

    WriteFullSettings(&settings);
}

void ListSerialPorts() {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        _tprintf(TEXT("Failed to get device information set.\n"));
        return;
    }
    _tprintf(TEXT("Available serial ports:\n"));
    int portCount = 0;
    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        DWORD dataSize;
        TCHAR deviceName[256] = { 0 }; // 배열 초기화
        DWORD regDataType;

        // 장치 이름 가져오기
        if (SetupDiGetDeviceRegistryProperty(deviceInfoSet, &deviceInfoData, SPDRP_FRIENDLYNAME, &regDataType, (PBYTE)deviceName, sizeof(deviceName) - 1, &dataSize) && regDataType == REG_SZ) {
            _tprintf(TEXT("  Device Name: %s\n"), deviceName);
            portCount++;
        }
        else {
            _tprintf(TEXT("  Error getting device name: %ld\n"), GetLastError());
        }
    }

    // 시리얼 포트가 없는 경우 메시지 출력
    if (portCount == 0) {
        _tprintf(TEXT("  No serial ports available.\n"));
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
}

void UpdateModemSettings(ModemConfig* modem, const TCHAR* modemName) {

    // 사용 가능한 시리얼 포트 나열
    ListSerialPorts();

    _tprintf(TEXT("Update settings for %s\n"), modemName);

    ModemConfig newModemConfig = { TEXT("COM6"), CBR_115200, 8, ONESTOPBIT, NOPARITY, INVALID_HANDLE_VALUE };

    _tprintf(TEXT("Enter COM port name (e.g., COM6): \n"));
    _tscanf(TEXT("%s"), newModemConfig.portName);
    FlushStdInBuffer();

    _tprintf(TEXT("Enter baud rate (e.g., 115200): \n"));
    int baudRate = 0;
    _tscanf(TEXT("%d"), &baudRate);
    FlushStdInBuffer();
    if (IsValidBaudRate(baudRate)) {
        newModemConfig.baudRate = baudRate;
    }
    else {
        newModemConfig.baudRate = CBR_115200; // 기본값
    }

    _tprintf(TEXT("Enter byte size (e.g., 8): \n"));
    int byteSize = 0;
    _tscanf(TEXT("%d"), &byteSize);
    FlushStdInBuffer();
    newModemConfig.byteSize = byteSize > 0 ? byteSize : 8; // 기본값

    _tprintf(TEXT("Enter stop bits (1 = One, 2 = Two): \n"));
    int stopBits = 0;
    _tscanf(TEXT("%d"), &stopBits);
    FlushStdInBuffer();
    newModemConfig.stopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS; // 기본값

    _tprintf(TEXT("Enter parity (0 = None, 1 = Odd, 2 = Even): \n"));
    int parityInput = 0;
    _tscanf(TEXT("%d"), &parityInput);
    FlushStdInBuffer();
    newModemConfig.parity = parityInput == 1 ? ODDPARITY : parityInput == 2 ? EVENPARITY : NOPARITY;

    // 새로운 설정으로 모뎀 열기 시도
    if (OpenSerialPort(&newModemConfig)) {
        // OPEN에 성공한 경우에만 기존 모뎀 연결을 닫고 새로운 설정 적용
        CloseSerialPort(&modem->hSerial);
        *modem = newModemConfig;
        // OPEN에 성공한 경우에만 설정 변경
        SaveSettings(modem, modemName);
    }
    else {
        _tprintf(TEXT("Failed to open modem with new settings.\n"));
    }
}

void SendMessageToModem(const ModemConfig* modem) {
    if (modem->hSerial == INVALID_HANDLE_VALUE) {
        _tprintf(TEXT("Modem is not connected.\n"));
        return;
    }

    TCHAR hexString[60];
    _tprintf(TEXT("Enter HEX message: \n"));
    _tscanf(TEXT("%[^\n]s"), hexString);  // 전체 라인 읽기
    FlushStdInBuffer();
    // 입력받은 HEX 문자열을 바이트 배열로 변환
    BYTE byteArray[30]; // 최대 30 바이트
    int byteArrayIndex = 0;
    TCHAR hexPair[3] = { 0 }; // HEX 쌍을 저장할 배열
    int hexPairIndex = 0;

    for (int i = 0; hexString[i] != '\0' && i < 59 && byteArrayIndex < 30; ++i) {
        if (hexString[i] != ' ' && hexString[i] != '\n' && hexString[i] != '\r') {
            hexPair[hexPairIndex++] = hexString[i];
            if (hexPairIndex == 2) {
                hexPair[hexPairIndex] = '\0'; // 널 문자로 종료
                byteArray[byteArrayIndex++] = (BYTE)_tcstoul(hexPair, NULL, 16);
                hexPairIndex = 0; // HEX 쌍 인덱스 초기화
            }
        }
    }

    DWORD bytesWritten;
    if (!WriteFile(modem->hSerial, byteArray, byteArrayIndex, &bytesWritten, NULL)) {
        _tprintf(TEXT("Failed to send message.\n"));
    }
    else {
        _tprintf(TEXT("Message sent.\n"));
    }
}

void DisplayHelp() {
    _tprintf(TEXT("Help:\n"));
    _tprintf(TEXT("1. Acoustic Modem Settings - Update settings for the Acoustic Modem.\n"));
    _tprintf(TEXT("2. Light Modem Settings - Update settings for the Light Modem.\n"));
    _tprintf(TEXT("3. Send a message to an Acoustic Modem - Send a message through the Acoustic Modem.\n"));
    _tprintf(TEXT("4. Send a message to a Light Modem - Send a message through the Light Modem.\n"));
    _tprintf(TEXT("5. Help - Display this help message.\n"));
    _tprintf(TEXT("6. Exit - Exit the program.\n"));
}

bool OpenSerialPort(ModemConfig* modem) {
    //시리얼 포트 오픈
    modem->hSerial = CreateFile(modem->portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (modem->hSerial == INVALID_HANDLE_VALUE) {
        _ftprintf(stderr, TEXT("Error opening serial port %s\n"), modem->portName);
        _ftprintf(stderr, TEXT("  Error Code: %d\n"), GetLastError());
        return false;
    }

    // 큐 사이즈 설정
    if (!SetupComm(modem->hSerial, 4096, 4096)) {
        _ftprintf(stderr, TEXT("Error setting queue size for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    //타임 아웃 설정
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 30;
    if (!SetCommTimeouts(modem->hSerial, &timeouts)) {
        _ftprintf(stderr, TEXT("Error setting timeout for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    // 이벤트 마스크 설정 (EV_ERR: 에러 이벤트)
    if (!SetCommMask(modem->hSerial, EV_ERR)) {
        _ftprintf(stderr, TEXT("Error setting event mask for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    //DCB 설정 가져오기
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(modem->hSerial, &dcbSerialParams)) {
        _ftprintf(stderr, TEXT("Error getting comm state for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    //DCB 설정 적용
    dcbSerialParams.BaudRate = modem->baudRate;
    dcbSerialParams.ByteSize = modem->byteSize;
    dcbSerialParams.StopBits = modem->stopBits;
    dcbSerialParams.Parity = modem->parity;
    if (!SetCommState(modem->hSerial, &dcbSerialParams)) {
        _ftprintf(stderr, TEXT("Error setting comm state for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    //디바이스 컨트롤
    DWORD dwBytesReturned = 0;

    SERIAL_CHARS SerialChars = { 0 };
    SerialChars.EofChar = 0x0;
    SerialChars.ErrorChar = 0x0;
    SerialChars.BreakChar = 0x0;
    SerialChars.EventChar = 0x0;
    SerialChars.XonChar = 0x11;
    SerialChars.XoffChar = 0x13;

    if (!DeviceIoControl(modem->hSerial, IOCTL_SERIAL_SET_CHARS, &SerialChars, sizeof(SerialChars), NULL, 0, &dwBytesReturned, NULL)) {
        _ftprintf(stderr, TEXT("Error setting comm SERIAL CHARS for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    SERIAL_HANDFLOW HandFlow = { 0 };
    HandFlow.ControlHandShake = 0x1;
    HandFlow.FlowReplace = 0x40;
    HandFlow.XonLimit = 0;
    HandFlow.XoffLimit = 16384;

    if (!DeviceIoControl(modem->hSerial, IOCTL_SERIAL_SET_HANDFLOW, &HandFlow, sizeof(HandFlow), NULL, 0, &dwBytesReturned, NULL)) {
        _ftprintf(stderr, TEXT("Error setting comm SERIAL HANDFLOW for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    // 퍼지 커맨드 실행
    if (!PurgeComm(modem->hSerial, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR)) {
        _ftprintf(stderr, TEXT("Error purging comm ports for %s\n"), modem->portName);
        CloseSerialPort(&modem->hSerial);
        return false;
    }

    return true;
}

void CloseSerialPort(HANDLE* hSerial) {
    if (*hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(*hSerial);
        *hSerial = INVALID_HANDLE_VALUE;
    }
}

void DisplayMenu() {
    _tprintf(TEXT("\n============\n"));
    _tprintf(TEXT("Acoustic Modem : %s\n"), acousticModem.hSerial != INVALID_HANDLE_VALUE ? TEXT("ON") : TEXT("OFF"));
    _tprintf(TEXT("Light Modem : %s\n"), lightModem.hSerial != INVALID_HANDLE_VALUE ? TEXT("ON") : TEXT("OFF"));
    _tprintf(TEXT("1. Acoustic Modem Settings\n"));
    _tprintf(TEXT("2. Light Modem Settings\n"));
    _tprintf(TEXT("3. Send a message to an Acoustic Modem\n"));
    _tprintf(TEXT("4. Send a message to a Light Modem\n"));
    _tprintf(TEXT("5. Help\n"));
    _tprintf(TEXT("6. Exit\n"));
    _tprintf(TEXT("============\n"));
}

void HandleUserInput() {
    int choice;
    _tprintf(TEXT("Enter your choice: \n"));
    _tscanf(TEXT("%d"), &choice);
    FlushStdInBuffer();

    switch (choice) {
    case 1:
        UpdateModemSettings(&acousticModem, TEXT("AcousticModem"));
        break;
    case 2:
        UpdateModemSettings(&lightModem, TEXT("LightModem"));
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
        _tprintf(TEXT("Please enter a number between 1 and 6.\n"));
        _gettchar();
        break;
    }
}

DWORD WINAPI ReadThread(LPVOID param) {
    ModemConfig* modem = (ModemConfig*)param;
    TCHAR buffer[2048];
    DWORD bytesRead;
    DWORD dwEventMask = 0;

    while (keepRunning) {
        dwEventMask = 0;
        if (ReadFile(modem->hSerial, buffer, 2047, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                if (modem->portName[0] == TEXT('A')) {
                    _tprintf(TEXT("Received Message(%s) >> %s\n"), modem->portName, buffer);
                }
                else {
                    printf("Received Message(%s) >> %s\n", modem->portName, buffer);
                }
            }
        }
    }
    return 0;
}

void SignalHandler(int signal) {
    if (signal == SIGINT) {
        keepRunning = false;
    }
}
