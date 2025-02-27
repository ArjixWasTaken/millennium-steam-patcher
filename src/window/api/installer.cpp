#include <window/api/installer.hpp>
#include <stdafx.h>

#include <regex>
#include <utils/clr/platform.hpp>
#include <utils/io/input-output.hpp>

namespace Community
{
	void installer::installUpdate(const nlohmann::json& skinData)
	{
		g_processingFileDrop = true;
		g_fileDropStatus = "Updating Theme...";

		bool success = clr_interop::clr_base::instance().start_update(nlohmann::json({
			{"owner", skinData["github"]["owner"]},
			{"repo", skinData["github"]["repo_name"]}
		}).dump(4));

		if (success) {
			g_fileDropStatus = "Updated Successfully!";
		}
		else {
			g_fileDropStatus = "Failed to update theme...";
		}
		Sleep(1500);
		g_processingFileDrop = false;
	}

    const void installer::handleFileDrop(const char* _filePath)
    {
        std::cout << "Dropped file: " << _filePath << std::endl;
        try {
            std::filesystem::path filePath(_filePath);

            if (!std::filesystem::exists(filePath) || !std::filesystem::exists(filePath / "skin.json") || !std::filesystem::is_directory(filePath))
            {
                MsgBox("The dropped skin either doesn't exist, isn't a folder, or doesn't have a skin.json inside. "
                    "Make sure the skin isn't archived, and it exists on your disk", "Can't Add Skin", MB_ICONERROR);
                return;
            }

            std::filesystem::rename(filePath, std::filesystem::path(config.getSkinDir()) / filePath.filename().string());
        }
        catch (const std::filesystem::filesystem_error& error) {
            MsgBox(std::format("An error occured while adding the dropped skin to your library.\nError:\n{}", error.what()).c_str(), "Fatal Error", MB_ICONERROR);
        }
    }

    bool unzip(std::string zipFileName, std::string targetDirectory) {

        std::string powershellCommand = std::format("powershell.exe -Command \"Expand-Archive '{}' -DestinationPath '{}' -Force\"", zipFileName, targetDirectory);

        STARTUPINFO si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (CreateProcess(NULL, const_cast<char*>(powershellCommand.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            return true;
        }
        return false;
    }

    const void installer::handleThemeInstall(std::string fileName, std::string downloadPath)
    {
        g_processingFileDrop = true;
        auto filePath = std::filesystem::path(config.getSkinDir()) / fileName;

        try 
        {
            g_fileDropStatus = std::format("Downloading {}...", fileName);
            file::writeFileBytesSync(filePath, http::get_bytes(downloadPath.c_str()));
            g_fileDropStatus = "Installing Theme and Verifying";

            if (unzip(filePath.string(), config.getSkinDir() + "/"))
            {
                g_fileDropStatus = "Done! Cleaning up...";
                std::this_thread::sleep_for(std::chrono::seconds(2));

                g_openSuccessPopup = true;
                m_Client.parseSkinData(false);
            }
            else {
                std::cout << "couldn't extract file" << std::endl;
                MessageBoxA(GetForegroundWindow(), "couldn't extract file", "Millennium", MB_ICONERROR);
            }
        }
        catch (const http_error&) {
            console.err("Couldn't download bytes from the file");
            MessageBoxA(GetForegroundWindow(), "Couldn't download bytes from the file", "Millennium", MB_ICONERROR);
        }
        catch (const std::exception& err) {
            console.err(std::format("Exception form {}: {}", __func__, err.what()));
            MessageBoxA(GetForegroundWindow(), std::format("Exception form {}: {}", __func__, err.what()).c_str(), "Millennium", MB_ICONERROR);
        }
        g_processingFileDrop = false;
    }
}