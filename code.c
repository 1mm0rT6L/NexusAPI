#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>



#define ID_INPUT_URL     1001
#define ID_INPUT_HEADERS 1002
#define ID_INPUT_BODY    1003
#define ID_BUTTON_SEND   1004
#define ID_BUTTON_CLEAR  1005
#define ID_COMBO_METHOD  1006
#define ID_PAYLOAD       1007
#define ID_OUTPUT        1008



HBITMAP hBackground = NULL;
HFONT hMainFont = NULL, hLabelFont = NULL, hTitleFont = NULL;
HBRUSH hBrushDark = NULL, hBrushGreen = NULL, hBrushOutput = NULL;
HPEN hPenGreen = NULL;
HWND hURLInput, hHeadersInput, hBodyInput, hOutputDisplay, hMethodCombo, hPayloadInput;
char g_Method[10] = "GET";
int matrixX = 0;
HBITMAP hBackBuffer = NULL;
HDC hBackDC = NULL;
int bgWidth = 0, bgHeight = 0;
BOOL bDrawing = FALSE;


void AppendPayloadToUrl(char* url, const char* payload) {
    if (strlen(payload) == 0) return;
    if (strchr(url, '?') == NULL) strcat(url, "?");
    else strcat(url, "&");
    strcat(url, payload);
}

void ShowError(HWND hOutput, const char* message, DWORD errorCode) {
    char buffer[512];
    if (errorCode != 0) sprintf(buffer, "%s (Error: %lu)", message, errorCode);
    else sprintf(buffer, "%s", message);
    SetWindowText(hOutput, buffer);
}

