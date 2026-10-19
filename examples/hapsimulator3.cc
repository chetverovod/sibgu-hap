/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2026 Reshetnev University
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Igor Plastov <chetverovod@gmail.com> Reshetnev University
 *
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"
#include "ns3/trace-helper.h"
#include "ns3/satellite-env-variables.h"
#include "ns3/system-path.h"
#include "ns3/satellite-net-device.h"
#include "ns3/satellite-typedefs.h"
#include "ns3/satellite-enums.h"
#include "../stats/device-ip-table.h"
#include "../model/orbiter-trajectory-validation.h"
#include "../stats/pcap-node-tracing.h"
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace ns3;

#define TEST_NAME "hapsimulator3"
NS_LOG_COMPONENT_DEFINE(TEST_NAME);

namespace
{

constexpr const char* kHapDataRoot = "contrib/sibgu-hap/data";

void
AddIssueIf(bool bad, const std::string& message, std::vector<std::string>& issues)
{
    if (bad)
    {
        issues.push_back(message);
    }
}

void
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

void
ValidateScenarioLayout(const std::string& scenarioRoot, std::vector<std::string>& issues)
{
    namespace fs = std::filesystem;

    if (!fs::exists(scenarioRoot) || !fs::is_directory(scenarioRoot))
    {
        issues.push_back("Scenario directory not found: " + scenarioRoot +
                         " - run from the ns-3 tree root (relative paths), or fix --scenarioName / "
                         "--scenarioPath / DataPath.");
        return;
    }

    const auto fileOk = [&](const std::string& rel) {
        const std::string p = scenarioRoot + rel;
        return fs::is_regular_file(p);
    };
    const auto dirOk = [&](const std::string& rel) {
        const std::string p = scenarioRoot + rel;
        return fs::exists(p) && fs::is_directory(p);
    };

    AddIssueIf(!fileOk("/standard/standard.txt"),
               "Missing: " + scenarioRoot + "/standard/standard.txt (DVB/LORA and frame settings).",
               issues);
    AddIssueIf(!fileOk("/beams/fwdConf.txt"), "Missing: " + scenarioRoot + "/beams/fwdConf.txt", issues);
    AddIssueIf(!fileOk("/beams/rtnConf.txt"), "Missing: " + scenarioRoot + "/beams/rtnConf.txt", issues);
    AddIssueIf(!dirOk("/waveforms"), "Missing directory: " + scenarioRoot + "/waveforms", issues);
    AddIssueIf(!fileOk("/positions/ut_positions.txt"),
               "Missing: " + scenarioRoot + "/positions/ut_positions.txt",
               issues);
    AddIssueIf(!fileOk("/positions/gw_positions.txt"),
               "Missing: " + scenarioRoot + "/positions/gw_positions.txt",
               issues);

    const std::string pos = scenarioRoot + "/positions/";
    const bool hasTles = fs::is_regular_file(pos + "tles.txt");
    const bool hasSatPos = fs::is_regular_file(pos + "sat_positions.txt");
    AddIssueIf(!hasTles && !hasSatPos,
               "Need either " + pos + "tles.txt (constellation) or " + pos +
                   "sat_positions.txt (static GEO).",
               issues);
    if (hasTles && !fs::is_regular_file(pos + "start_date.txt"))
    {
        issues.push_back("tles.txt present but missing " + pos + "start_date.txt (epoch for SGP4).");
    }

    AddIssueIf(!dirOk("/antennapatterns"),
               "Missing directory: " + scenarioRoot +
                   "/antennapatterns - SatAntennaGainPatternContainer will fail (see GeoPos.in). "
                   "Symlink or copy e.g. from contrib/sibgu-hap/data/scenarios/geo-33E-hap/antennapatterns.",
               issues);
    AddIssueIf(!fileOk("/antennapatterns/GeoPos.in"),
               "Missing: " + scenarioRoot +
                   "/antennapatterns/GeoPos.in - required for default antenna GEO reference.",
               issues);
}

void
PrintIssuesAndAbort(const std::vector<std::string>& issues)
{
    if (issues.empty())
    {
        return;
    }
    NS_LOG_UNCOND("[hapsimulator3] Input / scenario validation failed:");
    for (const auto& line : issues)
    {
        NS_LOG_UNCOND("  * " << line);
    }
    NS_LOG_UNCOND("[hapsimulator3] PacketTrace.log may stay header-only when scenario creation fails "
                  "or traffic cannot traverse PHY/MAC/channel.");
    NS_FATAL_ERROR("hapsimulator3: fix the listed issues and re-run.");
}

void
PrintRuntimeHints()
{
    NS_LOG_UNCOND("[hapsimulator3] Scenario layout OK. If PacketTrace.log stays header-only, the "
                  "stack is not carrying frames (geometry vs antenna patterns, beam choice, "
                  "DisableSchedulingIfNoDeviceConnected, SINR/scheduler). Use PCAP on UT/GW users or "
                  "NS_LOG on SatPhy/SatMac to debug.");
}

/** Remove runtask-only flags so CommandLine does not treat them as unknown arguments. */
std::vector<std::string>
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
        if (!argv[i])
        {
            continue;
        }
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

} // namespace

