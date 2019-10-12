// MiningSwitcher.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <vector>
#include <string>
#include <urlmon.h>
#include <iostream>
#include <algorithm>

#include "FlatFileDB.h"

#pragma comment(lib,"urlmon.lib")

template < class ContainerT >
void tokenize(const std::string& str, ContainerT& tokens,
    const std::string& delimiters = " ", bool trimEmpty = false)
{
    std::string::size_type pos, lastPos = 0, length = str.length();

    using value_type = typename ContainerT::value_type;
    using size_type  = typename ContainerT::size_type;

    while(lastPos < length + 1)
    {
        pos = str.find(delimiters, lastPos);
        if(pos == std::string::npos)
        {
            pos = length;
        }

        if(pos != lastPos || !trimEmpty)
            tokens.push_back(value_type(str.data()+lastPos,
            (size_type)pos-lastPos ));

        lastPos = pos + delimiters.size();
    }
}


std::string GetWebData( const std::string& strURL )
{
    LPSTREAM pStream;
    HRESULT hr;

    std::string strResponse;

    hr = URLOpenBlockingStream( NULL, strURL.c_str(), &pStream, 0, NULL );

    if( hr != S_OK )
    {
        //m_staticVerRemote.SetWindowText( "Unable to check!" );
        return std::string();
    }

    while( SUCCEEDED( hr ) )
    {
        char buffer[2048];	// Grab 2k chunks
        DWORD nRead = 0;

        hr = pStream->Read( buffer, sizeof( buffer ) - 1, &nRead );

        if( nRead == 0 )
        {
            break;
        }

        buffer[ nRead ] = 0;
        strResponse += buffer;
    }

    return strResponse;
}

