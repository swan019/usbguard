#include <windows.h>
#include <dbt.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include <commdlg.h>
#include <windowsx.h>
#include "resource.h"

using namespace std;

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);


class USBDevice {
public:
    string driveLetter;
    string signature;

    USBDevice(const string& drive, const string& sig) : driveLetter(drive), signature(sig) {}
};

class USBWhitelistManager {
    set<string> whitelist;
    const string filename = "whitelist.txt";

    USBWhitelistManager() {
        LoadFromFile();
    }

    void LoadFromFile() {
        ifstream in(filename);
        string line;
        while (getline(in, line)) {
            if (!line.empty()) whitelist.insert(line);
        }
    }

    void SaveToFile() {
        ofstream out(filename);
        for (const auto& sig : whitelist) {
            out << sig << endl;
        }
    }

public:
    static USBWhitelistManager& Instance() {
        static USBWhitelistManager instance;
        return instance;
    }

    bool IsWhitelisted(const string& sig) {
        return whitelist.count(sig);
    }

    void AddToWhitelist(const string& sig) {
        whitelist.insert(sig);
        SaveToFile();
    }

    USBWhitelistManager(const USBWhitelistManager&) = delete;
    void operator=(const USBWhitelistManager&) = delete;
};

class USBBlocker {
public:
    static void BlockAndEject(const string& driveLetter) {
        wstring path = L"\\\\.\\" + wstring(driveLetter.begin(), driveLetter.end()) + L":";

        HANDLE hVolume = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

        if (hVolume == INVALID_HANDLE_VALUE) return;

        DWORD bytesReturned;
        DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        DeviceIoControl(hVolume, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &bytesReturned, NULL);

        CloseHandle(hVolume);
        cout << "[!] USB drive " << driveLetter << ":\\ has been blocked and ejected." << endl;
    }
};

class USBMonitor {
public:
    static string GetSignature(const string& driveLetter) {
        wstring drive = wstring(driveLetter.begin(), driveLetter.end()) + L":\\";
        wchar_t volumeName[MAX_PATH];
        DWORD serialNumber = 0, maxComponentLen = 0, fileSystemFlags = 0;

        if (!GetVolumeInformationW(drive.c_str(), volumeName, MAX_PATH, &serialNumber,
            &maxComponentLen, &fileSystemFlags, NULL, 0)) {
            return "";
        }

        stringstream ss;
        ss << hex << uppercase << serialNumber << ":" << maxComponentLen << ":" << fileSystemFlags;
        return ss.str();
        
    }

    static bool PromptForPIN(HINSTANCE hInst) {
        wchar_t input[256] = { 0 };

        INT_PTR result = DialogBoxParam(
            hInst,
            MAKEINTRESOURCE(IDD_PIN_DIALOG),
            NULL,
            [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
                switch (msg) {
                case WM_INITDIALOG:
                    // Save the input buffer pointer using SetWindowLongPtr
                    SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
                    return TRUE;

                case WM_COMMAND:
                    if (LOWORD(wParam) == IDOK) {
                        // Retrieve the buffer pointer using GetWindowLongPtr
                        wchar_t* inputBuffer = (wchar_t*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
                        if (inputBuffer) {
                            GetDlgItemText(hDlg, IDC_PIN_INPUT, inputBuffer, 256);
                        }
                        EndDialog(hDlg, IDOK);
                        return TRUE;
                    }
                    else if (LOWORD(wParam) == IDCANCEL) {
                        EndDialog(hDlg, IDCANCEL);
                        return TRUE;
                    }
                    break;
                }
                return FALSE;
            },
            (LPARAM)input);

        if (result == IDOK) {
            char pin[256];
            size_t convertedChars = 0;
            wcstombs_s(&convertedChars, pin, sizeof(pin), input, _TRUNCATE);

            cout << "Pin : " << pin << endl;

            if (string(pin) == "1234") {
                MessageBoxA(NULL, "Correct PIN! Please Re Insert USB.", "Access Granted", MB_OK | MB_ICONINFORMATION);
                return true;
            }
            else {
                MessageBoxA(NULL, "Access Denied! Please remove USB.", "Unauthorized USB", MB_OK | MB_ICONERROR);
            }
        }

        return false;
    }
};

void AttachConsole() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONIN$", "r", stdin);
    SetConsoleOutputCP(CP_UTF8);
    cout.clear();
}

char GetDriveLetter(ULONG unitmask) {
    char drive = 'A';
    while (unitmask && !(unitmask & 0x1)) {
        unitmask >>= 1;
        drive++;
    }
    return drive;
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            GetDlgItemText(hDlg, IDC_PIN_INPUT, (LPWSTR)lParam, 256);
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL) {
            PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR)lParam;
            if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                auto vol = (PDEV_BROADCAST_VOLUME)hdr;
                char driveLetter = GetDriveLetter(vol->dbcv_unitmask);
                string driveStr(1, driveLetter);
                cout << "[+] USB Inserted: " << driveStr << ":\\\n";

                string sig = USBMonitor::GetSignature(driveStr);  // i take it sig of usb

                if (sig.empty()) {
                    cout << "[-] Failed to get USB signature.\n";
                    return 0;
                }

                USBDevice device(driveStr, sig);  // class created and add this in [Start Folder, sig]
                auto& wl = USBWhitelistManager::Instance();

                /*cout << "In mang " << endl;*/

                if (wl.IsWhitelisted(device.signature)) {
                    cout << "[\u2713] USB is whitelisted. Access granted.\n";
                }

                else {
                    //cout << "Usb Eject Hona Chahiye" << endl;

                    USBBlocker::BlockAndEject(driveStr);  // -> here USB eject 

                    if (USBMonitor::PromptForPIN((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE))) {
                        wl.AddToWhitelist(device.signature);
                        cout << "[\u2713] PIN correct. USB added to whitelist.\n";
                    }
                    else {
                        cout << "[\u2717] Incorrect PIN! Blocking device...\n";
                        USBBlocker::BlockAndEject(driveStr);
                    }
                }
            }
        }
        else if (wParam == DBT_DEVICEREMOVECOMPLETE) {
            cout << "[-] USB Removed.\n";
        }
    }
    else if (msg == WM_DESTROY) {
        PostQuitMessage(0);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    //AttachConsole();
    const wchar_t CLASS_NAME[] = L"USBGuardWindow";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"USB Guard", 0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