// ============================================================================
// main
// ============================================================================
int
main(int argc, char* argv[])
{
    Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    
    Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
    Config::SetDefault("ns3::SatHelper::HandoversEnabled", BooleanValue(true));
    Config::SetDefault("ns3::SatHandoverModule::NumberClosestSats", UintegerValue(3));
    Config::SetDefault("ns3::SatGwMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
    Config::SetDefault("ns3::SatOrbiterMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
    Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(true));

    float simulationDuration = 2.0; // seconds
    std::string scenarioName = "constellation-leo-3-satellites-hap";
    uint32_t packetSize = 512; // Packet size in bytes
    float interval = 100.0; // Time interval between CBR packets in milliseconds
    bool enablePcap = false;
    bool enableHexDump = false;

    std::string simsDir;
    std::string scenarioPath;
    std::string scenarioLayoutRoot = std::string(kHapDataRoot) + "/scenarios/" + scenarioName;
    std::vector<std::string> argvStorage = FilterArgvForCommandLine(argc, argv, simsDir, scenarioPath);
    std::vector<char*> argvFiltered;
    argvFiltered.reserve(argvStorage.size());
    for (auto& s : argvStorage)
    {
        argvFiltered.push_back(s.data());
    }

    if (scenarioPath.empty())
    {
        Config::SetDefault("ns3::SatEnvVariables::DataPath", StringValue(kHapDataRoot));
    }

    // Declare command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of CBR packets in bytes", packetSize);
    cmd.AddValue("interval", "Time interval between CBR packets, in milliseconds", interval);
    cmd.AddValue("scenarioName", "Scenario name", scenarioName);
    cmd.AddValue("simulationDuration", "Simulation duration, in seconds", simulationDuration);
    cmd.AddValue("enablePcap", "Enable PCAP", enablePcap);
    cmd.AddValue("enableHexDump", "Enable Hex-Dump", enableHexDump);

    std::string simulationName = TEST_NAME;
    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>(simulationName);
    simulationHelper->AddDefaultUiArguments(cmd); // Adds default UI arguments (simulation time, etc.)
    cmd.Parse(static_cast<int>(argvFiltered.size()), argvFiltered.data());

    {
        std::vector<std::string> issues;
        ValidateCliInputs(simulationDuration, interval, packetSize, simsDir, issues);
        PrintIssuesAndAbort(issues);
    }

    if (!scenarioPath.empty())
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path absScenario = fs::canonical(scenarioPath, ec);
        NS_ABORT_MSG_IF(ec || !fs::is_directory(absScenario),
                        "--scenarioPath must be an existing directory (absolute or cwd-relative); "
                        "got: "
                            << scenarioPath);
        const fs::path taskRoot = absScenario.parent_path();
        const fs::path scenariosDir = taskRoot / "scenarios";
        fs::create_directories(scenariosDir, ec);
        NS_ABORT_MSG_IF(ec, "Cannot create directory: " << scenariosDir.string());
        const fs::path linkPath = scenariosDir / scenarioName;
        fs::remove_all(linkPath, ec);
        ec.clear();
        fs::create_directory_symlink(absScenario, linkPath, ec);
        NS_ABORT_MSG_IF(ec,
                        "Cannot symlink scenario directory " << absScenario.string() << " -> "
                                                               << linkPath.string());
        std::error_code ecRoot;
        const std::string taskRootCanonical = fs::weakly_canonical(taskRoot, ecRoot).string();
        NS_ABORT_MSG_IF(ecRoot, "Cannot canonicalize task root: " << taskRoot.string());
        Config::SetDefault("ns3::SatEnvVariables::DataPath", StringValue(taskRootCanonical));
        scenarioLayoutRoot = linkPath.string();
    }

    {
        std::vector<std::string> issues;
        ValidateScenarioLayout(scenarioLayoutRoot, issues);
        PrintIssuesAndAbort(issues);
    }

    std::string fixedOutputDir = SystemPath::Append(simsDir, simulationName + "/");
    SystemPath::MakeDirectories(fixedOutputDir);
    simulationHelper->SetOutputPath(fixedOutputDir);
    simulationHelper->SetSimulationTime(Seconds(simulationDuration));
    uint32_t utUsers = 1;
    simulationHelper->SetGwUserCount(utUsers);
    simulationHelper->SetUserCountPerUt(utUsers);

    std::set<uint32_t> beamSetAll = {1,  2,  3,
                                     4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                     31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
                                     46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
                                     61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72
                                   };
    simulationHelper->SetBeamSet(beamSetAll);
    
    // Scenario with 3 orbiters:
    // - satId 0/1 use TLE
    // - satId 2 uses traced mobility from positions/sat_traces.txt
    //
    // runtask passes --scenarioPath=.../task/scenario (flat unpack). SimulationHelper only exposes
    // LoadScenario(name) which resolves LocateDataDirectory()/scenarios/<name>. When scenarioPath is
    // set, we symlink .../task/scenarios/<scenarioName> -> .../task/scenario and set DataPath to
    // .../task (works without LoadScenarioDirectory, which may be absent in some satellite builds).
    simulationHelper->LoadScenario(scenarioName);

    simulationHelper->CreateSatScenario(SatHelper::NONE);
    std::string outputDir = Singleton<SatEnvVariables>::Get()->GetOutputPath();
    SystemPath::MakeDirectories(outputDir);

    NS_LOG_UNCOND("Output directory set to: " << outputDir);
    PrintRuntimeHints();

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
    ValidateOrbiterTrajectories(scenarioName, topology);

    // ========================================================================
    // Unified device-to-IP mapping table for all roles
    // ========================================================================
    std::vector<std::tuple<uint32_t, std::string, uint32_t,
                std::string, std::string>> ipRows;
    CollectDeviceIpRows(topology->GetGwNodes(), "GW", ipRows);
    CollectDeviceIpRows(topology->GetOrbiterNodes(), "SAT", ipRows);
    CollectDeviceIpRows(topology->GetUtNodes(), "UT", ipRows);
    PrintDeviceIpTable(ipRows);
    SaveDeviceIpTableToFile(ipRows, SystemPath::Append(outputDir, "DevicesTable.txt"));

    // ========================================================================
    // PCAP for all nodes
    // ========================================================================
   
    // PCAP for all nodes
    if (enablePcap)
    {
    EnablePcapForNodeContainer(topology->GetGwNodes(),
                              "sat-handover-gw", outputDir,
                              "GW", enableHexDump);
    EnablePcapForNodeContainer(topology->GetOrbiterNodes(),
                               "sat-handover-orbiter", outputDir,
                               "SAT", enableHexDump);
    EnablePcapForNodeContainer(topology->GetUtNodes(),
                               "sat-handover-ut", outputDir,
                               "UT", enableHexDump);
    }

    // ========================================================================
    // Traffic
    // ========================================================================
    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::FWD_LINK, SatTrafficHelper::UDP, MilliSeconds(interval),
        packetSize,
        NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)),
        Singleton<SatTopology>::Get()->GetUtUserNodes(),
        Seconds(1.0), Seconds(simulationDuration), Seconds(0));

    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::RTN_LINK, SatTrafficHelper::UDP, MilliSeconds(interval),
        packetSize,
        NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)),
        Singleton<SatTopology>::Get()->GetUtUserNodes(),
        Seconds(1.0), Seconds(simulationDuration), Seconds(0));

    Config::SetDefault("ns3::ConfigStore::Filename", 
        StringValue(SystemPath::Append(outputDir, TEST_NAME"-attributes.xml")));
    Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
    Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
    ConfigStore outputConfig;
    outputConfig.ConfigureDefaults();

    
    // Statistics
    Ptr<SatStatsHelperContainer> s = simulationHelper->GetStatisticsContainer();
    s->AddPerSatFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatRtnUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerBeamFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerBeamFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerBeamBeamServiceTime(SatStatsHelper::OUTPUT_SCALAR_FILE);

    // Packet loss and collision diagnostics
    s->AddGlobalFwdUserDaPacketError(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerBeamFwdUserDaPacketError(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddGlobalRtnFeederDaPacketError(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerBeamRtnFeederDaPacketError(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerBeamFeederCrdsaPacketCollision(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerBeamFeederCrdsaPacketError(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerBeamFeederSlottedAlohaPacketCollision(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerBeamFeederSlottedAlohaPacketError(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddGlobalPacketDropRate(SatStatsHelper::OUTPUT_SCALAR_FILE);
    s->AddPerIslPacketDropRate(SatStatsHelper::OUTPUT_SCALAR_FILE);

    simulationHelper->EnableProgressLogs();

    NS_LOG_UNCOND("Configured simulator stop time (virtual): "
                  << simulationHelper->GetSimTime().GetSeconds() << " s");
    const auto simulationStart = std::chrono::steady_clock::now();
    simulationHelper->RunSimulation();
    const auto simulationEnd = std::chrono::steady_clock::now();
    const auto simulationElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(simulationEnd - simulationStart);
    NS_LOG_UNCOND("Simulation wall-clock time (CPU, not virtual sim time): "
                  << (simulationElapsed.count() / 1000.0) << " s");

    return 0;
}