bool IsPIDRunning(uint32_t pid)
{
    if (pid == 0) 
    {
        // Special case for system
        return false;
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (processHandle == NULL) {
        //invalid parameter means PID isn't in the system
        if (GetLastError() == ERROR_INVALID_PARAMETER) { 
            return false;
        }

        //some other error with OpenProcess
        return false;
    }

    DWORD exitCode(0);
    if (GetExitCodeProcess(processHandle, &exitCode)) 
    {
        CloseHandle(processHandle);
        return (exitCode == STILL_ACTIVE);
    }

    CloseHandle(processHandle);
    return false;
}

bool GetCurrentXMGReward(const std::string& urlToParse, double& currentReward)
{
    std::string strPage = GetWebData(urlToParse);

    if(strPage.empty())
    {
        std::cout << "Returned page was empty!" << std::endl;
        return false;
    }

    std::string::size_type offsetToBlocksTable = strPage.find("Last 20 Blocks Found");
    std::string::size_type offsetToBlocksTableBody = strPage.find("<tbody>", offsetToBlocksTable);
    std::string::size_type offsetToBlocksTableRowEnd = strPage.find("</tr>", offsetToBlocksTableBody);

    std::string tableRow = strPage.substr(offsetToBlocksTableBody, offsetToBlocksTableRowEnd - offsetToBlocksTableBody);
    tableRow.erase(std::remove(tableRow.begin(), tableRow.end(), ' '), tableRow.end());
    tableRow.erase(std::remove(tableRow.begin(), tableRow.end(), '\n'), tableRow.end());

    std::vector<std::string> tok;
    tokenize<std::vector<std::string> >(tableRow, tok, "</td><tdclass=\"text-right\">");

    currentReward = atof(tok[3].c_str());
    return true;
}

bool TerminateProcesses(DWORD pid)
{
    DWORD dwDesiredAccess = PROCESS_TERMINATE;
    BOOL  bInheritHandle  = FALSE;
    HANDLE hProcess = OpenProcess(dwDesiredAccess, bInheritHandle, pid);
    if (hProcess == NULL)
    {
        return false;
    }

    DWORD exitCode(0);
    BOOL result = TerminateProcess(hProcess, exitCode);

    CloseHandle(hProcess);

    return result == TRUE;
}

bool StartMiner(const std::string& path, const std::string& args, DWORD& monitoredPID)
{
    STARTUPINFO         siStartupInfo;
    PROCESS_INFORMATION piProcessInfo;

    memset(&siStartupInfo, 0, sizeof(siStartupInfo));
    memset(&piProcessInfo, 0, sizeof(piProcessInfo));

    siStartupInfo.cb = sizeof(siStartupInfo);
    monitoredPID = 0;

    if(CreateProcess(path.c_str(),       // Application name
        (LPSTR)args.c_str(),                 // Additional application arguments
        NULL,
        NULL,
        FALSE,
        CREATE_NEW_CONSOLE | CREATE_DEFAULT_ERROR_MODE,
        NULL,
        NULL,
        &siStartupInfo,
        &piProcessInfo) == FALSE)
    {
        std::cout << "Unable to start! (" << path << " " << args << ")!" << std::endl;
    }
    else
    {
        monitoredPID = piProcessInfo.dwProcessId;
    }

    CloseHandle(piProcessInfo.hThread);
    CloseHandle(piProcessInfo.hProcess);

    return true;
}

int main(int /*argc*/, char *argv[])
{
    std::string pathToSettings = argv[0];
    pathToSettings = pathToSettings.substr(0, pathToSettings.rfind("\\"));
    pathToSettings += "\\settings.txt";

    CTextFileDB settings(pathToSettings);

    if( settings.GetEntry("AltMinerEXE").empty() || 
        settings.GetEntry("AltMinerParams").empty() ||
        settings.GetEntry("XMGMinerEXE").empty() ||
        settings.GetEntry("XMGMinerParams").empty() ||
        settings.GetEntry("XMGThreshold").empty() ||
        settings.GetEntry("CheckTimeMinutes").empty() || 
        settings.GetEntry("URLToParse").empty())
    {
        std::cout << "Missing configuration options!" << std::endl;
        return 0;
    }

    std::string urlToParse = settings.GetEntry("URLToParse");

    bool shouldContinue = true;
    bool isXMGMinerRunning = false;
    bool isAltMinerRunning = false;

    DWORD monitoredPID = 0;

    const double XMGRewardThreshold = atof(settings.GetEntry("XMGThreshold").c_str());
    const uint32_t minutesBetweenChecks = atoi(settings.GetEntry("CheckTimeMinutes").c_str());
    std::cout << "Threshold configured as " << XMGRewardThreshold << " XMG/block, time between checks " << minutesBetweenChecks << " min" <<std::endl;
    std::cout << "Checking URL " << urlToParse << std::endl;

    double currentReward = 0;
    double previousReward = 0;
    
    while (shouldContinue)
    {
        // Check if monitored PID is still running
        if(monitoredPID != 0)
        {
            if(!IsPIDRunning(monitoredPID))
            {
                isXMGMinerRunning = false;
                isAltMinerRunning = false;

                std::cout << "Monitored PID (" << monitoredPID << ") no longer detected as running! Will restart.." << std::endl;
            }
        }

        previousReward = currentReward;

        // Update current XMG reward
        bool failedToUpdateLastReward = false;
        if(!GetCurrentXMGReward(urlToParse, currentReward))
        {
            failedToUpdateLastReward = true;
            std::cout << "Failed to get last XMG reward!" << std::endl;
        }
        else
        {
            std::cout << "XMG reward is now " << currentReward << " (threshold " << XMGRewardThreshold << ")" << std::endl;
        }

        // If there has been a reward update
        // or neither miner is running ...
        if( previousReward != currentReward || 
            ( !isXMGMinerRunning && !isAltMinerRunning ))
        {
            // Check which miner should be running and stop/start as required

            if(currentReward < XMGRewardThreshold)
            {
                if(isXMGMinerRunning)
                {
                    // terminate
                    if(!TerminateProcesses(monitoredPID))
                    {
                        std::cout << "Failed to terminate process (" << monitoredPID << ")!!" << std::endl;
                    }
                    else
                    {
                        isXMGMinerRunning = false;
                    }
                }

                if(!isAltMinerRunning)
                {
                    // start
                    if(!StartMiner(settings.GetEntry("AltMinerEXE"), settings.GetEntry("AltMinerParams"), monitoredPID))
                    {
                        std::cout << "Failed to start miner!" << std::endl;
                    }
                    else
                    {
                        std::cout << "Started alt miner on pid " << monitoredPID << std::endl;
                        isAltMinerRunning = true;
                    }
                }
        
            }
            else
            {
                if(isAltMinerRunning)
                {
                    // terminate
                    if(!TerminateProcesses(monitoredPID))
                    {
                        std::cout << "Failed to terminate process (" << monitoredPID << ")!!" << std::endl;
                    }
                    else
                    {
                        isAltMinerRunning = false;
                    }
                }

                if(!isXMGMinerRunning)
                {
                    // start
                    if(!StartMiner(settings.GetEntry("XMGMinerEXE"), settings.GetEntry("XMGMinerParams"), monitoredPID))
                    {
                        std::cout << "Failed to start miner!" << std::endl;
                    }
                    else
                    {
                        std::cout << "Started XMG miner on pid " << monitoredPID << std::endl;
                        isXMGMinerRunning = true;
                    }
                }
            }
        }

        // Wait for x minutes
        const uint32_t MilliSecondsToMinutes = 1000 * 60;
        uint32_t timetoWait = MilliSecondsToMinutes * minutesBetweenChecks;
        if (failedToUpdateLastReward)
        {
            std::cout << "Waiting for 1 minute instead of " << minutesBetweenChecks << " due to failed check" << std::endl;
            timetoWait = MilliSecondsToMinutes  * 1;
        }

        Sleep(timetoWait);
    }

    return 0;
}

