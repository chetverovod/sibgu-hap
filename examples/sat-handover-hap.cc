/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Magister Solutions
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
 * Author: Bastien TAURAN <bastien.tauran@viveris.fr>
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
#include "ns3/satellite-sgp4-mobility-model.h"
#include "ns3/satellite-traced-mobility-model.h"
#include "../stats/device-ip-table.h"
#include "../stats/pcap-node-tracing.h"
#include <fstream>
#include <sstream> 
#include <tuple>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("sat-handover-hap");

static std::set<uint32_t>
LoadTraceSatIds(const std::string& scenarioName)
{
    std::set<uint32_t> traceSatIds;
    std::string basePath = Singleton<SatEnvVariables>::Get()->LocateDataDirectory();
    std::string traceMap =
        SystemPath::Append(basePath, "scenarios/" + scenarioName + "/positions/sat_traces.txt");

    if (!Singleton<SatEnvVariables>::Get()->IsValidFile(traceMap))
    {
        return traceSatIds;
    }

    std::ifstream input(traceMap);
    NS_ABORT_MSG_UNLESS(input.is_open(), "Cannot open sat traces map file: " << traceMap);

    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '%')
        {
            continue;
        }

        std::istringstream iss(line);
        uint32_t satId = 0;
        std::string tracePath;
        if (!(iss >> satId >> tracePath))
        {
            continue;
        }

        NS_ABORT_MSG_UNLESS(
            Singleton<SatEnvVariables>::Get()->IsValidFile(tracePath),
            "Trajectory loading failed: satId=" << satId << " has invalid trace path '"
                                                << tracePath
                                                << "' in sat_traces.txt");
        traceSatIds.insert(satId);
    }

    return traceSatIds;
}

static void
ValidateOrbiterTrajectories(const std::string& scenarioName, Ptr<SatTopology> topology)
{
    std::set<uint32_t> traceSatIds = LoadTraceSatIds(scenarioName);
    NodeContainer orbiters = topology->GetOrbiterNodes();

    NS_ABORT_MSG_UNLESS(orbiters.GetN() > 0, "Scenario contains no orbiter nodes");

    for (uint32_t satId = 0; satId < orbiters.GetN(); ++satId)
    {
        Ptr<Node> satNode = orbiters.Get(satId);
        Ptr<SatTracedMobilityModel> tracedModel = satNode->GetObject<SatTracedMobilityModel>();
        Ptr<SatSGP4MobilityModel> tleModel = satNode->GetObject<SatSGP4MobilityModel>();
        bool shouldBeTraced = traceSatIds.count(satId) > 0;

        if (shouldBeTraced)
        {
            NS_ABORT_MSG_UNLESS(tracedModel != nullptr,
                                "Trajectory loading failed: satId=" << satId
                                << " must use SatTracedMobilityModel from sat_traces.txt");
        }
        else
        {
            NS_ABORT_MSG_UNLESS(tleModel != nullptr,
                                "Trajectory loading failed: satId=" << satId
                                << " must use SatSGP4MobilityModel from tles.txt");
        }
    }
}

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
    Config::SetDefault("ns3::SatEnvVariables::DataPath",
                       StringValue("contrib/sibgu-hap/data"));


    float simulationDuration = 2.0; // seconds
    std::string scenarioName = "constellation-leo-3-satellites-hap";
    uint32_t packetSize = 512; // Packet size in bytes
    float interval = 100.0; // Time interval between CBR packets in milliseconds
    bool enablePcap = false;
    bool enableHexDump = false;
    

    // Declare command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of CBR packets in bytes", packetSize);
    cmd.AddValue("interval", "Time interval between CBR packets, in milliseconds", interval);
    cmd.AddValue("scenarioName", "Scenario name", scenarioName);
    cmd.AddValue("simulationDuration", "Simulation duration, in seconds", simulationDuration);
    cmd.AddValue("enablePcap", "Enable PCAP", enablePcap);
    cmd.AddValue("enableHexDump", "Enable Hex-Dump", enableHexDump);

    std::string simulationName = "sat-handover-hap";
    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>(simulationName);
    simulationHelper->AddDefaultUiArguments(cmd); // Adds default UI arguments (simulation time, etc.)
    cmd.Parse(argc, argv); // Parses command-line arguments  
    std::string fixedOutputDir =
        SystemPath::Append("contrib/sibgu-hap/data/sims", simulationName + "/");
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
    simulationHelper->LoadScenario(scenarioName);

    simulationHelper->CreateSatScenario(SatHelper::NONE);
    std::string outputDir = Singleton<SatEnvVariables>::Get()->GetOutputPath();
    SystemPath::MakeDirectories(outputDir);

    NS_LOG_UNCOND("Output directory set to: " << outputDir);

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
    ValidateOrbiterTrajectories(scenarioName, topology);

    // ========================================================================
    // Единая таблица соответствий устройств и IP адресов для всех ролей
    // ========================================================================
    std::vector<std::tuple<uint32_t, std::string, uint32_t,
                std::string, std::string>> ipRows;
    CollectDeviceIpRows(topology->GetGwNodes(), "GW", ipRows);
    CollectDeviceIpRows(topology->GetOrbiterNodes(), "SAT", ipRows);
    CollectDeviceIpRows(topology->GetUtNodes(), "UT", ipRows);
    PrintDeviceIpTable(ipRows);
    SaveDeviceIpTableToFile(ipRows, SystemPath::Append(outputDir, "DevicesTable.txt"));

    // ========================================================================
    // PCAP для всех нод
    // ========================================================================
   
    // PCAP for all nodes
    if (enablePcap)
    {
    EnablePcapForNodeContainer(topology->GetGwNodes(), "sat-handover-gw", outputDir, "GW", enableHexDump);
    EnablePcapForNodeContainer(topology->GetOrbiterNodes(),
                               "sat-handover-orbiter",
                               outputDir,
                               "SAT",
                               enableHexDump);
    EnablePcapForNodeContainer(topology->GetUtNodes(), "sat-handover-ut", outputDir, "UT", enableHexDump);
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
        StringValue(SystemPath::Append(outputDir, "sat-handover-hap-attributes.xml")));
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
    simulationHelper->EnableProgressLogs();
   

    simulationHelper->RunSimulation();

    return 0;
}