const char* GetStatusText(DWORD statusCode) {
    switch(statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

void SendHttpRequest(const char* url, HWND hOutput) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char host[256] = {0}, path[512] = {0}, headers[1024] = {0}, body[4096] = {0};
    int port = INTERNET_DEFAULT_HTTP_PORT;
    BOOL isHttps = FALSE;

    GetWindowText(hHeadersInput, headers, sizeof(headers));
    GetWindowText(hBodyInput, body, sizeof(body));

    const char* p = url;
    if (strstr(url, "https://") == url) {
        p = url + 8;
        port = INTERNET_DEFAULT_HTTPS_PORT;
        isHttps = TRUE;
    }
    else if (strstr(url, "http://") == url) {
        p = url + 7;
        port = INTERNET_DEFAULT_HTTP_PORT;
    }
    else {
        ShowError(hOutput, "Invalid URL! Use http:// or https://", 0);
        return;
    }

    char* slash = strchr(p, '/');
    if (slash) {
        strncpy(host, p, slash - p);
        strcpy(path, slash);
    }
    else {
        strcpy(host, p);
        strcpy(path, "/");
    }

    if (strlen(host) == 0) {
        ShowError(hOutput, "Invalid host!", 0);
        return;
    }

    WCHAR wHost[256], wPath[512], wHeaders[1024], wMethod[10];
    MultiByteToWideChar(CP_ACP, 0, host, -1, wHost, 256);
    MultiByteToWideChar(CP_ACP, 0, path, -1, wPath, 512);
    MultiByteToWideChar(CP_ACP, 0, headers, -1, wHeaders, 1024);
    MultiByteToWideChar(CP_ACP, 0, g_Method, -1, wMethod, 10);

    hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        ShowError(hOutput, "Failed to create session", GetLastError());
        return;
    }

    DWORD timeout = 30000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    hConnect = WinHttpConnect(hSession, wHost, port, 0);
    if (!hConnect) {
        ShowError(hOutput, "Failed to connect to server", GetLastError());
        WinHttpCloseHandle(hSession);
        return;
    }

    hRequest = WinHttpOpenRequest(hConnect, wMethod, wPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        ShowError(hOutput, "Failed to create request", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    LPCWSTR headersToSend = WINHTTP_NO_ADDITIONAL_HEADERS;
    DWORD headersLength = 0;
    WCHAR wHeadersWithTerm[1024] = {0};
    if (wcslen(wHeaders) > 0) {
        wcscpy(wHeadersWithTerm, wHeaders);
        if (wcslen(wHeadersWithTerm) >= 2 && !(wcsstr(wHeadersWithTerm, L"\r\n"))) wcscat(wHeadersWithTerm, L"\r\n");
        headersToSend = wHeadersWithTerm;
        headersLength = wcslen(wHeadersWithTerm);
    }

    BOOL sendResult = FALSE;
    if ((strcmp(g_Method, "POST") == 0 || strcmp(g_Method, "PUT") == 0) && strlen(body) > 0) {
        sendResult = WinHttpSendRequest(hRequest, headersToSend, headersLength, body, strlen(body), strlen(body), 0);
    }
    else {
        sendResult = WinHttpSendRequest(hRequest, headersToSend, headersLength, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }

    if (!sendResult) {
        ShowError(hOutput, "Failed to send request", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        ShowError(hOutput, "Failed to receive response", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    DWORD statusCode = 0, size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

    char response[65536] = {0}, result[70000] = {0};
    DWORD bytesRead = 0, totalBytes = 0, bufferSize = sizeof(response) - 1;
    while (totalBytes < bufferSize) {
        if (!WinHttpReadData(hRequest, response + totalBytes, bufferSize - totalBytes, &bytesRead)) break;
        if (bytesRead == 0) break;
        totalBytes += bytesRead;
    }
    response[totalBytes] = '\0';

    const char* statusText = GetStatusText(statusCode);
    char headerLine[128];
    sprintf(headerLine, "[ STATUS: %lu %s ]", statusCode, statusText);
    int lineLen = strlen(headerLine);
    char separator[256];
    memset(separator, '-', lineLen);
    separator[lineLen] = '\0';
    
    if (totalBytes > 50000) {
        response[50000] = '\0';
        sprintf(result, "%s\n%s\n\n[ RESPONSE TRUNCATED to 50KB ]\n\n%s\n\n%s", separator, headerLine, response, separator);
    }
    else {
        sprintf(result, "%s\n%s\n\n%s\n\n%s", separator, headerLine, response, separator);
    }
    
    SetWindowText(hOutput, result);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
            
            hMainFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            hLabelFont = CreateFont(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            hTitleFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            
            hBrushDark = CreateSolidBrush(RGB(0, 0, 0));
            hBrushGreen = CreateSolidBrush(RGB(0, 255, 65));
            hBrushOutput = CreateSolidBrush(RGB(10, 10, 10));
            hPenGreen = CreatePen(PS_SOLID, 1, RGB(0, 255, 65));
            
            hBackground = (HBITMAP)LoadImage(NULL, "image.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_SHARED);
            if (hBackground) {
                BITMAP bm;
                GetObject(hBackground, sizeof(BITMAP), &bm);
                bgWidth = bm.bmWidth;
                bgHeight = bm.bmHeight;
            }

            HDC hdc = GetDC(hwnd);
            hBackDC = CreateCompatibleDC(hdc);
            RECT rect;
            GetClientRect(hwnd, &rect);
            hBackBuffer = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
            SelectObject(hBackDC, hBackBuffer);
            ReleaseDC(hwnd, hdc);

            HWND hTitle = CreateWindow("STATIC", "1mm0rT6L said stay", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 8, 620, 35, hwnd, NULL, hInst, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

            HWND hURL = CreateWindow("STATIC", "URL:", WS_CHILD | WS_VISIBLE, 20, 55, 40, 22, hwnd, NULL, hInst, NULL);
            SendMessage(hURL, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
            hURLInput = CreateWindow("EDIT", "https://jsonplaceholder.typicode.com/posts", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 65, 53, 330, 26, hwnd, (HMENU)ID_INPUT_URL, hInst, NULL);
            SendMessage(hURLInput, WM_SETFONT, (WPARAM)hMainFont, TRUE);

            HWND hMethod = CreateWindow("STATIC", "METHOD:", WS_CHILD | WS_VISIBLE, 405, 55, 55, 22, hwnd, NULL, hInst, NULL);
            SendMessage(hMethod, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
            hMethodCombo = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 465, 53, 95, 120, hwnd, (HMENU)ID_COMBO_METHOD, hInst, NULL);
            SendMessage(hMethodCombo, WM_SETFONT, (WPARAM)hMainFont, TRUE);
            SendMessage(hMethodCombo, CB_ADDSTRING, 0, (LPARAM)"GET");
            SendMessage(hMethodCombo, CB_ADDSTRING, 0, (LPARAM)"POST");
            SendMessage(hMethodCombo, CB_ADDSTRING, 0, (LPARAM)"PUT");
            SendMessage(hMethodCombo, CB_ADDSTRING, 0, (LPARAM)"DELETE");
            SendMessage(hMethodCombo, CB_SETCURSEL, 0, 0);

            HWND hParams = CreateWindow("STATIC", "PARAMS:", WS_CHILD | WS_VISIBLE, 20, 92, 50, 22, hwnd, NULL, hInst, NULL);
            SendMessage(hParams, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
            hPayloadInput = CreateWindow("EDIT", "userId=1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 75, 90, 190, 26, hwnd, (HMENU)ID_PAYLOAD, hInst, NULL);
            SendMessage(hPayloadInput, WM_SETFONT, (WPARAM)hMainFont, TRUE);

            HWND hHeaders = CreateWindow("STATIC", "HEADERS:", WS_CHILD | WS_VISIBLE, 280, 92, 60, 22, hwnd, NULL, hInst, NULL);
            SendMessage(hHeaders, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
            hHeadersInput = CreateWindow("EDIT", "Content-Type: application/json", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 345, 90, 215, 26, hwnd, (HMENU)ID_INPUT_HEADERS, hInst, NULL);
            SendMessage(hHeadersInput, WM_SETFONT, (WPARAM)hMainFont, TRUE);

            HWND hBody = CreateWindow("STATIC", "BODY:", WS_CHILD | WS_VISIBLE, 20, 130, 40, 22, hwnd, NULL, hInst, NULL);
            SendMessage(hBody, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
            hBodyInput = CreateWindow("EDIT", "{\n  \"title\": \"foo\",\n  \"body\": \"bar\",\n  \"userId\": 1\n}", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 65, 128, 495, 75, hwnd, (HMENU)ID_INPUT_BODY, hInst, NULL);
            SendMessage(hBodyInput, WM_SETFONT, (WPARAM)hMainFont, TRUE);

            HWND hBtnSend = CreateWindow("BUTTON", "[ SEND ]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 65, 215, 100, 32, hwnd, (HMENU)ID_BUTTON_SEND, hInst, NULL);
            SendMessage(hBtnSend, WM_SETFONT, (WPARAM)hMainFont, TRUE);
            
            HWND hBtnClear = CreateWindow("BUTTON", "[ CLEAR ]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 175, 215, 100, 32, hwnd, (HMENU)ID_BUTTON_CLEAR, hInst, NULL);
            SendMessage(hBtnClear, WM_SETFONT, (WPARAM)hMainFont, TRUE);

            HWND hOutput = CreateWindow("STATIC", ">> RESPONSE <<", WS_CHILD | WS_VISIBLE, 20, 260, 100, 22, hwnd, NULL, hInst, NULL);
            SendMessage(hOutput, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
            
            hOutputDisplay = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 20, 285, 580, 200, hwnd, (HMENU)ID_OUTPUT, hInst, NULL);
            SendMessage(hOutputDisplay, WM_SETFONT, (WPARAM)hMainFont, TRUE);
            
            SetTimer(hwnd, 1, 50, NULL);
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(0, 255, 65));
            SetBkColor(hdc, RGB(0, 0, 0));
            return (LRESULT)hBrushDark;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            HWND hwndCtl = (HWND)lParam;
            
            if (hwndCtl == hOutputDisplay) {
                SetTextColor(hdc, RGB(0, 255, 65));
                SetBkColor(hdc, RGB(5, 5, 5));
                return (LRESULT)hBrushOutput;
            }
            else if (hwndCtl == hBodyInput) {
                SetTextColor(hdc, RGB(0, 255, 65));
                SetBkColor(hdc, RGB(5, 5, 5));
                return (LRESULT)hBrushOutput;
            }
            else {
                SetTextColor(hdc, RGB(0, 255, 65));
                SetBkColor(hdc, RGB(0, 0, 0));
                return (LRESULT)hBrushDark;
            }
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(0, 255, 65));
            SetBkColor(hdc, RGB(0, 0, 0));
            return (LRESULT)hBrushDark;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(0, 255, 65));
            SetBkColor(hdc, RGB(0, 0, 0));
            return (LRESULT)hBrushDark;
        }

        case WM_ERASEBKGND: {
            return 1;
        }

        case WM_PAINT: {
            if (bDrawing) break;
            bDrawing = TRUE;
            
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            if (hBackground) {
                HDC hdcMem = CreateCompatibleDC(hBackDC);
                SelectObject(hdcMem, hBackground);
                
                float winRatio = (float)rect.right / rect.bottom;
                float imgRatio = (float)bgWidth / bgHeight;
                
                int drawX, drawY, drawW, drawH;
                
                if (winRatio > imgRatio) {
                    drawH = rect.bottom;
                    drawW = (int)(drawH * imgRatio);
                    drawX = (rect.right - drawW) / 2;
                    drawY = 0;
                } else {
                    drawW = rect.right;
                    drawH = (int)(drawW / imgRatio);
                    drawX = 0;
                    drawY = (rect.bottom - drawH) / 2;
                }
                
                StretchBlt(hBackDC, drawX, drawY, drawW, drawH, hdcMem, 0, 0, bgWidth, bgHeight, SRCCOPY);
                
                if (drawX > 0) {
                    RECT leftRect = {0, 0, drawX, rect.bottom};
                    RECT rightRect = {drawX + drawW, 0, rect.right, rect.bottom};
                    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
                    FillRect(hBackDC, &leftRect, hBrush);
                    FillRect(hBackDC, &rightRect, hBrush);
                    DeleteObject(hBrush);
                }
                if (drawY > 0) {
                    RECT topRect = {0, 0, rect.right, drawY};
                    RECT bottomRect = {0, drawY + drawH, rect.right, rect.bottom};
                    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
                    FillRect(hBackDC, &topRect, hBrush);
                    FillRect(hBackDC, &bottomRect, hBrush);
                    DeleteObject(hBrush);
                }
                
                DeleteDC(hdcMem);
            }
            else {
                HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(hBackDC, &rect, hBrush);
                DeleteObject(hBrush);
                
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 255, 65));
                SelectObject(hBackDC, hPen);
                for (int i = 0; i < 20; i++) {
                    int x = (i * 32 + matrixX) % rect.right;
                    MoveToEx(hBackDC, x, 0, NULL);
                    LineTo(hBackDC, x, rect.bottom);
                }
                DeleteObject(hPen);
            }
            
            BitBlt(hdc, 0, 0, rect.right, rect.bottom, hBackDC, 0, 0, SRCCOPY);
            
            EndPaint(hwnd, &ps);
            bDrawing = FALSE;
            break;
        }

        case WM_SIZE: {
            RECT rect;
            GetClientRect(hwnd, &rect);
            if (hBackBuffer) DeleteObject(hBackBuffer);
            if (hBackDC) {
                HDC hdc = GetDC(hwnd);
                hBackBuffer = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
                SelectObject(hBackDC, hBackBuffer);
                ReleaseDC(hwnd, hdc);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_TIMER: {
            matrixX += 2;
            if (!hBackground) {
                HDC hdc = GetDC(hwnd);
                RECT rect;
                GetClientRect(hwnd, &rect);
                
                HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(hBackDC, &rect, hBrush);
                DeleteObject(hBrush);
                
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 255, 65));
                SelectObject(hBackDC, hPen);
                for (int i = 0; i < 20; i++) {
                    int x = (i * 32 + matrixX) % rect.right;
                    MoveToEx(hBackDC, x, 0, NULL);
                    LineTo(hBackDC, x, rect.bottom);
                }
                DeleteObject(hPen);
                
                BitBlt(hdc, 0, 0, rect.right, rect.bottom, hBackDC, 0, 0, SRCCOPY);
                ReleaseDC(hwnd, hdc);
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_COMBO_METHOD && HIWORD(wParam) == CBN_SELCHANGE) {
                int index = SendMessage(hMethodCombo, CB_GETCURSEL, 0, 0);
                SendMessage(hMethodCombo, CB_GETLBTEXT, index, (LPARAM)g_Method);
            }
            if (id == ID_BUTTON_SEND) {
                char url[512], payload[256];
                GetWindowText(hURLInput, url, sizeof(url));
                GetWindowText(hPayloadInput, payload, sizeof(payload));
                if (strlen(url) == 0) {
                    SetWindowText(hOutputDisplay, "[ ERROR ] Please enter a URL");
                }
                else {
                    AppendPayloadToUrl(url, payload);
                    SendHttpRequest(url, hOutputDisplay);
                }
            }
            if (id == ID_BUTTON_CLEAR) {
                SetWindowText(hOutputDisplay, "");
            }
            break;
        }

        case WM_DESTROY:
            if (hBackground) DeleteObject(hBackground);
            if (hMainFont) DeleteObject(hMainFont);
            if (hLabelFont) DeleteObject(hLabelFont);
            if (hTitleFont) DeleteObject(hTitleFont);
            if (hBrushDark) DeleteObject(hBrushDark);
            if (hBrushGreen) DeleteObject(hBrushGreen);
            if (hBrushOutput) DeleteObject(hBrushOutput);
            if (hPenGreen) DeleteObject(hPenGreen);
            if (hBackBuffer) DeleteObject(hBackBuffer);
            if (hBackDC) DeleteDC(hBackDC);
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpszClassName = "NexusAPI";
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpfnWndProc = WndProc;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, "NexusAPI", "NexusAPI", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 540, NULL, NULL, hInstance, NULL);
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}