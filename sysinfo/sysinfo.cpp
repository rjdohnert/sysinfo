#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <iomanip>
#include <thread>
#include <chrono>

// Link required libraries for MSVC
#pragma comment(lib, "advapi32.lib")

// Helper function to read a string value from the registry
std::string GetRegistryString(HKEY hKeyParent, const std::string& subKey, const std::string& valueName) {
    HKEY hKey;
    std::string result = "Unknown";
    if (RegOpenKeyExA(hKeyParent, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256] = { 0 };
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            result = buffer;
        }
        RegCloseKey(hKey);
    }
    return result;
}

// Retrieves the System Brand (Manufacturer and Model Name)
std::string GetSystemBrand() {
    std::string manufacturer = GetRegistryString(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemManufacturer");
    std::string model = GetRegistryString(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName");
    
    // Trim extra spaces if present
    if (manufacturer == "Unknown" && model == "Unknown") {
        return "Generic System / Custom PC";
    }
    return manufacturer + " (Model: " + model + ")";
}

// Helper function to convert FILETIME to unsigned 64-bit integer
ULONGLONG FileTimeToULL(const FILETIME& ft) {
    return (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Retrieves the CPU brand name from the registry
std::string GetCPUName() {
    return GetRegistryString(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString");
}

// Measures active CPU utilization over a 500ms window
double GetCPUUsage() {
    FILETIME idleTime1, kernelTime1, userTime1;
    FILETIME idleTime2, kernelTime2, userTime2;

    if (!GetSystemTimes(&idleTime1, &kernelTime1, &userTime1)) {
        return -1.0;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!GetSystemTimes(&idleTime2, &kernelTime2, &userTime2)) {
        return -1.0;
    }

    ULONGLONG idle1 = FileTimeToULL(idleTime1);
    ULONGLONG kernel1 = FileTimeToULL(kernelTime1);
    ULONGLONG user1 = FileTimeToULL(userTime1);

    ULONGLONG idle2 = FileTimeToULL(idleTime2);
    ULONGLONG kernel2 = FileTimeToULL(kernelTime2);
    ULONGLONG user2 = FileTimeToULL(userTime2);

    ULONGLONG idleDiff = idle2 - idle1;
    ULONGLONG kernelDiff = kernel2 - kernel1;
    ULONGLONG userDiff = user2 - user1;

    ULONGLONG totalSystemDiff = kernelDiff + userDiff;

    if (totalSystemDiff == 0) {
        return 0.0;
    }

    ULONGLONG activeTimeDiff = totalSystemDiff - idleDiff;
    return (double)(activeTimeDiff) * 100.0 / totalSystemDiff;
}

// Returns the total number of running processes
int GetRunningProcessCount() {
    int count = 0;
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        return -1;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hProcessSnap, &pe32)) {
        do {
            count++;
        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);
    return count;
}

// Outputs active RAM statistics
void DisplayMemoryInfo() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);

    std::cout << "--- Memory Information ---\n";
    if (GlobalMemoryStatusEx(&memInfo)) {
        double totalGB = (double)memInfo.ullTotalPhys / (1024 * 1024 * 1024);
        double availGB = (double)memInfo.ullAvailPhys / (1024 * 1024 * 1024);
        double usedGB = totalGB - availGB;

        std::cout << "Total Physical RAM:  " << std::fixed << std::setprecision(2) << totalGB << " GB\n";
        std::cout << "Used Physical RAM:   " << usedGB << " GB (" << memInfo.dwMemoryLoad << "%)\n";
        std::cout << "Available RAM:       " << availGB << " GB\n";
    } else {
        std::cout << "Error retrieving memory status.\n";
    }
    std::cout << "\n";
}

// Displays system uptime converted from milliseconds
void DisplaySystemUptime() {
    ULONGLONG ms = GetTickCount64();
    ULONGLONG seconds = ms / 1000;
    ULONGLONG minutes = seconds / 60;
    seconds %= 60;
    ULONGLONG hours = minutes / 60;
    minutes %= 60;
    ULONGLONG days = hours / 24;
    hours %= 24;

    std::cout << "--- System Uptime ---\n";
    std::cout << days << " days, " << hours << " hours, " 
              << minutes << " minutes, " << seconds << " seconds\n\n";
}

// Iterates through drives, identifies drive type, and attempts to read file systems
void DisplayDriveInformation() {
    std::cout << "--- Disk Drives & File Systems ---\n";
    char driveBuffer[256];
    DWORD bufferSize = sizeof(driveBuffer);
    DWORD result = GetLogicalDriveStringsA(bufferSize, driveBuffer);

    if (result == 0 || result > bufferSize) {
        std::cout << "Failed to query system drives.\n\n";
        return;
    }

    char* drive = driveBuffer;
    while (*drive) {
        std::string driveLetter = drive;
        UINT driveType = GetDriveTypeA(driveLetter.c_str());
        
        std::string typeStr;
        switch (driveType) {
            case DRIVE_UNKNOWN:     typeStr = "Unknown Type"; break;
            case DRIVE_NO_ROOT_DIR: typeStr = "Invalid Root Path"; break;
            case DRIVE_REMOVABLE:   typeStr = "Removable Media"; break;
            case DRIVE_FIXED:       typeStr = "Fixed Disk (HDD/SSD)"; break;
            case DRIVE_REMOTE:      typeStr = "Network Share"; break;
            case DRIVE_CDROM:       typeStr = "CD-ROM/Optical"; break;
            case DRIVE_RAMDISK:     typeStr = "RAM Disk"; break;
            default:                typeStr = "Unknown"; break;
        }

        char volumeName[MAX_PATH + 1] = { 0 };
        char fileSystemName[MAX_PATH + 1] = { 0 };
        DWORD serialNumber = 0;
        DWORD maxComponentLen = 0;
        DWORD fileSystemFlags = 0;

        BOOL gotVolInfo = GetVolumeInformationA(
            driveLetter.c_str(),
            volumeName, sizeof(volumeName),
            &serialNumber,
            &maxComponentLen,
            &fileSystemFlags,
            fileSystemName, sizeof(fileSystemName)
        );

        std::cout << "  Drive: " << driveLetter << " (" << typeStr << ")\n";
        if (gotVolInfo) {
            std::cout << "    Label:       " << (strlen(volumeName) > 0 ? volumeName : "[None]") << "\n";
            std::cout << "    File System: " << fileSystemName << "\n";
        } else {
            std::cout << "    File System: [Not Ready/Unavailable]\n";
        }
        std::cout << "\n";

        // Increment to the next null-terminated string in the buffer
        drive += driveLetter.size() + 1;
    }
}

int main() {
    std::cout << "Gathering system statistics...\n\n";

    // 1. System Brand
    std::cout << "--- System Brand ---\n";
    std::cout << "Device Brand:        " << GetSystemBrand() << "\n\n";

    // 2. CPU
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    std::cout << "--- CPU Information ---\n";
    std::cout << "Model Name:          " << GetCPUName() << "\n";
    std::cout << "Logical Cores:       " << sysInfo.dwNumberOfProcessors << "\n";
    double cpuUsage = GetCPUUsage();
    if (cpuUsage >= 0) {
        std::cout << "Current Usage:       " << std::fixed << std::setprecision(1) << cpuUsage << "%\n";
    } else {
        std::cout << "Current Usage:       Unavailable\n";
    }
    std::cout << "\n";

    // 3. Memory
    DisplayMemoryInfo();

    // 4. System Uptime
    DisplaySystemUptime();

    // 5. Processes
    std::cout << "--- Process Information ---\n";
    int processCount = GetRunningProcessCount();
    if (processCount != -1) {
        std::cout << "Active Processes:    " << processCount << "\n";
    } else {
        std::cout << "Active Processes:    Unavailable\n";
    }
    std::cout << "\n";

    // 6. Drives
    DisplayDriveInformation();

    return 0;
}