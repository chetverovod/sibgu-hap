#ifndef HAPSIMULATOR_UTILS_H
#define HAPSIMULATOR_UTILS_H

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace HapSimulatorUtils
{

constexpr const char* kHapDataRoot = "contrib/sibgu-hap/data";

/**
 * \brief Вспомогательная функция для добавления ошибки в список
 */
inline void
AddIssueIf(bool bad, const std::string& message, std::vector<std::string>& issues)
{
    if (bad)
    {
        issues.push_back(message);
    }
}

/**
 * \brief Валидация входных параметров командной строки
 */
inline void
ValidateCliInputs(float simulationDuration,
                  float interval,
                  uint32_t packetSize,
                  const std::string& simsDir,
                  std::vector<std::string>& issues)
{
    AddIssueIf(simulationDuration <= 0.0f, "simulationDuration must be > 0 (seconds).", issues);
    AddIssueIf(interval <= 0.0f, "interval must be > 0 (milliseconds between CBR packets).", issues);
    AddIssueIf(packetSize == 0, "packetSize must be > 0.", issues);
    if (simsDir.empty())
    {
        issues.push_back("simsDir is empty.");
    }
}

/**
 * \brief Валидация наличия файлов сценария на диске
 */
inline void
ValidateScenarioLayout(const std::string& scenarioRoot, std::vector<std::string>& issues)
{
    namespace fs = std::filesystem;

    if (!fs::exists(scenarioRoot) || !fs::is_directory(scenarioRoot))
    {
        issues.push_back("Scenario directory not found: " + scenarioRoot);
        return;
    }

    const auto fileOk = [&](const std::string& rel) {
        return fs::is_regular_file(scenarioRoot + rel);
    };
    const auto dirOk = [&](const std::string& rel) {
        return fs::exists(scenarioRoot + rel) && fs::is_directory(scenarioRoot + rel);
    };

    AddIssueIf(!fileOk("/standard/standard.txt"),
               "Missing: " + scenarioRoot + "/standard/standard.txt", issues);
    AddIssueIf(!fileOk("/beams/fwdConf.txt"), "Missing: " + scenarioRoot + "/beams/fwdConf.txt", issues);
    AddIssueIf(!fileOk("/beams/rtnConf.txt"), "Missing: " + scenarioRoot + "/beams/rtnConf.txt", issues);
    AddIssueIf(!dirOk("/waveforms"), "Missing directory: " + scenarioRoot + "/waveforms", issues);
    const bool hasCellular =
        fileOk("/cellular/enb.conf") || fileOk("/cellular/enb_positions.txt");
    if (!hasCellular)
    {
        AddIssueIf(!fileOk("/positions/ut_positions.txt"),
                   "Missing: " + scenarioRoot + "/positions/ut_positions.txt", issues);
    }
    AddIssueIf(!fileOk("/positions/gw_positions.txt"),
               "Missing: " + scenarioRoot + "/positions/gw_positions.txt", issues);

    const std::string pos = scenarioRoot + "/positions/";
    const bool hasTles = fs::is_regular_file(pos + "tles.txt");
    const bool hasSatPos = fs::is_regular_file(pos + "sat_positions.txt");
    AddIssueIf(!hasTles && !hasSatPos,
               "Need either " + pos + "tles.txt or " + pos + "sat_positions.txt", issues);
    if (hasTles && !fs::is_regular_file(pos + "start_date.txt"))
    {
        issues.push_back("tles.txt present but missing " + pos + "start_date.txt");
    }

    AddIssueIf(!dirOk("/antennapatterns"),
               "Missing directory: " + scenarioRoot + "/antennapatterns", issues);
    AddIssueIf(!fileOk("/antennapatterns/GeoPos.in"),
               "Missing: " + scenarioRoot + "/antennapatterns/GeoPos.in", issues);
}

/**
 * \brief Фильтрация аргументов командной строки от кастомных флагов runtask
 */
inline std::vector<std::string>
FilterArgvForCommandLine(int argc,
                         char* argv[],
                         std::string& simsDir,
                         std::string& scenarioPath)
{
    simsDir = std::string(kHapDataRoot) + "/sims";
    scenarioPath.clear();
    std::vector<std::string> out;
    
    if (argc > 0 && argv[0])
    {
        out.emplace_back(argv[0]);
    }
    for (int i = 1; i < argc; ++i)
    {
        if (!argv[i]) continue;
        std::string a(argv[i]);
        
        if (a.rfind("--simsDir=", 0) == 0)
        {
            simsDir = a.substr(10);
            continue;
        }
        if (a.rfind("--scenarioPath=", 0) == 0)
        {
            scenarioPath = a.substr(15);
            continue;
        }
        out.push_back(std::move(a));
    }
    return out;
}

} // namespace HapSimulatorUtils

#endif // HAPSIMULATOR_UTILS_H